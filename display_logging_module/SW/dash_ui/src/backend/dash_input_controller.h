#pragma once

#include "backend/dash_action.h"
#include "core/signal_sample.h"

#include <QObject>

#include <array>

namespace miata::dash {

class DashInputController final : public QObject {
    Q_OBJECT

public:
    explicit DashInputController(QObject* parent = nullptr);

    // Returns true when the key belongs to the four-action dash input set.
    Q_INVOKABLE bool handleKey(int key, bool autoRepeat = false);

public slots:
    void processSamples(const QList<miata::data::SignalSample>& samples);

signals:
    void actionTriggered(miata::dash::DashAction action);
    void buttonReleased(int buttonId, int pressLengthMs);

private:
    void processStatus(quint8 inputs);
    void processEvent(int buttonId, bool pressed, int pressLengthMs, qint64 timestampNs);
    void updateButton(int buttonId, bool pressed, bool allowAction);
    void triggerForButton(int buttonId);

    std::array<bool, 8> buttonKnown_{};
    std::array<bool, 8> buttonPressed_{};
    bool statusInitialized_ = false;
    qint64 lastEventTimestampNs_ = -1;
};

}  // namespace miata::dash
