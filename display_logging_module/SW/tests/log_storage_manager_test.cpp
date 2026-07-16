#include "logging/log_storage_manager.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

class LogStorageManagerTest final : public QObject {
    Q_OBJECT

private slots:
    void removesOldestCompletedSessionAndProtectsIncompleteSession();
    void rejectsInvalidConfiguration();
};

namespace {

QString writeConfig(const QTemporaryDir& directory, qint64 maximumBytes) {
    const QString path = directory.filePath(QStringLiteral("logging.json"));
    const QJsonObject storage{
        {QStringLiteral("minimum_free_bytes"), 0},
        {QStringLiteral("maximum_total_bytes"), double(maximumBytes)},
        {QStringLiteral("maximum_age_days"), 0},
        {QStringLiteral("cleanup_interval_seconds"), 1},
    };
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return {};
    file.write(QJsonDocument(QJsonObject{{QStringLiteral("storage"), storage}}).toJson());
    return path;
}

bool writeSizedFile(const QString& path, int bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(QByteArray(bytes, 'x')) == bytes;
}

}  // namespace

void LogStorageManagerTest::removesOldestCompletedSessionAndProtectsIncompleteSession() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString oldStem = QStringLiteral("vehicle_20240101_000000_000");
    const QString newStem = QStringLiteral("vehicle_20250101_000000_000");
    const QString incompleteStem = QStringLiteral("vehicle_20260101_000000_000");
    QVERIFY(writeSizedFile(directory.filePath(oldStem + QStringLiteral(".mf4")), 10));
    QVERIFY(writeSizedFile(directory.filePath(newStem + QStringLiteral(".mf4")), 10));
    QVERIFY(writeSizedFile(directory.filePath(incompleteStem + QStringLiteral(".mf4")), 5));
    QVERIFY(writeSizedFile(directory.filePath(incompleteStem + QStringLiteral(".active")), 1));

    miata::data::LogStorageManager manager;
    QString error;
    QVERIFY2(manager.loadConfiguration(writeConfig(directory, 16), &error), qPrintable(error));
    QVERIFY2(manager.enforce(directory.path(), {}, &error), qPrintable(error));
    QVERIFY(!QFile::exists(directory.filePath(oldStem + QStringLiteral(".mf4"))));
    QVERIFY(QFile::exists(directory.filePath(newStem + QStringLiteral(".mf4"))));
    QVERIFY(QFile::exists(directory.filePath(incompleteStem + QStringLiteral(".mf4"))));
    QVERIFY(QFile::exists(directory.filePath(incompleteStem + QStringLiteral(".active"))));
    QCOMPARE(manager.lastDeletedSessions(), QStringList{oldStem});
    QCOMPARE(manager.managedBytes(), 16);
    QCOMPARE(manager.sessionCount(), 2);
    QCOMPARE(manager.incompleteSessionCount(), 1);
    QVERIFY(manager.healthy());

    const auto values = manager.samples(123, true);
    QCOMPARE(values.size(), miata::data::LogStorageManager::signalDefinitions().size());
    QCOMPARE(values.back().canonicalName, QStringLiteral("LOGGER.logging_active"));
    QCOMPARE(values.back().value.toDouble(), 1.0);
}

void LogStorageManagerTest::rejectsInvalidConfiguration() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("logging.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{\"storage\":{\"minimum_free_bytes\":-1}}\n");
    file.close();
    miata::data::LogStorageManager manager;
    QString error;
    QVERIFY(!manager.loadConfiguration(path, &error));
    QVERIFY(error.contains(QStringLiteral("minimum_free_bytes")));
}

QTEST_MAIN(LogStorageManagerTest)
#include "log_storage_manager_test.moc"
