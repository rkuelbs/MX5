#include "ipc/replay_control.h"

#include <QJsonDocument>

#include <cmath>
#include <utility>

namespace miata::ipc {
namespace {

QJsonObject statusObject(const ReplayControlStatus& status) {
    return {
        {QStringLiteral("version"), kReplayControlProtocolVersion},
        {QStringLiteral("type"), QStringLiteral("status")},
        {QStringLiteral("available"), true},
        {QStringLiteral("source"), status.source},
        {QStringLiteral("state"), status.state},
        {QStringLiteral("position_ns"), double(status.positionNs)},
        {QStringLiteral("duration_ns"), double(status.durationNs)},
        {QStringLiteral("speed_factor"), status.speedFactor},
    };
}

}  // namespace

ReplayControlServer::ReplayControlServer(QObject* parent) : QObject(parent) {
    server_.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&server_, &QLocalServer::newConnection, this, &ReplayControlServer::acceptConnections);
    statusTimer_.setInterval(100);
    connect(&statusTimer_, &QTimer::timeout, this, [this] {
        for (auto* client : std::as_const(clients_)) sendStatus(client);
    });
}

void ReplayControlServer::setStatusProvider(StatusProvider provider) {
    statusProvider_ = std::move(provider);
}

void ReplayControlServer::setCommandHandler(CommandHandler handler) {
    commandHandler_ = std::move(handler);
}

bool ReplayControlServer::start(const QString& serverName, QString* errorMessage) {
    stop();
    if (serverName.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("replay control server name is required");
        return false;
    }
    if (!statusProvider_ || !commandHandler_) {
        if (errorMessage) *errorMessage = QStringLiteral("replay control callbacks are not configured");
        return false;
    }
    if (!server_.listen(serverName)) {
        QLocalSocket probe;
        probe.connectToServer(serverName, QIODevice::ReadOnly);
        if (!probe.waitForConnected(100)) {
            QLocalServer::removeServer(serverName);
            if (server_.listen(serverName)) {
                statusTimer_.start();
                return true;
            }
        }
        if (errorMessage) *errorMessage = server_.errorString();
        return false;
    }
    statusTimer_.start();
    return true;
}

void ReplayControlServer::stop() {
    statusTimer_.stop();
    for (auto* client : std::as_const(clients_)) {
        client->disconnect(this);
        client->disconnectFromServer();
        client->deleteLater();
    }
    clients_.clear();
    buffers_.clear();
    if (server_.isListening()) server_.close();
}

bool ReplayControlServer::isListening() const { return server_.isListening(); }
QString ReplayControlServer::serverName() const { return server_.serverName(); }

void ReplayControlServer::acceptConnections() {
    while (server_.hasPendingConnections()) {
        auto* client = server_.nextPendingConnection();
        if (!client) continue;
        clients_.append(client);
        buffers_.insert(client, {});
        connect(client, &QLocalSocket::readyRead, this, [this, client] { readClient(client); });
        connect(client, &QLocalSocket::disconnected, this, [this, client] { removeClient(client); });
        sendStatus(client);
    }
}

void ReplayControlServer::readClient(QLocalSocket* client) {
    auto found = buffers_.find(client);
    if (found == buffers_.end()) return;
    found.value().append(client->readAll());
    if (found.value().size() > kMaximumReplayControlLineBytes) {
        client->abort();
        return;
    }
    while (true) {
        const qsizetype newline = found.value().indexOf('\n');
        if (newline < 0) break;
        const QByteArray line = found.value().left(newline).trimmed();
        found.value().remove(0, newline + 1);
        const QJsonDocument document = QJsonDocument::fromJson(line);
        const QJsonObject object = document.object();
        QString error;
        bool ok = document.isObject()
            && object.value(QStringLiteral("version")).toInt() == kReplayControlProtocolVersion
            && object.value(QStringLiteral("type")).toString() == QStringLiteral("command");
        if (ok) {
            const QString command = object.value(QStringLiteral("command")).toString();
            ok = !command.isEmpty() && commandHandler_(command, object, &error);
        } else {
            error = QStringLiteral("invalid replay control command");
        }
        send(client, {
            {QStringLiteral("version"), kReplayControlProtocolVersion},
            {QStringLiteral("type"), QStringLiteral("result")},
            {QStringLiteral("ok"), ok},
            {QStringLiteral("error"), error},
        });
        sendStatus(client);
    }
}

void ReplayControlServer::removeClient(QLocalSocket* client) {
    clients_.removeAll(client);
    buffers_.remove(client);
    client->deleteLater();
}

void ReplayControlServer::sendStatus(QLocalSocket* client) {
    if (statusProvider_) send(client, statusObject(statusProvider_()));
}

