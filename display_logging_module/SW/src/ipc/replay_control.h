#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QTimer>

#include <functional>

namespace miata::ipc {

inline constexpr int kReplayControlProtocolVersion = 1;
inline constexpr qsizetype kMaximumReplayControlLineBytes = 16 * 1024;

struct ReplayControlStatus {
    QString source;
    QString state;
    qint64 positionNs = 0;
    qint64 durationNs = 0;
    double speedFactor = 1.0;
};

class ReplayControlServer final : public QObject {
    Q_OBJECT

public:
    using StatusProvider = std::function<ReplayControlStatus()>;
    using CommandHandler = std::function<bool(const QString&, const QJsonObject&, QString*)>;

    explicit ReplayControlServer(QObject* parent = nullptr);
    void setStatusProvider(StatusProvider provider);
    void setCommandHandler(CommandHandler handler);
    bool start(const QString& serverName, QString* errorMessage = nullptr);
    void stop();
    [[nodiscard]] bool isListening() const;
    [[nodiscard]] QString serverName() const;

signals:
    void serverError(const QString& message);

private:
    void acceptConnections();
    void readClient(QLocalSocket* client);
    void removeClient(QLocalSocket* client);
    void sendStatus(QLocalSocket* client);
    void send(QLocalSocket* client, const QJsonObject& object);

    QLocalServer server_;
    QList<QLocalSocket*> clients_;
    QHash<QLocalSocket*, QByteArray> buffers_;
    QTimer statusTimer_;
    StatusProvider statusProvider_;
    CommandHandler commandHandler_;
};

class ReplayControlClient final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(bool available READ available NOTIFY statusChanged)
    Q_PROPERTY(QString source READ source NOTIFY statusChanged)
    Q_PROPERTY(QString state READ state NOTIFY statusChanged)
    Q_PROPERTY(double positionMs READ positionMs NOTIFY statusChanged)
    Q_PROPERTY(double durationMs READ durationMs NOTIFY statusChanged)
    Q_PROPERTY(double speedFactor READ speedFactor NOTIFY statusChanged)
    Q_PROPERTY(QString lastResult READ lastResult NOTIFY lastResultChanged)

public:
    explicit ReplayControlClient(QObject* parent = nullptr);
    void setServerName(const QString& serverName);
    [[nodiscard]] QString serverName() const;
    void start();
    void stop();
    [[nodiscard]] bool connected() const;
    [[nodiscard]] bool available() const;
    [[nodiscard]] QString source() const;
    [[nodiscard]] QString state() const;
    [[nodiscard]] double positionMs() const;
    [[nodiscard]] double durationMs() const;
    [[nodiscard]] double speedFactor() const;
    [[nodiscard]] QString lastResult() const;
    [[nodiscard]] QJsonObject statusJson() const;

    Q_INVOKABLE bool play();
    Q_INVOKABLE bool pause();
    Q_INVOKABLE bool seekMs(double positionMs);
    Q_INVOKABLE bool setPlaybackSpeed(double factor);

signals:
    void connectedChanged();
    void statusChanged();
    void lastResultChanged();

private:
    bool sendCommand(const QString& command, const QJsonObject& parameters = {});
    void connectToServer();
    void readAvailable();
    void scheduleReconnect();

    QLocalSocket socket_;
    QTimer reconnectTimer_;
    QByteArray buffer_;
    QString serverName_;
    ReplayControlStatus status_;
    QString lastResult_;
    bool running_ = false;
    bool connected_ = false;
    bool statusAvailable_ = false;
};

}  // namespace miata::ipc
