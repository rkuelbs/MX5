#include "service_log_catalog.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QRegularExpression>

#include <algorithm>

namespace miata::service {
namespace {

const QRegularExpression kSessionId(
    QStringLiteral("^vehicle_[0-9]{8}_[0-9]{6}_[0-9]{3}$"));
const QRegularExpression kLogFileName(
    QStringLiteral("^vehicle_[0-9]{8}_[0-9]{6}_[0-9]{3}\\.(?:mf4|can\\.log)$"));

struct Session {
    QString id;
    QJsonArray files;
    qint64 bytes = 0;
    QDateTime modifiedUtc;
    bool protectedByMarker = false;
};

bool fail(QString* errorMessage, const QString& message) {
    if (errorMessage) *errorMessage = message;
    return false;
}

QString stemFor(const QString& fileName) {
    if (fileName.endsWith(QStringLiteral(".can.log"))) return fileName.chopped(8);
    if (fileName.endsWith(QStringLiteral(".active"))) return fileName.chopped(7);
    if (fileName.endsWith(QStringLiteral(".mf4"))) return fileName.chopped(4);
    return {};
}

QDateTime startTimeFor(const QString& sessionId) {
    QDateTime time = QDateTime::fromString(
        sessionId.mid(8), QStringLiteral("yyyyMMdd_HHmmss_zzz"));
    time.setTimeZone(QTimeZone::UTC);
    return time;
}

}  // namespace

void ServiceLogCatalog::setDirectory(const QString& path) { directoryPath_ = path; }

QJsonObject ServiceLogCatalog::catalogJson() const {
    QDir directory(directoryPath_);
    QHash<QString, Session> sessions;
    const QFileInfoList entries = directory.entryInfoList(
        {QStringLiteral("vehicle_*.mf4"), QStringLiteral("vehicle_*.can.log"),
         QStringLiteral("vehicle_*.active")},
        QDir::Files | QDir::Readable, QDir::Name);
    for (const QFileInfo& info : entries) {
        if (info.isSymLink()) continue;
        const QString stem = stemFor(info.fileName());
        if (!kSessionId.match(stem).hasMatch()) continue;
        auto& session = sessions[stem];
        session.id = stem;
        const QDateTime modified = info.lastModified().toUTC();
        if (!session.modifiedUtc.isValid() || modified > session.modifiedUtc)
            session.modifiedUtc = modified;
        if (info.fileName().endsWith(QStringLiteral(".active"))) {
            session.protectedByMarker = true;
            continue;
        }
        session.bytes += info.size();
        session.files.append(QJsonObject{
            {QStringLiteral("name"), info.fileName()},
            {QStringLiteral("bytes"), double(info.size())},
        });
    }

    QList<Session> ordered = sessions.values();
    std::sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) {
        return left.id > right.id;
    });
    QJsonArray result;
    qint64 managedBytes = 0;
    int completed = 0;
    int protectedCount = 0;
    for (const auto& session : ordered) {
        managedBytes += session.bytes;
        if (session.protectedByMarker) ++protectedCount; else ++completed;
        const QDateTime started = startTimeFor(session.id);
        result.append(QJsonObject{
            {QStringLiteral("id"), session.id},
            {QStringLiteral("started_utc"), started.isValid() ? started.toString(Qt::ISODateWithMs) : QString{}},
            {QStringLiteral("modified_utc"), session.modifiedUtc.toString(Qt::ISODateWithMs)},
            {QStringLiteral("bytes"), double(session.bytes)},
            {QStringLiteral("complete"), !session.protectedByMarker},
            {QStringLiteral("status"), session.protectedByMarker
                 ? QStringLiteral("active-or-incomplete") : QStringLiteral("complete")},
            {QStringLiteral("files"), session.files},
        });
    }
    return {
        {QStringLiteral("managed_bytes"), double(managedBytes)},
        {QStringLiteral("session_count"), ordered.size()},
        {QStringLiteral("completed_count"), completed},
        {QStringLiteral("protected_count"), protectedCount},
        {QStringLiteral("sessions"), result},
    };
}

QString ServiceLogCatalog::resolveDownload(
    const QString& fileName, QString* errorMessage) const {
    if (!kLogFileName.match(fileName).hasMatch() || QFileInfo(fileName).fileName() != fileName) {
        fail(errorMessage, QStringLiteral("invalid log file name"));
        return {};
    }
    const QFileInfo file(QDir(directoryPath_).filePath(fileName));
    if (!file.exists() || !file.isFile() || file.isSymLink()) {
        fail(errorMessage, QStringLiteral("log file does not exist"));
        return {};
    }
    return file.absoluteFilePath();
}

bool ServiceLogCatalog::deleteCompletedSession(
    const QString& sessionId, QString* errorMessage) const {
    if (!kSessionId.match(sessionId).hasMatch())
        return fail(errorMessage, QStringLiteral("invalid log session ID"));
    QDir directory(directoryPath_);
    if (QFile::exists(directory.filePath(sessionId + QStringLiteral(".active"))))
        return fail(errorMessage, QStringLiteral("active or incomplete sessions are protected"));

    const QStringList names{
        sessionId + QStringLiteral(".mf4"),
        sessionId + QStringLiteral(".can.log"),
    };
    bool found = false;
    for (const QString& name : names) {
        const QFileInfo info(directory.filePath(name));
        if (!info.exists()) continue;
        found = true;
        if (!info.isFile() || info.isSymLink())
            return fail(errorMessage, QStringLiteral("refusing to delete an invalid log path"));
        if (QFile::exists(directory.filePath(sessionId + QStringLiteral(".active"))))
            return fail(errorMessage, QStringLiteral("session became active during deletion"));
        if (!QFile::remove(info.absoluteFilePath()))
            return fail(errorMessage, QStringLiteral("could not delete %1").arg(name));
    }
    return found || fail(errorMessage, QStringLiteral("log session does not exist"));
}

}  // namespace miata::service
