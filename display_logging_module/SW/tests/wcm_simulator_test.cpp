#include "can/dbc_decoder.h"
#include "sim/wcm_simulator.h"

#include <QtTest>

namespace {

QVariantMap decodeValues(miata::data::DbcDecoder& codec,
                         const miata::data::CanFrameRecord& record) {
    QString error;
    const auto samples = codec.decode(record.frame, record.monotonicTimestampNs, &error);
    Q_ASSERT_X(error.isEmpty(), "decodeValues", qPrintable(error));

    QVariantMap values;
    for (const auto& sample : samples)
        values.insert(sample.canonicalName, sample.value);
    return values;
}

}  // namespace

class WcmSimulatorTest final : public QObject {
    Q_OBJECT

private slots:
    void emitsButtonEdgesAndPeriodicState();
    void supportsDroppedEventFault();
    void supportsCounterGapsAndWrap();
};

void WcmSimulatorTest::emitsButtonEdgesAndPeriodicState() {
    miata::data::DbcDecoder codec;
    QString error;
    QVERIFY2(codec.load(QStringLiteral(MIATA_TEST_DBC_PATH), &error), qPrintable(error));

    miata::data::WcmSimulator simulator(&codec);
    QList<miata::data::CanFrameRecord> frames;
    connect(&simulator, &miata::data::WcmSimulator::frameGenerated,
            this, [&frames](const auto& frame) { frames.append(frame); });

    simulator.start();
    simulator.stop();
    frames.clear();

    simulator.setButtonPressed(2, true);
    QCOMPARE(frames.size(), 2);
    QCOMPARE(frames.at(0).frame.frameId(), 0x100U);
    QCOMPARE(frames.at(1).frame.frameId(), 0x101U);

    auto event = decodeValues(codec, frames.at(0));
    QCOMPARE(event.value(QStringLiteral("WCM.event_id")).toInt(), 2);
    QCOMPARE(event.value(QStringLiteral("WCM.event_type")).toInt(), 1);
    QCOMPARE(event.value(QStringLiteral("WCM.event_length")).toInt(), 0);

    const auto status = decodeValues(codec, frames.at(1));
    QCOMPARE(status.value(QStringLiteral("WCM.inputs")).toInt(), 1 << 2);

    QTest::qWait(15);
    frames.clear();
    simulator.setButtonPressed(2, false);
    QCOMPARE(frames.size(), 2);
    event = decodeValues(codec, frames.at(0));
    QCOMPARE(event.value(QStringLiteral("WCM.event_type")).toInt(), 0);
    QVERIFY(event.value(QStringLiteral("WCM.event_length")).toInt() >= 10);
    QCOMPARE(decodeValues(codec, frames.at(1))
                 .value(QStringLiteral("WCM.inputs")).toInt(), 0);
}

void WcmSimulatorTest::supportsDroppedEventFault() {
    miata::data::DbcDecoder codec;
    QString error;
    QVERIFY2(codec.load(QStringLiteral(MIATA_TEST_DBC_PATH), &error), qPrintable(error));

    miata::data::WcmSimulator simulator(&codec);
    QList<miata::data::CanFrameRecord> frames;
    connect(&simulator, &miata::data::WcmSimulator::frameGenerated,
            this, [&frames](const auto& frame) { frames.append(frame); });
    simulator.start();
    simulator.stop();
    frames.clear();

    simulator.setDropEvents(true);
    simulator.setButtonPressed(3, true);
    QCOMPARE(frames.size(), 1);
    QCOMPARE(frames.front().frame.frameId(), 0x101U);
    QCOMPARE(decodeValues(codec, frames.front())
                 .value(QStringLiteral("WCM.inputs")).toInt(), 1 << 3);
}

void WcmSimulatorTest::supportsCounterGapsAndWrap() {
    miata::data::DbcDecoder codec;
    QString error;
    QVERIFY2(codec.load(QStringLiteral(MIATA_TEST_DBC_PATH), &error), qPrintable(error));

    miata::data::WcmSimulator simulator(&codec);
    QList<miata::data::CanFrameRecord> frames;
    connect(&simulator, &miata::data::WcmSimulator::frameGenerated,
            this, [&frames](const auto& frame) { frames.append(frame); });

    simulator.setCounter(254);
    simulator.setCounterStep(2);
    simulator.sendStatusNow();
    simulator.sendStatusNow();
    QCOMPARE(frames.size(), 2);
    QCOMPARE(decodeValues(codec, frames.at(0))
                 .value(QStringLiteral("WCM.counter")).toInt(), 254);
    QCOMPARE(decodeValues(codec, frames.at(1))
                 .value(QStringLiteral("WCM.counter")).toInt(), 0);
}

QTEST_MAIN(WcmSimulatorTest)
#include "wcm_simulator_test.moc"
