#include "can/candump_codec.h"

#include <QtTest>

class CandumpCodecTest final : public QObject {
    Q_OBJECT

private slots:
    void parsesAndFormatsClassicCan();
    void rejectsMalformedPayload();
    void ignoresCommentsAndBlankLines();
};

void CandumpCodecTest::parsesAndFormatsClassicCan() {
    QString error;
    auto record = miata::data::CandumpCodec::parseLine(
        QStringLiteral("(12.000000345) can0 5F0#0000000000000BB8"), &error);
    QVERIFY2(record.has_value(), qPrintable(error));
    QCOMPARE(record->monotonicTimestampNs, 12'000'000'345LL);
    QCOMPARE(record->interfaceName, QStringLiteral("can0"));
    QCOMPARE(record->frame.frameId(), 0x5F0U);
    QCOMPARE(record->frame.payload(), QByteArray::fromHex("0000000000000BB8"));
    QCOMPARE(
        miata::data::CandumpCodec::formatLine(*record),
        QStringLiteral("(12.000000345) can0 5f0#0000000000000BB8"));
}

void CandumpCodecTest::rejectsMalformedPayload() {
    QString error;
    const auto record = miata::data::CandumpCodec::parseLine(
        QStringLiteral("(0.0) can0 123#ABC"), &error);
    QVERIFY(!record.has_value());
    QVERIFY(error.contains(QStringLiteral("complete bytes")));
}

void CandumpCodecTest::ignoresCommentsAndBlankLines() {
    bool ignored = false;
    QVERIFY(!miata::data::CandumpCodec::parseLine(QStringLiteral("; metadata"), nullptr, &ignored));
    QVERIFY(ignored);
    ignored = false;
    QVERIFY(!miata::data::CandumpCodec::parseLine(QStringLiteral("   "), nullptr, &ignored));
    QVERIFY(ignored);
}

QTEST_MAIN(CandumpCodecTest)
#include "candump_codec_test.moc"
