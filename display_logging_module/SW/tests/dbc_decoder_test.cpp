#include "can/dbc_decoder.h"

#include <QCanBusFrame>
#include <QTemporaryFile>
#include <QtTest>

#include <algorithm>

class DbcDecoderTest final : public QObject {
    Q_OBJECT

private slots:
    void usesSenderAndSignalForCanonicalName();
    void decodesEcmRpm();
    void encodesEcmRpm();
    void encodesAndDecodesWcmEvent();
    void rejectsDuplicateSignalNamesFromOneSender();
};

void DbcDecoderTest::usesSenderAndSignalForCanonicalName() {
    miata::data::DbcDecoder decoder;
    QString error;
    QVERIFY2(decoder.load(QStringLiteral(MIATA_TEST_DBC_PATH), &error), qPrintable(error));
    QVERIFY(decoder.canonicalSignalNames().contains(QStringLiteral("ECM.rpm")));
    QVERIFY(!decoder.canonicalSignalNames().contains(
        QStringLiteral("ECM.MS_Realtime_Group_00.rpm")));
    QVERIFY(decoder.canonicalSignalNames().contains(QStringLiteral("WCM.inputs")));
    QVERIFY(decoder.canonicalSignalNames().contains(QStringLiteral("WCM.event_type")));
    QVERIFY(!decoder.canonicalSignalNames().contains(QStringLiteral("WCM.wcm_inputs")));
}

void DbcDecoderTest::encodesAndDecodesWcmEvent() {
    miata::data::DbcDecoder codec;
    QString error;
    QVERIFY2(codec.load(QStringLiteral(MIATA_TEST_DBC_PATH), &error), qPrintable(error));

    const QCanBusFrame frame = codec.encodeMessage(
        256,
        {
            {QStringLiteral("WCM.event_type"), 0.0},
            {QStringLiteral("WCM.event_id"), 3.0},
            {QStringLiteral("WCM.event_length"), 1234.0},
        },
        &error);
    QVERIFY2(frame.isValid(), qPrintable(error));

    const auto samples = codec.decode(frame, 20, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const auto valueFor = [&samples](const QString& name) {
        const auto sample = std::find_if(
            samples.cbegin(), samples.cend(), [&name](const auto& candidate) {
                return candidate.canonicalName == name;
            });
        return sample == samples.cend() ? -1.0 : sample->value.toDouble();
    };

    QCOMPARE(valueFor(QStringLiteral("WCM.event_type")), 0.0);
    QCOMPARE(valueFor(QStringLiteral("WCM.event_id")), 3.0);
    QCOMPARE(valueFor(QStringLiteral("WCM.event_length")), 1234.0);
}

void DbcDecoderTest::decodesEcmRpm() {
    miata::data::DbcDecoder decoder;
    QString error;
    QVERIFY2(decoder.load(QStringLiteral(MIATA_TEST_DBC_PATH), &error), qPrintable(error));

    // MS_Realtime_Group_00 is big-endian. RPM occupies payload bytes 6 and 7.
    const QCanBusFrame frame(1520, QByteArray::fromHex("0000000000000BB8"));
    const auto samples = decoder.decode(frame, 123456, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    const auto rpm = std::find_if(samples.cbegin(), samples.cend(), [](const auto& sample) {
        return sample.canonicalName == QStringLiteral("ECM.rpm");
    });
    QVERIFY(rpm != samples.cend());
    QCOMPARE(rpm->value.toDouble(), 3000.0);
    QCOMPARE(rpm->unit, QStringLiteral("RPM"));
    QCOMPARE(rpm->monotonicTimestampNs, 123456);
}

void DbcDecoderTest::encodesEcmRpm() {
    miata::data::DbcDecoder codec;
    QString error;
    QVERIFY2(codec.load(QStringLiteral(MIATA_TEST_DBC_PATH), &error), qPrintable(error));

    const QCanBusFrame frame = codec.encodeMessage(
        1520, {{QStringLiteral("ECM.rpm"), 3000.0}}, &error);
    QVERIFY2(frame.isValid(), qPrintable(error));
    QCOMPARE(frame.frameId(), 1520U);

    const auto samples = codec.decode(frame, 10, &error);
    const auto rpm = std::find_if(samples.cbegin(), samples.cend(), [](const auto& sample) {
        return sample.canonicalName == QStringLiteral("ECM.rpm");
    });
    QVERIFY(rpm != samples.cend());
    QCOMPARE(rpm->value.toDouble(), 3000.0);
}

void DbcDecoderTest::rejectsDuplicateSignalNamesFromOneSender() {
    QTemporaryFile dbc;
    QVERIFY(dbc.open());
    dbc.write(R"DBC(VERSION "test"
NS_ :
BS_:
BU_: ECM RPI
BO_ 1 MessageA: 1 ECM
 SG_ rpm : 7|8@1+ (1,0) [0|255] "RPM" RPI
BO_ 2 MessageB: 1 ECM
 SG_ rpm : 7|8@1+ (1,0) [0|255] "RPM" RPI
)DBC");
    QVERIFY(dbc.flush());

    miata::data::DbcDecoder decoder;
    QString error;
    QVERIFY(!decoder.load(dbc.fileName(), &error));
    QVERIFY2(error.contains(QStringLiteral("Duplicate canonical signal ECM.rpm")), qPrintable(error));
}

QTEST_MAIN(DbcDecoderTest)
#include "dbc_decoder_test.moc"
