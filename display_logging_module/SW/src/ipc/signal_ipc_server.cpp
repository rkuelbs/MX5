#include "ipc/signal_ipc_server.h"

#include "ipc/signal_ipc_protocol.h"

#include <QLocalSocket>

namespace miata::ipc {
namespace {

constexpr qint64 kMaximumClientBacklogBytes = 2 * 1024 * 1024;

bool isEventSignal(const QString& name) {
    return name.startsWith(QStringLiteral("WCM.event_"));
}

}  // namespace

SignalIpcServer::SignalIpcServer(QObject* parent) : QObject(parent) {
    server_.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&server_, &QLocalServer::newConnection, this, &SignalIpcServer::acceptConnections);
    flushTimer_.setInterval(33);
    connect(&flushTimer_, &QTimer::timeout, this, &SignalIpcServer::flushPendingSamples);
}

void SignalIpcServer::setDefinitions(
    const QList<miata::data::SignalDefinition>& definitions) {
    definitions_ = definitions;
}

bool SignalIpcServer::start(const QString& serverName, QString* errorMessage) {
    stop();
    if (serverName.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("IPC server name cannot be empty");
        return false;
    }
    if (!server_.listen(serverName)) {
        // A crashed Unix process can leave a stale socket path. Never remove a
        // name that still accepts connections from a live logger.
        QLocalSocket probe;
        probe.connectToServer(serverName, QIODevice::ReadOnly);
        if (!probe.waitForConnected(100)) {
            QLocalServer::removeServer(serverName);
            if (server_.listen(serverName)) {
                flushTimer_.start();
                return true;
            }
        }
        if (errorMessage) *errorMessage = server_.errorString();
        return false;
    }
    flushTimer_.start();
    return true;
}

void SignalIpcServer::stop() {
    flushTimer_.stop();
    for (auto* client : std::as_const(clients_)) {
        client->disconnect(this);
        client->disconnectFromServer();
        client->deleteLater();
    }
    clients_.clear();
    pendingLatest_.clear();
    pendingEvents_.clear();
    if (server_.isListening()) server_.close();
}

bool SignalIpcServer::isListening() const { return server_.isListening(); }
QString SignalIpcServer::serverName() const { return server_.serverName(); }
int SignalIpcServer::clientCount() const { return clients_.size(); }

void SignalIpcServer::publishSamples(const QList<miata::data::SignalSample>& samples) {
    for (const auto& sample : samples) {
        if (isEventSignal(sample.canonicalName)) {
            if (!clients_.isEmpty()) pendingEvents_.append(sample);
        } else {
            latestState_.insert(sample.canonicalName, sample);
            if (!clients_.isEmpty()) pendingLatest_.insert(sample.canonicalName, sample);
        }
    }
}

void SignalIpcServer::acceptConnections() {
    while (server_.hasPendingConnections()) {
        auto* socket = server_.nextPendingConnection();
        if (!socket) continue;
        clients_.append(socket);
        connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
            removeClient(socket);
        });
        connect(socket, &QLocalSocket::errorOccurred, this,
                [this](QLocalSocket::LocalSocketError error) {
            if (error != QLocalSocket::PeerClosedError)
                emit serverError(QStringLiteral("Dash IPC client error"));
        });
        send(socket, encodeDefinitions(definitions_));
        if (!latestState_.isEmpty()) send(socket, encodeSamples(latestState_.values()));
        emit clientCountChanged(clients_.size());
    }
}

void SignalIpcServer::flushPendingSamples() {
    if (clients_.isEmpty()) {
        pendingLatest_.clear();
        pendingEvents_.clear();
        return;
    }
    if (pendingLatest_.isEmpty() && pendingEvents_.isEmpty()) return;

    QList<miata::data::SignalSample> batch = pendingEvents_;
    batch.append(pendingLatest_.values());
    pendingEvents_.clear();
    pendingLatest_.clear();
    const QByteArray frame = encodeSamples(batch);
    const auto clients = clients_;
    for (auto* client : clients) send(client, frame);
}

void SignalIpcServer::send(QLocalSocket* socket, const QByteArray& frame) {
    if (!socket || socket->state() != QLocalSocket::ConnectedState) return;
    if (socket->bytesToWrite() > kMaximumClientBacklogBytes) {
        socket->abort();
        return;
    }
    if (socket->write(frame) < 0) emit serverError(socket->errorString());
}

void SignalIpcServer::removeClient(QLocalSocket* socket) {
    if (!clients_.removeOne(socket)) return;
    socket->deleteLater();
    emit clientCountChanged(clients_.size());
}

}  // namespace miata::ipc
