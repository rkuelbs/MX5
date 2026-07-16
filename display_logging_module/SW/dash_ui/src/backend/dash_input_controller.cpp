#include "backend/dash_input_controller.h"

#include <Qt>

#include <QMap>

#include <optional>

namespace miata::dash {
namespace {

struct EventFields {
    std::optional<int> id;
    std::optional<int> type;
    std::optional<int> lengthMs;
    qint64 idTimestampNs = -1;
    qint64 typeTimestampNs = -1;
    qint64 lengthTimestampNs = -1;
};

}  // namespace

DashInputController::DashInputController(QObject* parent) : QObject(parent) {
    qRegisterMetaType<DashAction>();
}

bool DashInputController::handleKey(int key, bool autoRepeat) {
    std::optional<DashAction> action;
    switch (key) {
    case Qt::Key_Up:
        action = DashAction::NavigateUp;
        break;
    case Qt::Key_Down:
        action = DashAction::NavigateDown;
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (!autoRepeat) action = DashAction::Activate;
        break;
    case Qt::Key_Escape:
    case Qt::Key_M:
        if (!autoRepeat) action = DashAction::MenuBack;
        break;
    default:
        return false;
    }

    if (action) emit actionTriggered(*action);
    return true;
}

void DashInputController::processSamples(const QList<miata::data::SignalSample>& samples) {
    QMap<qint64, EventFields> eventsByTimestamp;
    std::optional<quint8> inputs;

    for (const auto& sample : samples) {
        if (sample.canonicalName == QStringLiteral("WCM.inputs")) {
            inputs = static_cast<quint8>(sample.value.toUInt());
        } else if (sample.canonicalName == QStringLiteral("WCM.event_id")) {
            auto& event = eventsByTimestamp[sample.monotonicTimestampNs];
            event.id = sample.value.toInt();
            event.idTimestampNs = sample.monotonicTimestampNs;
        } else if (sample.canonicalName == QStringLiteral("WCM.event_type")) {
            auto& event = eventsByTimestamp[sample.monotonicTimestampNs];
            event.type = sample.value.toInt();
            event.typeTimestampNs = sample.monotonicTimestampNs;
        } else if (sample.canonicalName == QStringLiteral("WCM.event_length")) {
            auto& event = eventsByTimestamp[sample.monotonicTimestampNs];
            event.lengthMs = sample.value.toInt();
            event.lengthTimestampNs = sample.monotonicTimestampNs;
        }
    }

    for (auto iterator = eventsByTimestamp.cbegin(); iterator != eventsByTimestamp.cend(); ++iterator) {
        const auto& event = iterator.value();
        const bool completeEvent = event.id && event.type && event.lengthMs
            && event.idTimestampNs == event.typeTimestampNs
            && event.idTimestampNs == event.lengthTimestampNs;
        if (completeEvent && event.idTimestampNs > lastEventTimestampNs_
            && *event.id >= 0 && *event.id < 8 && (*event.type == 0 || *event.type == 1)) {
            processEvent(*event.id, *event.type == 1, *event.lengthMs, event.idTimestampNs);
        }
    }

    // Processing the edge first makes a same-batch status frame a state
    // confirmation rather than a duplicate action.
    if (inputs) processStatus(*inputs);
}

void DashInputController::processStatus(quint8 inputs) {
    if (!statusInitialized_) {
        statusInitialized_ = true;
        for (int button = 0; button < 8; ++button) {
            if (!buttonKnown_[button]) {
                buttonKnown_[button] = true;
                buttonPressed_[button] = (inputs & (1U << button)) != 0;
            } else {
                updateButton(button, (inputs & (1U << button)) != 0, true);
            }
        }
        return;
    }

    for (int button = 0; button < 8; ++button)
        updateButton(button, (inputs & (1U << button)) != 0, true);
}

void DashInputController::processEvent(
    int buttonId, bool pressed, int pressLengthMs, qint64 timestampNs) {
    lastEventTimestampNs_ = timestampNs;
    updateButton(buttonId, pressed, true);
    if (!pressed) emit buttonReleased(buttonId, pressLengthMs);
}

void DashInputController::updateButton(int buttonId, bool pressed, bool allowAction) {
    if (buttonId < 0 || buttonId >= 8) return;
    const bool changed = !buttonKnown_[buttonId] || buttonPressed_[buttonId] != pressed;
    buttonKnown_[buttonId] = true;
    buttonPressed_[buttonId] = pressed;
    if (changed && pressed && allowAction) triggerForButton(buttonId);
}

void DashInputController::triggerForButton(int buttonId) {
    switch (buttonId) {
    case 0:
        emit actionTriggered(DashAction::NavigateDown);
        break;
    case 1:
        emit actionTriggered(DashAction::NavigateUp);
        break;
    case 2:
        emit actionTriggered(DashAction::MenuBack);
        break;
    case 3:
        emit actionTriggered(DashAction::Activate);
        break;
    default:
        break;  // Buttons 4-7 are owned by the PDM, not dash navigation.
    }
}

}  // namespace miata::dash
