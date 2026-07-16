#pragma once

#include <QHash>
#include <QHostAddress>
#include <QObject>
#include <QTcpServer>

#include <functional>

class QTcpSocket;
class QFile;

namespace miata::service {

struct HttpRequest {
    QByteArray method;
    QByteArray target;
    QByteArray path;
    QHash<QByteArray, QByteArray> headers;
    QByteArray body;
};

struct HttpResponse {
    int status = 200;
    QByteArray contentType = QByteArrayLiteral("application/json; charset=utf-8");
    QByteArray body;
    QString filePath;
    QByteArray downloadName;

    static HttpResponse json(int status, const QByteArray& body);
    static HttpResponse text(int status, const QByteArray& body);
    static HttpResponse file(
        const QString& path, const QByteArray& downloadName,
        const QByteArray& contentType = QByteArrayLiteral("application/octet-stream"));
};

class SimpleHttpServer final : public QObject {
    Q_OBJECT

public:
    using Handler = std::function<HttpResponse(const HttpRequest&)>;

    explicit SimpleHttpServer(QObject* parent = nullptr);
    void setHandler(Handler handler);
    bool listen(const QHostAddress& address, quint16 port, QString* errorMessage = nullptr);
    void close();
    [[nodiscard]] bool isListening() const;
    [[nodiscard]] quint16 serverPort() const;

signals:
    void serverError(const QString& message);

private:
    void acceptConnections();
    void readClient(QTcpSocket* socket);
    void removeClient(QTcpSocket* socket);
    void respond(QTcpSocket* socket, const HttpResponse& response);
    void writeFileChunk(QTcpSocket* socket);
    static bool parseRequest(
        const QByteArray& buffer, HttpRequest* request, qsizetype* consumed,
        bool* incomplete, QString* errorMessage);

    QTcpServer server_;
    QHash<QTcpSocket*, QByteArray> buffers_;
    QHash<QTcpSocket*, QFile*> downloads_;
    Handler handler_;
};

}  // namespace miata::service
