#include "logging/session_log_sink.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class SessionLogSinkTest final : public QObject {
    Q_OBJECT

private slots:
    void writesRawAndDecodedLogsWithDbcHash();
};

void SessionLogSinkTest::writesRawAndDecodedLogsWithDbcHash() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString rawPath = directory.filePath(QStringLiteral("session.can.log"));
    const QString csvPath = directory.filePath(QStringLiteral("session.signals.csv"));

    miata::data::SessionLogSink sink;
    QString error;
    QVERIFY2(
        sink.openFiles(rawPath, csvPath, QStringLiteral(MIATA_TEST_DBC_PATH), &error),
        qPrintable(error));
    QVERIFY(sink.writeRawFrame(
        {QCanBusFrame(0x5F0, QByteArray::fromHex("0000000000000BB8")), 25, QStringLiteral("can0")},
        &error));
    QVERIFY(sink.writeSignalSample(
        {QStringLiteral("ECM.rpm"), 3000.0, QStringLiteral("RPM"), 25,
         miata::data::SignalSource::Replay},
        &error));
    QVERIFY(sink.flush(&error));
    sink.close();

    QFile raw(rawPath);
    QVERIFY(raw.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray rawContents = raw.readAll();
    QVERIFY(rawContents.contains("dbc_sha256="));
    QVERIFY(rawContents.contains("can0 5f0#0000000000000BB8"));

    QFile csv(csvPath);
    QVERIFY(csv.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray csvContents = csv.readAll();
    QVERIFY(csvContents.contains("dbc_sha256="));
    QVERIFY(csvContents.contains("25,ECM.rpm,3000,RPM,REPLAY"));
}

QTEST_MAIN(SessionLogSinkTest)
#include "session_log_sink_test.moc"
