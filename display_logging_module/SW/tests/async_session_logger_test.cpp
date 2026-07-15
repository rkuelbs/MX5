#include "logging/async_session_logger.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <mdf/ichannel.h>
#include <mdf/ichannelgroup.h>
#include <mdf/ichannelobserver.h>
#include <mdf/idatagroup.h>
#include <mdf/mdfreader.h>

class AsyncSessionLoggerTest final : public QObject {
    Q_OBJECT

private slots:
    void drainsQueuedRecordsOnClose();
    void rejectsInvalidQueueCapacity();
};

void AsyncSessionLoggerTest::drainsQueuedRecordsOnClose() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    miata::data::AsyncSessionLogger logger;
    QString error;
    QVERIFY2(
        logger.start(
            directory.path(),
            QStringLiteral(MIATA_TEST_DBC_PATH),
            {{QStringLiteral("ECM.rpm"), QStringLiteral("RPM")}},
            {QStringLiteral(MIATA_TEST_DBC_PATH)},
            true,
            16,
            &error),
        qPrintable(error));
    const QString rawPath = logger.rawCanPath();
    const QString mdfPath = logger.mdfPath();
    QVERIFY(!rawPath.isEmpty());
    QVERIFY(!mdfPath.isEmpty());

    QVERIFY(logger.writeRawFrame(
        {QCanBusFrame(0x5F0, QByteArray::fromHex("0000000000000BB8")),
         25,
         QStringLiteral("can0")},
        &error));
    QVERIFY(logger.writeSignalSample(
        {QStringLiteral("ECM.rpm"),
         3000.0,
         QStringLiteral("RPM"),
         25,
         miata::data::SignalSource::Replay},
        &error));
    logger.close();

    QCOMPARE(logger.takeDroppedRecordCount(), 0);
    QVERIFY(logger.maximumQueueDepth() > 0);
    const QString workerError = logger.takeWorkerError();
    QVERIFY2(workerError.isEmpty(), qPrintable(workerError));

    QFile raw(rawPath);
    QVERIFY(raw.open(QIODevice::ReadOnly | QIODevice::Text));
    QVERIFY(raw.readAll().contains("can0 5f0#0000000000000BB8"));

    QFile mdf(mdfPath);
    QVERIFY(mdf.open(QIODevice::ReadOnly));
    QVERIFY(mdf.read(8).startsWith("MDF"));
    mdf.close();

    mdf::MdfReader reader(mdfPath.toStdString());
    QVERIFY(reader.IsOk());
    QVERIFY(reader.ReadEverythingButData());
    mdf::IDataGroup* dataGroup = reader.GetDataGroup(0);
    QVERIFY(dataGroup != nullptr);
    const auto groups = dataGroup->ChannelGroups();
    QCOMPARE(groups.size(), 1);
    mdf::IChannelGroup* group = groups.front();
    QVERIFY(group != nullptr);
    QCOMPARE(QString::fromStdString(group->Name()), QStringLiteral("ECM.rpm"));
    const mdf::IChannel* rpm = group->GetChannel("ECM.rpm");
    QVERIFY(rpm != nullptr);
    QCOMPARE(QString::fromStdString(rpm->Unit()), QStringLiteral("RPM"));

    auto observer = mdf::CreateChannelObserver(*dataGroup, *group, *rpm);
    QVERIFY(observer != nullptr);
    QVERIFY(reader.ReadData(*dataGroup));
    QCOMPARE(observer->NofSamples(), 1);
    double rpmValue = 0.0;
    QVERIFY(observer->GetChannelValue(0, rpmValue));
    QCOMPARE(rpmValue, 3000.0);
}

void AsyncSessionLoggerTest::rejectsInvalidQueueCapacity() {
    miata::data::AsyncSessionLogger logger;
    QString error;
    QVERIFY(!logger.start(
        {}, QStringLiteral(MIATA_TEST_DBC_PATH), {}, {}, false, 0, &error));
    QVERIFY(error.contains(QStringLiteral("capacity")));
}

QTEST_MAIN(AsyncSessionLoggerTest)
#include "async_session_logger_test.moc"
