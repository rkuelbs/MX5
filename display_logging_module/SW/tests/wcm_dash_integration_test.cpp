#include "backend/dash_input_controller.h"
#include "can/dbc_decoder.h"
#include "sim/wcm_simulator.h"

#include <QSignalSpy>
#include <QtTest>

class WcmDashIntegrationTest final : public QObject {
    Q_OBJECT

private slots:
    void simulatorButtonDrivesDashActionOnce();
};

void WcmDashIntegrationTest::simulatorButtonDrivesDashActionOnce() {
    miata::data::DbcDecoder codec;
    QString error;
    QVERIFY2(codec.load(QStringLiteral(MIATA_TEST_DBC_PATH), &error), qPrintable(error));

    miata::data::WcmSimulator simulator(&codec);
    miata::dash::DashInputController controller;
    QSignalSpy actions(&controller, &miata::dash::DashInputController::actionTriggered);
    QSignalSpy releases(&controller, &miata::dash::DashInputController::buttonReleased);

    connect(&simulator, &miata::data::WcmSimulator::frameGenerated,
            this, [&](const miata::data::CanFrameRecord& record) {
        QString decodeError;
        const auto samples = codec.decode(record.frame, record.monotonicTimestampNs, &decodeError);
        QVERIFY2(decodeError.isEmpty(), qPrintable(decodeError));
        controller.processSamples(samples);
    });

    // The initial periodic state establishes the fallback bitmask without
    // generating a navigation action.
    simulator.start();
    simulator.stop();
    QCOMPARE(actions.size(), 0);

    simulator.setButtonPressed(2, true);
    QCOMPARE(actions.size(), 1);
    QCOMPARE(qvariant_cast<miata::dash::DashAction>(actions.front().front()),
             miata::dash::DashAction::MenuBack);

    // The immediate status confirmation must not duplicate the edge action.
    QCOMPARE(actions.size(), 1);
    QTest::qWait(10);
    simulator.setButtonPressed(2, false);
    QCOMPARE(actions.size(), 1);
    QCOMPARE(releases.size(), 1);
    QCOMPARE(releases.front().at(0).toInt(), 2);
    QVERIFY(releases.front().at(1).toInt() >= 5);

    // PDM-owned WCM inputs still travel through the DBC but never navigate UI.
    simulator.setButtonPressed(4, true);
    QCOMPARE(actions.size(), 1);
}

QTEST_MAIN(WcmDashIntegrationTest)
#include "wcm_dash_integration_test.moc"
