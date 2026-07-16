#include "simple_http_server.h"

#include <QTcpSocket>
#include <QTimer>
#include <QFile>

#include <algorithm>

namespace miata::service {
namespace {

constexpr qsizetype kMaximumHeaderBytes = 16 * 1024;
constexpr qsizetype kMaximumBodyBytes = 1024 * 1024;
constexpr qsizetype kMaximumClients = 16;
constexpr int kClientTimeoutMs = 5000;
constexpr qint64 kDownloadChunkBytes = 64 * 1024;
constexpr qint64 kMaximumQueuedDownloadBytes = 256 * 1024;

QByteArray reasonPhrase(int status) {
    switch (status) {
    case 200: return QByteArrayLiteral("OK");
    case 202: return QByteArrayLiteral("Accepted");
    case 400: return QByteArrayLiteral("Bad Request");
    case 408: return QByteArrayLiteral("Request Timeout");
    case 403: return QByteArrayLiteral("Forbidden");
    case 404: return QByteArrayLiteral("Not Found");
    case 405: return QByteArrayLiteral("Method Not Allowed");
    case 409: return QByteArrayLiteral("Conflict");
    case 413: return QByteArrayLiteral("Payload Too Large");
    case 500: return QByteArrayLiteral("Internal Server Error");
    case 503: return QByteArrayLiteral("Service Unavailable");
    default: return QByteArrayLiteral("Error");
    }
}

}  // namespace

HttpResponse HttpResponse::json(int status, const QByteArray& body) {
    return {status, QByteArrayLiteral("application/json; charset=utf-8"), body};
}

HttpResponse HttpResponse::text(int status, const QByteArray& body) {
    return {status, QByteArrayLiteral("text/plain; charset=utf-8"), body};
}

HttpResponse HttpResponse::file(
    const QString& path, const QByteArray& downloadName, const QByteArray& contentType) {
    HttpResponse response;
    response.status = 200;
    response.contentType = contentType;
    response.filePath = path;
    response.downloadName = downloadName;
    return response;
}

SimpleHttpServer::SimpleHttpServer(QObject* parent) : QObject(parent) {
    server_.setMaxPendingConnections(kMaximumClients);
    connect(&server_, &QTcpServer::newConnection, this, &SimpleHttpServer::acceptConnections);
    connect(&server_, &QTcpServer::acceptError, this,
            [this](QAbstractSocket::SocketError) { emit serverError(server_.errorString()); });
}

void SimpleHttpServer::setHandler(Handler handler) { handler_ = std::move(handler); }

bool SimpleHttpServer::listen(
    const QHostAddress& address, quint16 port, QString* errorMessage) {
    close();
    if (!server_.listen(address, port)) {
        if (errorMessage) *errorMessage = server_.errorString();
        return false;
    }
    return true;
}

void SimpleHttpServer::close() {
    for (auto* socket : buffers_.keys()) {
        socket->abort();
        socket->deleteLater();
    }
    for (auto* socket : downloads_.keys()) {
        socket->abort();
        socket->deleteLater();
    }
    buffers_.clear();
    downloads_.clear();
    server_.close();
}

bool SimpleHttpServer::isListening() const { return server_.isListening(); }
quint16 SimpleHttpServer::serverPort() const { return server_.serverPort(); }

void SimpleHttpServer::acceptConnections() {
    while (server_.hasPendingConnections()) {
        auto* socket = server_.nextPendingConnection();
        if (!socket) continue;
        if (buffers_.size() >= kMaximumClients) {
            respond(socket, HttpResponse::text(503, QByteArrayLiteral("too many connections")));
            socket->deleteLater();
            continue;
        }
        buffers_.insert(socket, {});
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { readClient(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] { removeClient(socket); });
        connect(socket, &QTcpSocket::errorOccurred, this,
                [this, socket](QAbstractSocket::SocketError error) {
            if (error != QAbstractSocket::RemoteHostClosedError)
                emit serverError(socket->errorString());
        });
        QTimer::singleShot(kClientTimeoutMs, socket, [this, socket] {
            if (buffers_.contains(socket))
                respond(socket, HttpResponse::text(408, QByteArrayLiteral("request timed out")));
        });
    }
}

void SimpleHttpServer::readClient(QTcpSocket* socket) {
    auto found = buffers_.find(socket);
    if (found == buffers_.end()) return;
    found.value().append(socket->readAll());
    if (found.value().size() > kMaximumHeaderBytes + kMaximumBodyBytes) {
        respond(socket, HttpResponse::text(413, QByteArrayLiteral("request is too large")));
        return;
    }

    HttpRequest request;
    qsizetype consumed = 0;
    bool incomplete = false;
    QString error;
    if (!parseRequest(found.value(), &request, &consumed, &incomplete, &error)) {
        respond(socket, HttpResponse::text(400, error.toUtf8()));
        return;
    }
    if (incomplete) return;

    buffers_.remove(socket);

    HttpResponse response = handler_
        ? handler_(request)
        : HttpResponse::text(503, QByteArrayLiteral("no request handler is configured"));
    respond(socket, response);
}

void SimpleHttpServer::removeClient(QTcpSocket* socket) {
    buffers_.remove(socket);
    if (auto* file = downloads_.take(socket)) file->deleteLater();
    socket->deleteLater();
}

void SimpleHttpServer::respond(QTcpSocket* socket, const HttpResponse& response) {
    if (!socket || socket->state() == QAbstractSocket::UnconnectedState) return;
    if (!response.filePath.isEmpty()) {
        auto* file = new QFile(response.filePath, socket);
        if (!file->open(QIODevice::ReadOnly)) {
            file->deleteLater();
            respond(socket, HttpResponse::text(404, QByteArrayLiteral("log file is unavailable")));
            return;
        }
        QByteArray name = response.downloadName;
        name.replace('"', '_');
        name.replace('\\', '_');
        name.replace('/', '_');
        const QByteArray header = QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: ")
            + response.contentType + QByteArrayLiteral("\r\nContent-Length: ")
            + QByteArray::number(file->size())
            + QByteArrayLiteral("\r\nContent-Disposition: attachment; filename=\"") + name
            + QByteArrayLiteral("\"\r\nConnection: close\r\nCache-Control: no-store\r\n")
            + QByteArrayLiteral("X-Content-Type-Options: nosniff\r\n\r\n");
        downloads_.insert(socket, file);
        connect(socket, &QTcpSocket::bytesWritten, this,
                [this, socket](qint64) { writeFileChunk(socket); });
        socket->write(header);
        writeFileChunk(socket);
        return;
    }
    QByteArray header = QByteArrayLiteral("HTTP/1.1 ") + QByteArray::number(response.status)
        + ' ' + reasonPhrase(response.status) + QByteArrayLiteral("\r\nContent-Type: ")
        + response.contentType + QByteArrayLiteral("\r\nContent-Length: ")
        + QByteArray::number(response.body.size())
        + QByteArrayLiteral("\r\nConnection: close\r\nCache-Control: no-store\r\n")
        + QByteArrayLiteral("X-Content-Type-Options: nosniff\r\n")
        + QByteArrayLiteral("Content-Security-Policy: default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'\r\n\r\n");
    socket->write(header);
    socket->write(response.body);
    socket->disconnectFromHost();
}

void SimpleHttpServer::writeFileChunk(QTcpSocket* socket) {
    auto found = downloads_.find(socket);
    if (found == downloads_.end() || !socket) return;
    QFile* file = found.value();
    while (socket->bytesToWrite() < kMaximumQueuedDownloadBytes && !file->atEnd()) {
        const QByteArray chunk = file->read(kDownloadChunkBytes);
        if (chunk.isEmpty()) break;
        socket->write(chunk);
    }
    if (file->atEnd()) {
        downloads_.erase(found);
        file->deleteLater();
        socket->disconnectFromHost();
    }
}

bool SimpleHttpServer::parseRequest(
    const QByteArray& buffer, HttpRequest* request, qsizetype* consumed,
    bool* incomplete, QString* errorMessage) {
    *incomplete = false;
    const qsizetype headerEnd = buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        if (buffer.size() > kMaximumHeaderBytes) {
            if (errorMessage) *errorMessage = QStringLiteral("HTTP headers are too large");
            return false;
        }
        *incomplete = true;
        return true;
    }
    if (headerEnd > kMaximumHeaderBytes) {
        if (errorMessage) *errorMessage = QStringLiteral("HTTP headers are too large");
        return false;
    }
    const auto lines = buffer.left(headerEnd).split('\n');
    if (lines.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("HTTP request line is missing");
        return false;
    }
    const auto requestParts = lines.front().trimmed().split(' ');
    if (requestParts.size() != 3 || requestParts.at(2) != QByteArrayLiteral("HTTP/1.1")) {
        if (errorMessage) *errorMessage = QStringLiteral("HTTP request line is invalid");
        return false;
    }
    request->method = requestParts.at(0);
    request->target = requestParts.at(1);
    const qsizetype queryStart = request->target.indexOf('?');
    request->path = queryStart < 0 ? request->target : request->target.left(queryStart);
    if (!request->path.startsWith('/') || request->path.contains("..")) {
        if (errorMessage) *errorMessage = QStringLiteral("HTTP target is invalid");
        return false;
    }

