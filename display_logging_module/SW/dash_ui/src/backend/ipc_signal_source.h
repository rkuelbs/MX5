#pragma once

#include "backend/signal_data_source.h"

#include <QByteArray>
#include <QHash>
#include <QLocalSocket>
#include <QTimer>

namespace miata::dash {

class IpcSignalSource final : public SignalDataSource {
    Q_OBJECT

public:
    explicit IpcSignalSource(QObject* parent = nullptr);

    void setServerName(const QString& serverName);
    [[nodiscard]] QString serverName() const;
    void start() override;
    void stop() override;

private slots:
    void connectToServer();
    void readAvailable();
    void handleConnected();
    void handleDisconnected();
    void handleError(QLocalSocket::LocalSocketError error);

private:
    void scheduleReconnect();
    void reportErrorOnce(const QString& message);

    QLocalSocket socket_;
    QTimer reconnectTimer_;
    QByteArray receiveBuffer_;
    QHash<QString, QString> unitsByName_;
    QString serverName_;
    QString lastReportedError_;
    bool running_ = false;
    bool connected_ = false;
};

}  // namespace miata::dash
