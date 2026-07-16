#include "backend/ipc_signal_source.h"

#include "ipc/signal_ipc_protocol.h"

namespace miata::dash {

IpcSignalSource::IpcSignalSource(QObject* parent)
    : SignalDataSource(parent), serverName_(miata::ipc::kDefaultSignalIpcServerName) {
    reconnectTimer_.setInterval(500);
    reconnectTimer_.setSingleShot(true);
    connect(&reconnectTimer_, &QTimer::timeout, this, &IpcSignalSource::connectToServer);
    connect(&socket_, &QLocalSocket::connected, this, &IpcSignalSource::handleConnected);
    connect(&socket_, &QLocalSocket::disconnected, this, &IpcSignalSource::handleDisconnected);
    connect(&socket_, &QLocalSocket::readyRead, this, &IpcSignalSource::readAvailable);
    connect(&socket_, &QLocalSocket::errorOccurred, this, &IpcSignalSource::handleError);
}

void IpcSignalSource::setServerName(const QString& serverName) {
    if (!running_ && !serverName.isEmpty()) serverName_ = serverName;
}

QString IpcSignalSource::serverName() const { return serverName_; }

void IpcSignalSource::start() {
    if (running_) return;
    running_ = true;
    connectToServer();
}

void IpcSignalSource::stop() {
    running_ = false;
    reconnectTimer_.stop();
    receiveBuffer_.clear();
    unitsByName_.clear();
    socket_.abort();
    if (connected_) {
        connected_ = false;
        emit connectedChanged(false);
    }
}

void IpcSignalSource::connectToServer() {
    if (!running_ || socket_.state() != QLocalSocket::UnconnectedState) return;
    socket_.connectToServer(serverName_, QIODevice::ReadOnly);
}

void IpcSignalSource::readAvailable() {
    receiveBuffer_.append(socket_.readAll());
    QList<miata::ipc::SignalIpcMessage> messages;
    QString error;
    if (!miata::ipc::decodeAvailableMessages(&receiveBuffer_, &messages, &error)) {
        reportErrorOnce(QStringLiteral("Logger IPC protocol error: %1").arg(error));
        receiveBuffer_.clear();
        socket_.abort();
        return;
    }

    for (auto& message : messages) {
        if (message.type == miata::ipc::SignalIpcMessageType::Definitions) {
            unitsByName_.clear();
            for (const auto& definition : message.definitions)
                unitsByName_.insert(definition.canonicalName, definition.unit);
            emit definitionsAvailable(message.definitions);
        } else if (message.type == miata::ipc::SignalIpcMessageType::Samples) {
            for (auto& sample : message.samples)
                sample.unit = unitsByName_.value(sample.canonicalName);
            emit samplesAvailable(message.samples);
        }
    }
}

void IpcSignalSource::handleConnected() {
    reconnectTimer_.stop();
    receiveBuffer_.clear();
    lastReportedError_.clear();
    if (!connected_) {
        connected_ = true;
        emit connectedChanged(true);
    }
}

void IpcSignalSource::handleDisconnected() {
    receiveBuffer_.clear();
    if (connected_) {
        connected_ = false;
        emit connectedChanged(false);
    }
    scheduleReconnect();
}

void IpcSignalSource::handleError(QLocalSocket::LocalSocketError error) {
    if (!running_ || error == QLocalSocket::PeerClosedError) return;
    reportErrorOnce(QStringLiteral("Logger IPC unavailable: %1").arg(socket_.errorString()));
    scheduleReconnect();
}

void IpcSignalSource::scheduleReconnect() {
    if (running_ && !reconnectTimer_.isActive()) reconnectTimer_.start();
}

void IpcSignalSource::reportErrorOnce(const QString& message) {
    if (message == lastReportedError_) return;
    lastReportedError_ = message;
    emit sourceError(message);
}

}  // namespace miata::dash
