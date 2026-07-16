#include "logging/log_storage_manager.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStorageInfo>

#include <algorithm>
#include <cmath>
#include <limits>

namespace miata::data {
namespace {

struct SessionRecord {
    QString stem;
    QStringList paths;
    qint64 bytes = 0;
    QDateTime lastModifiedUtc;
    bool incomplete = false;
};

bool fail(QString* errorMessage, const QString& message) {
    if (errorMessage) *errorMessage = message;
    return false;
}

bool readNonNegativeInteger(
    const QJsonObject& object, const QString& name, qint64* result, QString* errorMessage) {
    const QJsonValue value = object.value(name);
    const double number = value.toDouble(-1.0);
    if (!value.isDouble() || !std::isfinite(number) || number < 0.0
        || std::floor(number) != number
        || number > static_cast<double>(std::numeric_limits<qint64>::max())) {
        return fail(errorMessage, QStringLiteral("storage.%1 must be a non-negative integer").arg(name));
    }
    *result = static_cast<qint64>(number);
    return true;
}

QString sessionStem(const QString& fileName) {
    if (!fileName.startsWith(QStringLiteral("vehicle_"))) return {};
    if (fileName.endsWith(QStringLiteral(".can.log"))) return fileName.chopped(8);
    if (fileName.endsWith(QStringLiteral(".active"))) return fileName.chopped(7);
    if (fileName.endsWith(QStringLiteral(".mf4"))) return fileName.chopped(4);
    return {};
}

QList<SessionRecord> scanSessions(const QDir& directory) {
    QHash<QString, SessionRecord> byStem;
    const QFileInfoList files = directory.entryInfoList(
        {QStringLiteral("vehicle_*.mf4"), QStringLiteral("vehicle_*.can.log"),
         QStringLiteral("vehicle_*.active")},
        QDir::Files | QDir::Readable, QDir::Name);
    for (const QFileInfo& info : files) {
        const QString stem = sessionStem(info.fileName());
        if (stem.isEmpty()) continue;
        auto& session = byStem[stem];
        session.stem = stem;
        session.paths.append(info.absoluteFilePath());
        session.bytes += info.size();
        const QDateTime modified = info.lastModified().toUTC();
        if (!session.lastModifiedUtc.isValid() || modified > session.lastModifiedUtc)
            session.lastModifiedUtc = modified;
        if (info.fileName().endsWith(QStringLiteral(".active"))) session.incomplete = true;
    }
    QList<SessionRecord> result = byStem.values();
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        if (left.lastModifiedUtc != right.lastModifiedUtc)
            return left.lastModifiedUtc < right.lastModifiedUtc;
        return left.stem < right.stem;
    });
    return result;
}

qint64 totalBytes(const QList<SessionRecord>& sessions) {
    qint64 total = 0;
    for (const auto& session : sessions) total += session.bytes;
    return total;
}

SignalSample sample(
    const QString& name, double value, const QString& unit, qint64 timestampNs) {
    return {name, value, unit, timestampNs, SignalSource::Derived};
}

}  // namespace

