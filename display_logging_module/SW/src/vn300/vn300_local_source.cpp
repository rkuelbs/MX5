#include "vn300/vn300_local_source.h"

namespace miata::data {

Vn300LocalSource::Vn300LocalSource(QObject* parent) : QObject(parent) {
    monotonicClock_.start();
    timestampClock_ = &monotonicClock_;
    reconnectTimer_.setInterval(500);
    connect(&reconnectTimer_, &QTimer::timeout, this, &Vn300LocalSource::connectToServer);
    connect(&socket_, &QLocalSocket::readyRead, this, &Vn300LocalSource::readAvailable);
    connect(&socket_, &QLocalSocket::connected, this, [this] { reconnectTimer_.stop(); });
    connect(&socket_, &QLocalSocket::disconnected, this, [this] {
        if (running_) reconnectTimer_.start();
    });
    connect(&socket_, &QLocalSocket::errorOccurred, this,
            [this](QLocalSocket::LocalSocketError error) {
        if (error != QLocalSocket::ServerNotFoundError
            && error != QLocalSocket::ConnectionRefusedError
            && error != QLocalSocket::PeerClosedError)
            emit sourceError(socket_.errorString());
        if (running_ && socket_.state() == QLocalSocket::UnconnectedState)
            reconnectTimer_.start();
    });
}

void Vn300LocalSource::setTimestampClock(const QElapsedTimer* clock) {
    timestampClock_ = clock ? clock : &monotonicClock_;
}

bool Vn300LocalSource::start(const QString& serverName, QString* errorMessage) {
    stop();
    if (serverName.trimmed().isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("VN300 local server name is required");
        return false;
    }
    serverName_ = serverName;
    parser_.reset();
    running_ = true;
    if (timestampClock_ == &monotonicClock_) monotonicClock_.restart();
    connectToServer();
    return true;
}

void Vn300LocalSource::stop() {
    running_ = false;
    reconnectTimer_.stop();
    socket_.abort();
    serverName_.clear();
}

bool Vn300LocalSource::isConnected() const {
    return socket_.state() == QLocalSocket::ConnectedState;
}

const Vn300BinaryParser& Vn300LocalSource::parser() const { return parser_; }

void Vn300LocalSource::connectToServer() {
    if (!running_ || socket_.state() != QLocalSocket::UnconnectedState) return;
    socket_.connectToServer(serverName_, QIODevice::ReadOnly);
}

void Vn300LocalSource::readAvailable() {
    const auto values = parser_.consume(socket_.readAll(), timestampClock_->nsecsElapsed());
    if (!values.isEmpty()) emit samplesReceived(values);
}

}  // namespace miata::data
