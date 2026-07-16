#include "sim/wcm_simulator.h"

#include <algorithm>

namespace miata::data {

WcmSimulator::WcmSimulator(DbcDecoder* codec, QObject* parent)
    : QObject(parent), codec_(codec) {
    pressStartedNs_.fill(-1);
    statusTimer_.setTimerType(Qt::PreciseTimer);
    connect(&statusTimer_, &QTimer::timeout, this, &WcmSimulator::generateStatus);
}

bool WcmSimulator::setStatusRateHz(int rateHz, QString* errorMessage) {
    if (rateHz < 1 || rateHz > 200) {
        if (errorMessage) *errorMessage = QStringLiteral("WCM status rate must be from 1 through 200 Hz");
        return false;
    }
    statusRateHz_ = rateHz;
    if (statusTimer_.isActive()) statusTimer_.setInterval(std::max(1, 1000 / statusRateHz_));
    return true;
}

void WcmSimulator::start() {
    if (statusTimer_.isActive()) return;
    clock_.restart();
    for (int button = 0; button < 8; ++button)
        pressStartedNs_[button] = buttonPressed(button) ? 0 : -1;
    generateStatus();
    statusTimer_.start(std::max(1, 1000 / statusRateHz_));
}

void WcmSimulator::stop() { statusTimer_.stop(); }
bool WcmSimulator::isRunning() const { return statusTimer_.isActive(); }
int WcmSimulator::statusRateHz() const { return statusRateHz_; }
int WcmSimulator::inputs() const { return inputs_; }
int WcmSimulator::counter() const { return counter_; }
int WcmSimulator::boostEncoder() const { return boostEncoder_; }
bool WcmSimulator::paused() const { return paused_; }
bool WcmSimulator::dropStatus() const { return dropStatus_; }
bool WcmSimulator::dropEvents() const { return dropEvents_; }
bool WcmSimulator::freezeCounter() const { return freezeCounter_; }
int WcmSimulator::counterStep() const { return counterStep_; }
int WcmSimulator::statusFramesGenerated() const { return statusFramesGenerated_; }
int WcmSimulator::eventFramesGenerated() const { return eventFramesGenerated_; }
QString WcmSimulator::lastEvent() const { return lastEvent_; }

void WcmSimulator::setButtonPressed(int buttonId, bool pressed) {
    if (buttonId < 0 || buttonId >= 8 || buttonPressed(buttonId) == pressed) return;
    int lengthMs = 0;
    if (pressed) {
        inputs_ |= quint8(1U << buttonId);
        pressStartedNs_[buttonId] = clock_.isValid() ? clock_.nsecsElapsed() : 0;
    } else {
        inputs_ &= quint8(~(1U << buttonId));
        if (clock_.isValid() && pressStartedNs_[buttonId] >= 0) {
            lengthMs = static_cast<int>(std::clamp<qint64>(
                (clock_.nsecsElapsed() - pressStartedNs_[buttonId]) / 1'000'000,
                0, 65'535));
        }
        pressStartedNs_[buttonId] = -1;
    }
    emit stateChanged();
    generateEvent(buttonId, pressed, lengthMs);
    generateStatus();
}

bool WcmSimulator::buttonPressed(int buttonId) const {
    return buttonId >= 0 && buttonId < 8 && (inputs_ & quint8(1U << buttonId)) != 0;
}

void WcmSimulator::releaseAllButtons() {
    for (int button = 0; button < 8; ++button)
        if (buttonPressed(button)) setButtonPressed(button, false);
}

void WcmSimulator::sendStatusNow() { generateStatus(); }

void WcmSimulator::setCounter(int counter) {
    const quint8 bounded = quint8(std::clamp(counter, 0, 255));
    if (bounded == counter_) return;
    counter_ = bounded;
    emit stateChanged();
}

void WcmSimulator::setBoostEncoder(int value) {
    const quint8 bounded = quint8(std::clamp(value, 0, 255));
    if (bounded == boostEncoder_) return;
    boostEncoder_ = bounded;
    emit stateChanged();
    generateStatus();
}

void WcmSimulator::setPaused(bool paused) {
    if (paused == paused_) return;
    paused_ = paused;
    emit stateChanged();
    if (!paused_) generateStatus();
}

void WcmSimulator::setDropStatus(bool drop) {
    if (drop == dropStatus_) return;
    dropStatus_ = drop;
    emit stateChanged();
    if (!dropStatus_) generateStatus();
}

void WcmSimulator::setDropEvents(bool drop) {
    if (drop == dropEvents_) return;
    dropEvents_ = drop;
    emit stateChanged();
}

void WcmSimulator::setFreezeCounter(bool freeze) {
    if (freeze == freezeCounter_) return;
    freezeCounter_ = freeze;
    emit stateChanged();
}

void WcmSimulator::setCounterStep(int step) {
    const int bounded = std::clamp(step, 1, 255);
    if (bounded == counterStep_) return;
    counterStep_ = bounded;
    emit stateChanged();
}

void WcmSimulator::generateEvent(int buttonId, bool pressed, int lengthMs) {
    lastEvent_ = QStringLiteral("%1 %2 (%3 ms)")
        .arg(buttonName(buttonId), pressed ? QStringLiteral("pressed") : QStringLiteral("released"))
        .arg(pressed ? 0 : lengthMs);
    emit statisticsChanged();
    if (paused_ || dropEvents_) return;
    emitEncodedFrame(256, {
        {QStringLiteral("WCM.event_id"), buttonId},
        {QStringLiteral("WCM.event_type"), pressed ? 1 : 0},
        {QStringLiteral("WCM.event_length"), pressed ? 0 : lengthMs},
    }, true);
}

void WcmSimulator::generateStatus() {
    if (paused_ || dropStatus_) return;
    const int transmittedCounter = counter_;
    emitEncodedFrame(257, {
        {QStringLiteral("WCM.inputs"), inputs_},
        {QStringLiteral("WCM.boost_encoder"), boostEncoder_},
        {QStringLiteral("WCM.status"), 0},
        {QStringLiteral("WCM.counter"), transmittedCounter},
    }, false);
    if (!freezeCounter_) counter_ = quint8(counter_ + counterStep_);
    emit stateChanged();
}

void WcmSimulator::emitEncodedFrame(
    quint32 frameId, const QVariantMap& values, bool eventFrame) {
    if (!codec_) {
        emit simulatorError(QStringLiteral("WCM simulator has no DBC codec"));
        return;
    }
    QString error;
    const QCanBusFrame frame = codec_->encodeMessage(frameId, values, &error);
    if (!frame.isValid() || !error.isEmpty()) {
        emit simulatorError(QStringLiteral("Could not encode WCM frame 0x%1: %2")
                                .arg(frameId, 0, 16).arg(error));
        return;
    }
    if (eventFrame) ++eventFramesGenerated_;
    else ++statusFramesGenerated_;
    emit statisticsChanged();
    emit frameGenerated({frame, clock_.isValid() ? clock_.nsecsElapsed() : 0,
                         QStringLiteral("can0")});
}

QString WcmSimulator::buttonName(int buttonId) {
    static const std::array<const char*, 8> names{
        "Down", "Up", "Menu", "Ack", "Left turn", "Right turn", "Wiper", "Flash"};
    return buttonId >= 0 && buttonId < 8
        ? QString::fromLatin1(names[buttonId]) : QStringLiteral("Unknown");
}

}  // namespace miata::data