void ReplayControlServer::send(QLocalSocket* client, const QJsonObject& object) {
    if (!client || client->state() != QLocalSocket::ConnectedState) return;
    if (client->bytesToWrite() > 256 * 1024) {
        client->disconnectFromServer();
        return;
    }
    client->write(QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n');
}

ReplayControlClient::ReplayControlClient(QObject* parent) : QObject(parent) {
    reconnectTimer_.setInterval(500);
    reconnectTimer_.setSingleShot(true);
    connect(&reconnectTimer_, &QTimer::timeout, this, &ReplayControlClient::connectToServer);
    connect(&socket_, &QLocalSocket::connected, this, [this] {
        connected_ = true;
        emit connectedChanged();
    });
    connect(&socket_, &QLocalSocket::disconnected, this, [this] {
        connected_ = false;
        statusAvailable_ = false;
        emit connectedChanged();
        emit statusChanged();
        scheduleReconnect();
    });
    connect(&socket_, &QLocalSocket::readyRead, this, &ReplayControlClient::readAvailable);
    connect(&socket_, &QLocalSocket::errorOccurred, this,
            [this](QLocalSocket::LocalSocketError error) {
        if (running_ && error != QLocalSocket::PeerClosedError) scheduleReconnect();
    });
}

void ReplayControlClient::setServerName(const QString& name) {
    if (!running_) serverName_ = name;
}
QString ReplayControlClient::serverName() const { return serverName_; }
void ReplayControlClient::start() { if (!running_ && !serverName_.isEmpty()) { running_ = true; connectToServer(); } }
void ReplayControlClient::stop() {
    running_ = false;
    reconnectTimer_.stop();
    buffer_.clear();
    socket_.abort();
    if (connected_) { connected_ = false; emit connectedChanged(); }
    if (statusAvailable_) { statusAvailable_ = false; emit statusChanged(); }
}
bool ReplayControlClient::connected() const { return connected_; }
bool ReplayControlClient::available() const { return connected_ && statusAvailable_; }
QString ReplayControlClient::source() const { return status_.source; }
QString ReplayControlClient::state() const { return status_.state; }
double ReplayControlClient::positionMs() const { return double(status_.positionNs) / 1.0e6; }
double ReplayControlClient::durationMs() const { return double(status_.durationNs) / 1.0e6; }
double ReplayControlClient::speedFactor() const { return status_.speedFactor; }
QString ReplayControlClient::lastResult() const { return lastResult_; }
QJsonObject ReplayControlClient::statusJson() const {
    QJsonObject object = statusObject(status_);
    object.insert(QStringLiteral("available"), available());
    object.insert(QStringLiteral("connected"), connected_);
    object.insert(QStringLiteral("last_result"), lastResult_);
    return object;
}

bool ReplayControlClient::play() { return sendCommand(QStringLiteral("play")); }
bool ReplayControlClient::pause() { return sendCommand(QStringLiteral("pause")); }
bool ReplayControlClient::seekMs(double position) {
    if (!std::isfinite(position) || position < 0.0) return false;
    return sendCommand(QStringLiteral("seek"), {{QStringLiteral("position_ns"), position * 1.0e6}});
}
bool ReplayControlClient::setPlaybackSpeed(double factor) {
    if (!std::isfinite(factor) || factor <= 0.0 || factor > 100.0) return false;
    return sendCommand(QStringLiteral("speed"), {{QStringLiteral("factor"), factor}});
}

bool ReplayControlClient::sendCommand(const QString& command, const QJsonObject& parameters) {
    if (!connected_) {
        lastResult_ = QStringLiteral("Replay control is not connected");
        emit lastResultChanged();
        return false;
    }
    QJsonObject object = parameters;
    object.insert(QStringLiteral("version"), kReplayControlProtocolVersion);
    object.insert(QStringLiteral("type"), QStringLiteral("command"));
    object.insert(QStringLiteral("command"), command);
    socket_.write(QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n');
    return true;
}

void ReplayControlClient::connectToServer() {
    if (running_ && socket_.state() == QLocalSocket::UnconnectedState)
        socket_.connectToServer(serverName_, QIODevice::ReadWrite);
}

void ReplayControlClient::readAvailable() {
    buffer_.append(socket_.readAll());
    if (buffer_.size() > kMaximumReplayControlLineBytes) { socket_.abort(); return; }
    while (true) {
        const qsizetype newline = buffer_.indexOf('\n');
        if (newline < 0) break;
        const QJsonDocument document = QJsonDocument::fromJson(buffer_.left(newline).trimmed());
        buffer_.remove(0, newline + 1);
        if (!document.isObject()) continue;
        const QJsonObject object = document.object();
        if (object.value(QStringLiteral("version")).toInt() != kReplayControlProtocolVersion) continue;
        const QString type = object.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("status")) {
            status_.source = object.value(QStringLiteral("source")).toString();
            status_.state = object.value(QStringLiteral("state")).toString();
            status_.positionNs = qint64(object.value(QStringLiteral("position_ns")).toDouble());
            status_.durationNs = qint64(object.value(QStringLiteral("duration_ns")).toDouble());
            status_.speedFactor = object.value(QStringLiteral("speed_factor")).toDouble(1.0);
            statusAvailable_ = object.value(QStringLiteral("available")).toBool();
            emit statusChanged();
        } else if (type == QStringLiteral("result")) {
            lastResult_ = object.value(QStringLiteral("ok")).toBool()
                ? QStringLiteral("Command accepted")
                : object.value(QStringLiteral("error")).toString(QStringLiteral("Command failed"));
            emit lastResultChanged();
        }
    }
}

void ReplayControlClient::scheduleReconnect() {
    if (running_ && !reconnectTimer_.isActive()) reconnectTimer_.start();
}

}  // namespace miata::ipc
