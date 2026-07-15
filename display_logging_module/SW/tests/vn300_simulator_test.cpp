#include "sim/vn300_simulator.h"
#include "vn300/vn300_binary_parser.h"
#include "vn300/vn300_log_codec.h"
#include "vn300/vn300_replay_source.h"

#include <QSignalSpy>
#include <QTemporaryFile>
#include <QTextStream>
#include <QtTest>

class Vn300SimulatorTest final : public QObject {
    Q_OBJECT

private slots:
    void drivesInCircleAndHonorsSignalLock();
    void recordsAndReplaysPackets();
};

namespace {
const miata::data::SignalSample* find(
    const QList<miata::data::SignalSample>& samples,
    const QString& name) {
    for (const auto& sample : samples) if (sample.canonicalName == name) return &sample;
    return nullptr;
}

QByteArray scenarioJson() {
    return R"JSON({
      "rate_hz": 100,
      "circle": {"radius_m": 50, "speed_mps": 12,
                 "origin_latitude_deg": 39, "origin_longitude_deg": -96,
                 "altitude_m": 300},
      "signals": {"VN300.lin_body_accel_y": 7.5},
      "faults": {"paused": false, "corrupt_crc_every": 0, "drop_every": 0}
    })JSON";
}
}

void Vn300SimulatorTest::drivesInCircleAndHonorsSignalLock() {
    QTemporaryFile scenario;
    QVERIFY(scenario.open());
    scenario.write(scenarioJson());
    QVERIFY(scenario.flush());

    qRegisterMetaType<miata::data::Vn300PacketRecord>();
    miata::data::Vn300Simulator simulator;
    QString error;
    QVERIFY2(simulator.loadScenario(scenario.fileName(), &error), qPrintable(error));
    QSignalSpy packets(&simulator, &miata::data::Vn300Simulator::packetGenerated);
    simulator.start();
    QTRY_VERIFY_WITH_TIMEOUT(packets.count() >= 3, 1000);
    simulator.stop();

    miata::data::Vn300BinaryParser parser;
    const auto firstRecord = qvariant_cast<miata::data::Vn300PacketRecord>(packets.at(0).at(0));
    const auto lastRecord = qvariant_cast<miata::data::Vn300PacketRecord>(packets.at(2).at(0));
    const auto first = parser.consume(firstRecord.packet, firstRecord.monotonicTimestampNs);
    const auto last = parser.consume(lastRecord.packet, lastRecord.monotonicTimestampNs);
    QCOMPARE(first.size(), 46);
    QCOMPARE(last.size(), 46);
    QVERIFY(find(last, QStringLiteral("VN300.time_startup"))->value.toDouble()
            > find(first, QStringLiteral("VN300.time_startup"))->value.toDouble());
    QVERIFY(find(last, QStringLiteral("VN300.latitude"))->value.toDouble()
            != find(first, QStringLiteral("VN300.latitude"))->value.toDouble());
    QCOMPARE(find(last, QStringLiteral("VN300.lin_body_accel_y"))->value.toDouble(), 7.5);
}

void Vn300SimulatorTest::recordsAndReplaysPackets() {
    QTemporaryFile scenario;
    QVERIFY(scenario.open());
    scenario.write(scenarioJson());
    QVERIFY(scenario.flush());
    qRegisterMetaType<miata::data::Vn300PacketRecord>();
    miata::data::Vn300Simulator simulator;
    QString error;
    QVERIFY(simulator.loadScenario(scenario.fileName(), &error));
    QSignalSpy packets(&simulator, &miata::data::Vn300Simulator::packetGenerated);
    simulator.start();
    QTRY_VERIFY_WITH_TIMEOUT(packets.count() >= 2, 1000);
    simulator.stop();

    QTemporaryFile capture;
    QVERIFY(capture.open());
    QTextStream stream(&capture);
    for (int i = 0; i < 2; ++i) {
        stream << miata::data::Vn300LogCodec::formatLine(
            qvariant_cast<miata::data::Vn300PacketRecord>(packets.at(i).at(0))) << '\n';
    }
    stream.flush();
    capture.flush();

    miata::data::Vn300ReplaySource replay;
    QVERIFY2(replay.load(capture.fileName(), &error), qPrintable(error));
    QSignalSpy sampleBatches(&replay, &miata::data::Vn300ReplaySource::samplesReceived);
    QSignalSpy finished(&replay, &miata::data::Vn300ReplaySource::replayFinished);
    QVERIFY(replay.start(miata::data::Vn300ReplaySource::TimingMode::Fast, 1.0, &error));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 1000);
    QCOMPARE(sampleBatches.count(), 2);
}

QTEST_MAIN(Vn300SimulatorTest)
#include "vn300_simulator_test.moc"
