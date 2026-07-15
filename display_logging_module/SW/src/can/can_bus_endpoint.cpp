#include "can/can_bus_endpoint.h"

#include <QCanBus>

namespace miata::data {

CanBusEndpoint::CanBusEndpoint(QObject* parent) : QObject(parent) {
    monotonicClock_.start();
    timestampClock_ = &monotonicClock_;
}

void CanBusEndpoint::setTimestampClock(const QElapsedTimer* clock) {
    timestampClock_ = clock != nullptr ? clock : &monotonicClock_;
}

bool CanBusEndpoint::start(
    const QString& pluginName,
    const QString& interfaceName,
    QString* errorMessage) {
    stop();

    QString createError;
    device_.reset(QCanBus::instance()->createDevice(pluginName, interfaceName, &createError));
    if (!device_) {
        if (errorMessage != nullptr) {
            *errorMessage = createError.isEmpty()
                ? QStringLiteral("CAN plugin '%1' is unavailable").arg(pluginName)
                : createError;
        }
        return false;
    }

    interfaceName_ = interfaceName;
    if (timestampClock_ == &monotonicClock_) monotonicClock_.restart();
    connect(device_.get(), &QCanBusDevice::framesReceived, this, &CanBusEndpoint::readFrames);
    connect(device_.get(), &QCanBusDevice::errorOccurred, this, &CanBusEndpoint::handleError);

    if (!device_->connectDevice()) {
        if (errorMessage != nullptr) {
            *errorMessage = device_->errorString();
        }
        device_.reset();
        return false;
    }

    return true;
}

void CanBusEndpoint::stop() {
    if (device_) {
        if (device_->state() != QCanBusDevice::UnconnectedState) {
            device_->disconnectDevice();
        }
        device_.reset();
    }
}

bool CanBusEndpoint::isConnected() const {
    return device_ && device_->state() == QCanBusDevice::ConnectedState;
}

bool CanBusEndpoint::writeFrame(const QCanBusFrame& frame, QString* errorMessage) {
    if (!isConnected()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("CAN endpoint is not connected");
        }
        return false;
    }
    if (!device_->writeFrame(frame)) {
        if (errorMessage != nullptr) {
            *errorMessage = device_->errorString();
        }
        return false;
    }
    return true;
}

void CanBusEndpoint::readFrames() {
    if (!device_) {
        return;
    }
    const QList<QCanBusFrame> frames = device_->readAllFrames();
    for (const QCanBusFrame& frame : frames) {
        emit frameReceived(CanFrameRecord{frame, timestampClock_->nsecsElapsed(), interfaceName_});
    }
}

void CanBusEndpoint::handleError(QCanBusDevice::CanBusError error) {
    if (error != QCanBusDevice::NoError && device_) {
        emit endpointError(device_->errorString());
    }
}

}  // namespace miata::data
