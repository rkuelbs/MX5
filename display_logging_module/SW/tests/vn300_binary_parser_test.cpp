#include "vn300/vn300_binary_parser.h"

#include <QtEndian>
#include <QtTest>

#include <cstring>

class Vn300BinaryParserTest final : public QObject {
    Q_OBJECT

private slots:
    void parsesIcdGoldenYprPacket();
    void parsesEveryCommonField();
    void parsesSelectedCommonAndLinBodyAccelAcrossChunks();
    void rejectsBadCrcAndResynchronizes();
};

namespace {

void appendU16(QByteArray& bytes, quint16 value) {
    const quint16 little = qToLittleEndian(value);
    bytes.append(reinterpret_cast<const char*>(&little), sizeof(little));
}

template <typename T>
void appendValue(QByteArray& bytes, T value) {
    bytes.append(reinterpret_cast<const char*>(&value), sizeof(value));
}

quint16 referenceCrc(const QByteArray& bytes) {
    quint16 crc = 0;
    for (const char raw : bytes) {
        crc = static_cast<quint16>((crc >> 8) | (crc << 8));
        crc ^= static_cast<quint8>(raw);
        crc ^= static_cast<quint8>(crc & 0xffU) >> 4;
        crc ^= static_cast<quint16>(crc << 12);
        crc ^= static_cast<quint16>((crc & 0x00ffU) << 5);
    }
    return crc;
}

QByteArray finishPacket(QByteArray bytes) {
    const quint16 crc = referenceCrc(bytes.mid(1));
    bytes.append(static_cast<char>((crc >> 8) & 0xffU));
    bytes.append(static_cast<char>(crc & 0xffU));
    return bytes;
}

const miata::data::SignalSample* findSample(
    const QList<miata::data::SignalSample>& samples,
    const QString& name) {
    for (const auto& sample : samples) if (sample.canonicalName == name) return &sample;
    return nullptr;
}

}  // namespace

void Vn300BinaryParserTest::parsesIcdGoldenYprPacket() {
    const QByteArray packet = QByteArray::fromHex(
        "FA01080093502E42833EF13F48B504BB9288");
    QCOMPARE(miata::data::Vn300BinaryParser::calculateCrc(
                 packet.constData() + 1, packet.size() - 1), 0);

    miata::data::Vn300BinaryParser parser;
    const auto samples = parser.consume(packet, 1234);
    QCOMPARE(samples.size(), 3);
    QCOMPARE(samples[0].canonicalName, QStringLiteral("VN300.yaw"));
    QVERIFY(qAbs(samples[0].value.toDouble() - 43.578686) < 0.00001);
    QCOMPARE(samples[0].unit, QStringLiteral("deg"));
    QCOMPARE(samples[0].monotonicTimestampNs, 1234);
    QCOMPARE(samples[0].source, miata::data::SignalSource::Vn300);
    QCOMPARE(parser.validPacketCount(), 1);
}

void Vn300BinaryParserTest::parsesEveryCommonField() {
    QByteArray packet;
    packet.append(static_cast<char>(0xFA));
    packet.append(static_cast<char>(0x01));
    appendU16(packet, 0x7FFF);
    appendValue<quint64>(packet, 1'000'000'000ULL); // TimeStartup
    appendValue<quint64>(packet, 2'000'000'000ULL); // TimeGps
    appendValue<quint64>(packet, 3'000'000'000ULL); // TimeSyncIn
    for (int i = 0; i < 3; ++i) appendValue<float>(packet, 10.0F + i); // Ypr
    for (int i = 0; i < 4; ++i) appendValue<float>(packet, 20.0F + i); // Quaternion
    for (int i = 0; i < 3; ++i) appendValue<float>(packet, 30.0F + i); // AngularRate
    for (int i = 0; i < 3; ++i) appendValue<double>(packet, 40.0 + i); // PosLla
    for (int i = 0; i < 3; ++i) appendValue<float>(packet, 50.0F + i); // VelNed
    for (int i = 0; i < 3; ++i) appendValue<float>(packet, 60.0F + i); // Accel
    for (int i = 0; i < 6; ++i) appendValue<float>(packet, 70.0F + i); // Imu
    for (int i = 0; i < 5; ++i) appendValue<float>(packet, 80.0F + i); // MagPres
    for (int i = 0; i < 7; ++i) appendValue<float>(packet, 90.0F + i); // Deltas
    appendValue<quint16>(packet, 0x1234); // InsStatus
    appendValue<quint32>(packet, 55);     // SyncInCnt
    appendValue<quint64>(packet, 4'000'000'000ULL); // TimeGpsPps
    packet = finishPacket(packet);

    miata::data::Vn300BinaryParser parser;
    const auto samples = parser.consume(packet, 100);
    QCOMPARE(samples.size(), 43);
    QCOMPARE(findSample(samples, QStringLiteral("VN300.ins_status"))->value.toUInt(), 0x1234U);
    QCOMPARE(findSample(samples, QStringLiteral("VN300.sync_in_count"))->value.toUInt(), 55U);
    QCOMPARE(findSample(samples, QStringLiteral("VN300.time_gps_pps"))->value.toDouble(), 4.0);
    QCOMPARE(parser.validPacketCount(), 1);
}

void Vn300BinaryParserTest::parsesSelectedCommonAndLinBodyAccelAcrossChunks() {
    QByteArray packet;
    packet.append(static_cast<char>(0xFA));
    packet.append(static_cast<char>(0x11)); // Common + Attitude
    appendU16(packet, 0x4001);              // TimeStartup + TimeGpsPps
    appendU16(packet, 0x0040);              // LinBodyAcc
    appendValue<quint64>(packet, 2'500'000'000ULL);
    appendValue<quint64>(packet, 125'000'000ULL);
    appendValue<float>(packet, 1.25F);
    appendValue<float>(packet, -2.5F);
    appendValue<float>(packet, 0.125F);
    packet = finishPacket(packet);

    miata::data::Vn300BinaryParser parser;
    QVERIFY(parser.consume(packet.left(4), 50).isEmpty());
    QVERIFY(parser.consume(packet.mid(4, 9), 50).isEmpty());
    const auto samples = parser.consume(packet.mid(13), 50);
    QCOMPARE(samples.size(), 5);
    QVERIFY(qAbs(findSample(samples, QStringLiteral("VN300.time_startup"))->value.toDouble() - 2.5) < 1e-12);
    QVERIFY(qAbs(findSample(samples, QStringLiteral("VN300.time_gps_pps"))->value.toDouble() - 0.125) < 1e-12);
    QVERIFY(qAbs(findSample(samples, QStringLiteral("VN300.lin_body_accel_y"))->value.toDouble() + 2.5) < 1e-6);
    QCOMPARE(parser.validPacketCount(), 1);
}

void Vn300BinaryParserTest::rejectsBadCrcAndResynchronizes() {
    const QByteArray good = QByteArray::fromHex(
        "FA01080093502E42833EF13F48B504BB9288");
    QByteArray bad = good;
    bad[7] = static_cast<char>(bad[7] ^ 0x20);

    miata::data::Vn300BinaryParser parser;
    const auto samples = parser.consume(QByteArray::fromHex("001122") + bad + good, 99);
    QCOMPARE(samples.size(), 3);
    QCOMPARE(parser.crcErrorCount(), 1);
    QCOMPARE(parser.validPacketCount(), 1);
}

QTEST_MAIN(Vn300BinaryParserTest)
#include "vn300_binary_parser_test.moc"
