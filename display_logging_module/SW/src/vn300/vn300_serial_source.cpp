#include "vn300/vn300_serial_source.h"

namespace miata::data {

Vn300SerialSource::Vn300SerialSource(QObject* parent) : QObject(parent) {
    connect(&serial_, &QSerialPort::readyRead, this, &Vn300SerialSource::readAvailable);
    connect(&serial_, &QSerialPort::errorOccurred, this, &Vn300SerialSource::handleError);
    monotonicClock_.start();
    timestampClock_ = &monotonicClock_;
}

void Vn300SerialSource::setTimestampClock(const QElapsedTimer* clock) {
    timestampClock_ = clock != nullptr ? clock : &monotonicClock_;
}

bool Vn300SerialSource::start(const QString& portName, qint32 baudRate, QString* errorMessage) {
    stop();
    if (portName.isEmpty() || baudRate <= 0) {
        if (errorMessage) *errorMessage = QStringLiteral("VN300 port and positive baud rate are required");
        return false;
    }
    parser_.reset();
    serial_.setPortName(portName);
    serial_.setBaudRate(baudRate);
    serial_.setDataBits(QSerialPort::Data8);
    serial_.setParity(QSerialPort::NoParity);
    serial_.setStopBits(QSerialPort::OneStop);
    serial_.setFlowControl(QSerialPort::NoFlowControl);
    if (!serial_.open(QIODevice::ReadOnly)) {
        if (errorMessage) *errorMessage = serial_.errorString();
        return false;
    }
    if (timestampClock_ == &monotonicClock_) monotonicClock_.restart();
    return true;
}

void Vn300SerialSource::stop() { if (serial_.isOpen()) serial_.close(); }
bool Vn300SerialSource::isOpen() const { return serial_.isOpen(); }
const Vn300BinaryParser& Vn300SerialSource::parser() const { return parser_; }

void Vn300SerialSource::readAvailable() {
    const auto samples = parser_.consume(serial_.readAll(), timestampClock_->nsecsElapsed());
    if (!samples.isEmpty()) emit samplesReceived(samples);
}

void Vn300SerialSource::handleError(QSerialPort::SerialPortError error) {
    if (error != QSerialPort::NoError) emit sourceError(serial_.errorString());
}

}  // namespace miata::data
