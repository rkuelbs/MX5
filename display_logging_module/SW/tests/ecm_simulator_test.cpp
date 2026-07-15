#include "can/dbc_decoder.h"
#include "sim/ecm_simulator.h"

#include <QSignalSpy>
#include <QTemporaryFile>
#include <QtTest>

class EcmSimulatorTest final : public QObject {
    Q_OBJECT

private slots:
    void appliesManualSignalValue();
    void appliesDropAndDlcFaults();
    void animatesUnlockedSignalsAndKeepsLocks();
};

void EcmSimulatorTest::appliesManualSignalValue() {
    miata::data::DbcDecoder codec;
    QString error;
    QVERIFY2(codec.load(QStringLiteral(MIATA_TEST_DBC_PATH), &error), qPrintable(error));

    QTemporaryFile scenario;
    QVERIFY(scenario.open());
    scenario.write(R"JSON({
        "rate_hz": 100,
        "automatic_values": false,
        "signals": {"ECM.rpm": 4321},
        "faults": {"paused": false, "drop_frames": [], "dlc_overrides": {}}
    })JSON");
    QVERIFY(scenario.flush());

    qRegisterMetaType<miata::data::CanFrameRecord>();
    miata::data::EcmSimulator simulator(&codec);
    QVERIFY2(simulator.loadScenario(scenario.fileName(), &error), qPrintable(error));
    QSignalSpy frames(&simulator, &miata::data::EcmSimulator::frameGenerated);
    simulator.start();
    QTRY_VERIFY_WITH_TIMEOUT(frames.count() >= 5, 1000);
    simulator.stop();

    bool foundRpm = false;
    for (const QList<QVariant>& arguments : frames) {
        const auto record = qvariant_cast<miata::data::CanFrameRecord>(arguments.at(0));
        if (record.frame.frameId() != 0x5F0U) {
            continue;
        }
        const auto samples = codec.decode(record.frame, record.monotonicTimestampNs, &error);
        for (const auto& sample : samples) {
            if (sample.canonicalName == QStringLiteral("ECM.rpm")) {
                QCOMPARE(sample.value.toDouble(), 4321.0);
                foundRpm = true;
            }
        }
    }
    QVERIFY(foundRpm);
}

void EcmSimulatorTest::appliesDropAndDlcFaults() {
    miata::data::DbcDecoder codec;
    QString error;
    QVERIFY2(codec.load(QStringLiteral(MIATA_TEST_DBC_PATH), &error), qPrintable(error));

    QTemporaryFile scenario;
    QVERIFY(scenario.open());
    scenario.write(R"JSON({
        "rate_hz": 100,
        "automatic_values": false,
        "signals": {"ECM.rpm": 3000},
        "faults": {
            "paused": false,
            "drop_frames": ["0x5F0"],
            "dlc_overrides": {"0x5F2": 4}
        }
    })JSON");
    QVERIFY(scenario.flush());

    qRegisterMetaType<miata::data::CanFrameRecord>();
    miata::data::EcmSimulator simulator(&codec);
    QVERIFY2(simulator.loadScenario(scenario.fileName(), &error), qPrintable(error));
    QSignalSpy frames(&simulator, &miata::data::EcmSimulator::frameGenerated);
    QSignalSpy simulatorErrors(&simulator, &miata::data::EcmSimulator::simulatorError);
    simulator.start();
    QTRY_VERIFY_WITH_TIMEOUT(frames.count() >= 5, 1000);
    simulator.stop();
    QCOMPARE(simulatorErrors.count(), 0);

    bool foundDlcFault = false;
    for (const QList<QVariant>& arguments : frames) {
        const auto record = qvariant_cast<miata::data::CanFrameRecord>(arguments.at(0));
        QVERIFY(record.frame.frameId() != 0x5F0U);
        if (record.frame.frameId() == 0x5F2U) {
            QCOMPARE(record.frame.payload().size(), 4);
            foundDlcFault = true;
        }
    }
    QVERIFY(foundDlcFault);
}

void EcmSimulatorTest::animatesUnlockedSignalsAndKeepsLocks() {
    miata::data::DbcDecoder codec;
    QString error;
    QVERIFY2(codec.load(QStringLiteral(MIATA_TEST_DBC_PATH), &error), qPrintable(error));
    QTemporaryFile scenario;
    QVERIFY(scenario.open());
    scenario.write(R"JSON({
        "rate_hz": 100,
        "automatic_values": true,
        "signals": {"ECM.rpm": 4321},
        "faults": {"paused": false, "drop_frames": [], "dlc_overrides": {}}
    })JSON");
    QVERIFY(scenario.flush());

    qRegisterMetaType<miata::data::CanFrameRecord>();
    miata::data::EcmSimulator simulator(&codec);
    QVERIFY2(simulator.loadScenario(scenario.fileName(), &error), qPrintable(error));
    QSignalSpy frames(&simulator, &miata::data::EcmSimulator::frameGenerated);
    QSignalSpy simulatorErrors(&simulator, &miata::data::EcmSimulator::simulatorError);
    simulator.start();
    QTRY_VERIFY_WITH_TIMEOUT(frames.count() >= 150, 2000);
    simulator.stop();
    QCOMPARE(simulatorErrors.count(), 0);

    QList<double> tpsValues;
    for (const auto& invocation : frames) {
        const auto record = qvariant_cast<miata::data::CanFrameRecord>(invocation.at(0));
        const auto samples = codec.decode(record.frame, record.monotonicTimestampNs, &error);
        for (const auto& sample : samples) {
            if (sample.canonicalName == QStringLiteral("ECM.rpm"))
                QCOMPARE(sample.value.toDouble(), 4321.0);
            if (sample.canonicalName == QStringLiteral("ECM.tps"))
                tpsValues.append(sample.value.toDouble());
        }
    }
    QVERIFY(tpsValues.size() >= 2);
    QVERIFY(tpsValues.front() != tpsValues.back());
}

QTEST_MAIN(EcmSimulatorTest)
#include "ecm_simulator_test.moc"