bool LogStorageManager::loadConfiguration(const QString& configPath, QString* errorMessage) {
    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly)) return fail(errorMessage, file.errorString());
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return fail(errorMessage,
                    QStringLiteral("invalid logging config: %1").arg(parseError.errorString()));
    }
    const QJsonValue storageValue = document.object().value(QStringLiteral("storage"));
    if (!storageValue.isObject())
        return fail(errorMessage, QStringLiteral("logging config has no storage object"));
    const QJsonObject storage = storageValue.toObject();
    qint64 minimumFree = 0;
    qint64 maximumTotal = 0;
    qint64 maximumAgeDays = 0;
    qint64 cleanupSeconds = 0;
    if (!readNonNegativeInteger(storage, QStringLiteral("minimum_free_bytes"), &minimumFree, errorMessage)
        || !readNonNegativeInteger(storage, QStringLiteral("maximum_total_bytes"), &maximumTotal, errorMessage)
        || !readNonNegativeInteger(storage, QStringLiteral("maximum_age_days"), &maximumAgeDays, errorMessage)
        || !readNonNegativeInteger(storage, QStringLiteral("cleanup_interval_seconds"), &cleanupSeconds, errorMessage))
        return false;
    if (cleanupSeconds < 1 || cleanupSeconds > 86'400)
        return fail(errorMessage, QStringLiteral("storage.cleanup_interval_seconds must be from 1 through 86400"));
    if (maximumAgeDays > std::numeric_limits<qint64>::max() / 86'400)
        return fail(errorMessage, QStringLiteral("storage.maximum_age_days is too large"));
    if (cleanupSeconds > std::numeric_limits<int>::max() / 1000)
        return fail(errorMessage, QStringLiteral("storage.cleanup_interval_seconds is too large"));

    minimumFreeBytes_ = minimumFree;
    maximumTotalBytes_ = maximumTotal;
    maximumAgeSeconds_ = maximumAgeDays * 86'400;
    cleanupIntervalMs_ = static_cast<int>(cleanupSeconds * 1000);
    configured_ = true;
    return true;
}

bool LogStorageManager::enforce(
    const QString& directoryPath, const QString& activeMdfPath, QString* errorMessage) {
    lastDeletedSessions_.clear();
    lastError_.clear();
    if (!configured_) return fail(errorMessage, QStringLiteral("log storage policy is not configured"));
    QDir directory(directoryPath);
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        lastError_ = QStringLiteral("could not create log directory %1").arg(directoryPath);
        ++cleanupErrorCount_;
        healthy_ = false;
        return fail(errorMessage, lastError_);
    }

    auto sessions = scanSessions(directory);
    qint64 managed = totalBytes(sessions);
    QStorageInfo storage(directory.absolutePath());
    storage.refresh();
    qint64 available = storage.isReady() ? storage.bytesAvailable() : -1;
    const QDateTime ageCutoff = maximumAgeSeconds_ > 0
        ? QDateTime::currentDateTimeUtc().addSecs(-maximumAgeSeconds_) : QDateTime{};

    for (auto& session : sessions) {
        const bool tooOld = ageCutoff.isValid() && session.lastModifiedUtc < ageCutoff;
        const bool tooLarge = maximumTotalBytes_ > 0 && managed > maximumTotalBytes_;
        const bool tooFull = available >= 0 && available < minimumFreeBytes_;
        if (session.incomplete || (!tooOld && !tooLarge && !tooFull)) continue;

        bool removed = true;
        for (const QString& path : session.paths) {
            if (!QFile::remove(path)) {
                removed = false;
                lastError_ = QStringLiteral("could not delete completed log file %1").arg(path);
                ++cleanupErrorCount_;
                break;
            }
        }
        if (!removed) continue;
        lastDeletedSessions_.append(session.stem);
        managed -= session.bytes;
        storage.refresh();
        available = storage.isReady() ? storage.bytesAvailable() : -1;
    }

    sessions = scanSessions(directory);
    managedBytes_ = totalBytes(sessions);
    sessionCount_ = sessions.size();
    incompleteSessionCount_ = 0;
    const QString activeStem = QFileInfo(activeMdfPath).completeBaseName();
    for (const auto& session : sessions) {
        if (session.incomplete && session.stem != activeStem) ++incompleteSessionCount_;
    }
    storage.refresh();
    availableBytes_ = storage.isReady() ? storage.bytesAvailable() : -1;
    const bool freeOkay = availableBytes_ >= 0 && availableBytes_ >= minimumFreeBytes_;
    const bool totalOkay = maximumTotalBytes_ == 0 || managedBytes_ <= maximumTotalBytes_;
    healthy_ = freeOkay && totalOkay && lastError_.isEmpty();
    if (!healthy_ && lastError_.isEmpty()) {
        lastError_ = availableBytes_ < 0
            ? QStringLiteral("log filesystem storage information is unavailable")
            : QStringLiteral("log storage limits cannot be met without deleting an incomplete session");
    }
    if (!healthy_ && errorMessage) *errorMessage = lastError_;
    return healthy_;
}

int LogStorageManager::cleanupIntervalMs() const { return cleanupIntervalMs_; }
bool LogStorageManager::healthy() const { return healthy_; }
qint64 LogStorageManager::availableBytes() const { return availableBytes_; }
qint64 LogStorageManager::managedBytes() const { return managedBytes_; }
int LogStorageManager::sessionCount() const { return sessionCount_; }
int LogStorageManager::incompleteSessionCount() const { return incompleteSessionCount_; }
quint64 LogStorageManager::cleanupErrorCount() const { return cleanupErrorCount_; }
QString LogStorageManager::lastError() const { return lastError_; }
QStringList LogStorageManager::lastDeletedSessions() const { return lastDeletedSessions_; }

QList<SignalSample> LogStorageManager::samples(
    qint64 monotonicTimestampNs, bool loggingActive) const {
    return {
        sample(QStringLiteral("LOGGER.storage_healthy"), healthy_ ? 1.0 : 0.0, {}, monotonicTimestampNs),
        sample(QStringLiteral("LOGGER.storage_free_bytes"), double(availableBytes_), QStringLiteral("bytes"), monotonicTimestampNs),
        sample(QStringLiteral("LOGGER.storage_used_bytes"), double(managedBytes_), QStringLiteral("bytes"), monotonicTimestampNs),
        sample(QStringLiteral("LOGGER.storage_session_count"), sessionCount_, {}, monotonicTimestampNs),
        sample(QStringLiteral("LOGGER.storage_incomplete_sessions"), incompleteSessionCount_, {}, monotonicTimestampNs),
        sample(QStringLiteral("LOGGER.storage_cleanup_errors"), double(cleanupErrorCount_), {}, monotonicTimestampNs),
        sample(QStringLiteral("LOGGER.logging_active"), loggingActive ? 1.0 : 0.0, {}, monotonicTimestampNs),
    };
}

QStringList LogStorageManager::canonicalSignalNames() {
    QStringList names;
    for (const auto& definition : signalDefinitions()) names.append(definition.canonicalName);
    return names;
}

QList<SignalDefinition> LogStorageManager::signalDefinitions() {
    return {
        {QStringLiteral("LOGGER.storage_healthy"), {}},
        {QStringLiteral("LOGGER.storage_free_bytes"), QStringLiteral("bytes")},
        {QStringLiteral("LOGGER.storage_used_bytes"), QStringLiteral("bytes")},
        {QStringLiteral("LOGGER.storage_session_count"), {}},
        {QStringLiteral("LOGGER.storage_incomplete_sessions"), {}},
        {QStringLiteral("LOGGER.storage_cleanup_errors"), {}},
        {QStringLiteral("LOGGER.logging_active"), {}},
    };
}

}  // namespace miata::data
