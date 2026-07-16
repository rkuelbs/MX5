#include "backend/dash_input_controller.h"

#include <QSignalSpy>
#include <QtTest>

using miata::dash::DashAction;
using miata::dash::DashInputController;
using miata::data::SignalSample;
using miata::data::SignalSource;

namespace {

SignalSample sample(const QString& name, double value, qint64 timestamp) {
    return {name, value, {}, timestamp, SignalSource::Can};
}

QList<SignalSample> eventSamples(int id, int type, int lengthMs, qint64 timestamp) {
    return {
        sample(QStringLiteral("WCM.event_id"), id, timestamp),
        sample(QStringLiteral("WCM.event_type"), type, timestamp),
        sample(QStringLiteral("WCM.event_length"), lengthMs, timestamp),
    };
}

}  // namespace

class DashInputControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void mapsKeyboardActionsAndRepeatRules();
    void deduplicatesEventAndStatusTransitions();
    void usesStatusAsFallbackWithoutActingOnInitialState();
};

void DashInputControllerTest::mapsKeyboardActionsAndRepeatRules() {
    DashInputController controller;
    QSignalSpy actions(&controller, &DashInputController::actionTriggered);

    QVERIFY(controller.handleKey(Qt::Key_Up));
    QVERIFY(controller.handleKey(Qt::Key_Down, true));
    QVERIFY(controller.handleKey(Qt::Key_Return));
    QVERIFY(controller.handleKey(Qt::Key_Return, true));
    QVERIFY(controller.handleKey(Qt::Key_M));
    QVERIFY(!controller.handleKey(Qt::Key_A));

    QCOMPARE(actions.size(), 4);
    QCOMPARE(qvariant_cast<DashAction>(actions.at(0).at(0)), DashAction::NavigateUp);
    QCOMPARE(qvariant_cast<DashAction>(actions.at(1).at(0)), DashAction::NavigateDown);
    QCOMPARE(qvariant_cast<DashAction>(actions.at(2).at(0)), DashAction::Activate);
    QCOMPARE(qvariant_cast<DashAction>(actions.at(3).at(0)), DashAction::MenuBack);
}

void DashInputControllerTest::deduplicatesEventAndStatusTransitions() {
    DashInputController controller;
    QSignalSpy actions(&controller, &DashInputController::actionTriggered);
    QSignalSpy releases(&controller, &DashInputController::buttonReleased);

    controller.processSamples({sample(QStringLiteral("WCM.inputs"), 0, 1)});
    controller.processSamples(eventSamples(1, 1, 0, 100));
    QCOMPARE(actions.size(), 1);
    QCOMPARE(qvariant_cast<DashAction>(actions.at(0).at(0)), DashAction::NavigateUp);

    // Periodic status confirms the event state without duplicating the action.
    controller.processSamples({sample(QStringLiteral("WCM.inputs"), 0x02, 101)});
    QCOMPARE(actions.size(), 1);

    // Status may report release before the falling event supplies its duration.
    controller.processSamples({sample(QStringLiteral("WCM.inputs"), 0, 150)});
    controller.processSamples(eventSamples(1, 0, 347, 200));
    QCOMPARE(releases.size(), 1);
    QCOMPARE(releases.at(0).at(0).toInt(), 1);
    QCOMPARE(releases.at(0).at(1).toInt(), 347);

    // Replaying the same atomic event timestamp is ignored.
    controller.processSamples(eventSamples(1, 0, 347, 200));
    QCOMPARE(releases.size(), 1);

    // PDM-owned buttons never become dash actions.
    controller.processSamples(eventSamples(4, 1, 0, 300));
    QCOMPARE(actions.size(), 1);

    // Multiple event frames may be delivered in one display-rate IPC batch.
    auto combinedEvents = eventSamples(2, 1, 0, 400);
    combinedEvents.append(eventSamples(2, 0, 125, 500));
    controller.processSamples(combinedEvents);
    QCOMPARE(actions.size(), 2);
    QCOMPARE(qvariant_cast<DashAction>(actions.at(1).at(0)), DashAction::MenuBack);
    QCOMPARE(releases.size(), 2);
    QCOMPARE(releases.at(1).at(1).toInt(), 125);
}

void DashInputControllerTest::usesStatusAsFallbackWithoutActingOnInitialState() {
    DashInputController controller;
    QSignalSpy actions(&controller, &DashInputController::actionTriggered);

    controller.processSamples({sample(QStringLiteral("WCM.inputs"), 0x01, 1)});
    QCOMPARE(actions.size(), 0);

    controller.processSamples({sample(QStringLiteral("WCM.inputs"), 0, 2)});
    controller.processSamples({sample(QStringLiteral("WCM.inputs"), 0x01, 3)});
    QCOMPARE(actions.size(), 1);
    QCOMPARE(qvariant_cast<DashAction>(actions.at(0).at(0)), DashAction::NavigateDown);
}

QTEST_MAIN(DashInputControllerTest)
#include "dash_input_controller_test.moc"