    for (qsizetype index = 1; index < lines.size(); ++index) {
        const QByteArray line = lines.at(index).trimmed();
        const qsizetype separator = line.indexOf(':');
        if (separator <= 0) {
            if (errorMessage) *errorMessage = QStringLiteral("HTTP header is invalid");
            return false;
        }
        request->headers.insert(
            line.left(separator).trimmed().toLower(), line.mid(separator + 1).trimmed());
    }
    if (request->headers.contains(QByteArrayLiteral("transfer-encoding"))) {
        if (errorMessage) *errorMessage = QStringLiteral("HTTP transfer encoding is unsupported");
        return false;
    }
    bool lengthValid = true;
    const qlonglong contentLength = request->headers.value(QByteArrayLiteral("content-length"), "0")
        .toLongLong(&lengthValid);
    if (!lengthValid || contentLength < 0 || contentLength > kMaximumBodyBytes) {
        if (errorMessage) *errorMessage = QStringLiteral("HTTP content length is invalid");
        return false;
    }
    const qsizetype totalBytes = headerEnd + 4 + contentLength;
    if (buffer.size() < totalBytes) {
        *incomplete = true;
        return true;
    }
    request->body = buffer.mid(headerEnd + 4, contentLength);
    *consumed = totalBytes;
    return true;
}

}  // namespace miata::service
