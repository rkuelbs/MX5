#include "service_config_manager.h"

#include "backend/dash_config_store.h"
#include "can/dbc_decoder.h"
#include "core/source_health_monitor.h"
#include "logging/decoded_log_policy.h"
#include "logging/log_storage_manager.h"
#include "vn300/vn300_binary_parser.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryFile>
#include <QUuid>

namespace miata::service {
namespace {

constexpr qsizetype kMaximumDbcBytes = 1024 * 1024;
constexpr qsizetype kMaximumJsonBytes = 256 * 1024;

bool fail(QString* errorMessage, const QString& message) {
    if (errorMessage) *errorMessage = message;
    return false;
}

bool validateDashReferences(
    const QString& path, const QSet<QString>& knownNames, QString* errorMessage) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return fail(errorMessage, file.errorString());
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) return fail(errorMessage, QStringLiteral("dash config is not a JSON object"));
    const QJsonObject presentations = document.object().value(QStringLiteral("signals")).toObject();
    for (auto entry = presentations.begin(); entry != presentations.end(); ++entry) {
        if (!knownNames.contains(entry.key()))
            return fail(errorMessage, QStringLiteral("dash config references unknown signal '%1'").arg(entry.key()));
        const QJsonObject presentation = entry.value().toObject();
        const QJsonObject thresholds = presentation.value(QStringLiteral("thresholds")).toObject();
        for (auto threshold = thresholds.begin(); threshold != thresholds.end(); ++threshold) {
            const QString source = threshold.value().toObject().value(QStringLiteral("signal")).toString();
            if (!source.isEmpty() && !knownNames.contains(source))
                return fail(errorMessage,
                            QStringLiteral("dash threshold for '%1' references unknown signal '%2'")
                                .arg(entry.key(), source));
        }
        const QString gate = presentation.value(QStringLiteral("enabled_when"))
                                 .toObject().value(QStringLiteral("signal")).toString();
        if (!gate.isEmpty() && !knownNames.contains(gate))
            return fail(errorMessage,
                        QStringLiteral("dash enable condition for '%1' references unknown signal '%2'")
                            .arg(entry.key(), gate));
    }
    return true;
}

}  // namespace

ServiceConfigManager::~ServiceConfigManager() { discard(); }

void ServiceConfigManager::setPaths(
    const QString& dbcPath, const QString& loggingPath, const QString& dashPath) {
    discard();
    dbcPath_ = QFileInfo(dbcPath).absoluteFilePath();
    loggingPath_ = QFileInfo(loggingPath).absoluteFilePath();
    dashPath_ = QFileInfo(dashPath).absoluteFilePath();
}

void ServiceConfigManager::setUpdatesEnabled(bool enabled) {
    updatesEnabled_ = enabled;
    if (!enabled) discard();
}

QJsonObject ServiceConfigManager::statusJson() const {
    QJsonObject staged;
    if (!staged_.token.isEmpty()) {
        staged = {
            {QStringLiteral("type"), staged_.type},
            {QStringLiteral("token"), staged_.token},
            {QStringLiteral("sha256"), staged_.sha256},
            {QStringLiteral("warnings"), QJsonArray::fromStringList(staged_.warnings)},
        };
    }
    return {
        {QStringLiteral("updates_enabled"), updatesEnabled_},
        {QStringLiteral("maximum_dbc_bytes"), double(kMaximumDbcBytes)},
        {QStringLiteral("maximum_json_bytes"), double(kMaximumJsonBytes)},
        {QStringLiteral("staged"), staged},
    };
}

bool ServiceConfigManager::stage(
    const QString& type, const QByteArray& contents,
    ConfigStageResult* result, QString* errorMessage) {
    if (!updatesEnabled_) return fail(errorMessage, QStringLiteral("configuration updates are disabled"));
    const QString target = targetPath(type);
    if (target.isEmpty()) return fail(errorMessage, QStringLiteral("unknown configuration type"));
    const qsizetype limit = type == QStringLiteral("dbc") ? kMaximumDbcBytes : kMaximumJsonBytes;
    if (contents.isEmpty() || contents.size() > limit)
        return fail(errorMessage, QStringLiteral("candidate configuration size is invalid"));

    discard();
    const QFileInfo targetInfo(target);
    QTemporaryFile candidate(
        QDir(targetInfo.absolutePath()).filePath(QStringLiteral(".miata-stage-%1-XXXXXX").arg(type)));
    candidate.setAutoRemove(false);
    if (!candidate.open()) return fail(errorMessage, candidate.errorString());
    const QString candidatePath = candidate.fileName();
    if (candidate.write(contents) != contents.size() || !candidate.flush()) {
        const QString error = candidate.errorString();
        candidate.close();
        QFile::remove(candidatePath);
        return fail(errorMessage, error);
    }
    candidate.close();

    const QString candidateDbc = type == QStringLiteral("dbc") ? candidatePath : dbcPath_;
    const QString candidateLogging = type == QStringLiteral("logging") ? candidatePath : loggingPath_;
    const QString candidateDash = type == QStringLiteral("dash") ? candidatePath : dashPath_;
    QStringList warnings;
    if (!validateSet(candidateDbc, candidateLogging, candidateDash, &warnings, errorMessage)) {
        QFile::remove(candidatePath);
        return false;
    }

    staged_.type = type;
    staged_.token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    staged_.path = candidatePath;
    staged_.sha256 = QString::fromLatin1(
        QCryptographicHash::hash(contents, QCryptographicHash::Sha256).toHex());
    staged_.warnings = warnings;
    if (result) *result = {staged_.token, staged_.sha256, staged_.warnings};
    return true;
}

bool ServiceConfigManager::activate(
    const QString& type, const QString& token, QString* errorMessage) {
    if (!updatesEnabled_) return fail(errorMessage, QStringLiteral("configuration updates are disabled"));
    if (token.isEmpty() || type != staged_.type || token != staged_.token)
        return fail(errorMessage, QStringLiteral("staged configuration token does not match"));
    const QString target = targetPath(type);
    const QString candidateDbc = type == QStringLiteral("dbc") ? staged_.path : dbcPath_;
    const QString candidateLogging = type == QStringLiteral("logging") ? staged_.path : loggingPath_;
    const QString candidateDash = type == QStringLiteral("dash") ? staged_.path : dashPath_;
    QStringList warnings;
    if (!validateSet(candidateDbc, candidateLogging, candidateDash, &warnings, errorMessage)) return false;

    QFile stagedFile(staged_.path);
    if (!stagedFile.open(QIODevice::ReadOnly)) return fail(errorMessage, stagedFile.errorString());
    const QByteArray candidateContents = stagedFile.readAll();
    stagedFile.close();
    QFile currentFile(target);
    if (!currentFile.open(QIODevice::ReadOnly)) return fail(errorMessage, currentFile.errorString());
    const QByteArray currentContents = currentFile.readAll();
    currentFile.close();
    if (!writeAtomic(target + QStringLiteral(".previous"), currentContents, errorMessage)
        || !writeAtomic(target, candidateContents, errorMessage))
        return false;
    discard();
    return true;
}

void ServiceConfigManager::discard() {
    if (!staged_.path.isEmpty()) QFile::remove(staged_.path);
    staged_ = {};
}

QString ServiceConfigManager::targetPath(const QString& type) const {
    if (type == QStringLiteral("dbc")) return dbcPath_;
    if (type == QStringLiteral("logging")) return loggingPath_;
    if (type == QStringLiteral("dash")) return dashPath_;
    return {};
}

bool ServiceConfigManager::validateSet(
    const QString& dbcPath, const QString& loggingPath, const QString& dashPath,
    QStringList* warnings, QString* errorMessage) const {
    miata::data::DbcDecoder decoder;
    if (!decoder.load(dbcPath, errorMessage)) return false;
    QStringList knownNames = decoder.canonicalSignalNames();
    knownNames.append(miata::data::Vn300BinaryParser::canonicalSignalNames());
    knownNames.append(miata::data::SourceHealthMonitor::canonicalSignalNames());
    knownNames.append(miata::data::LogStorageManager::canonicalSignalNames());

    miata::data::DecodedLogPolicy logging;
    if (!logging.load(loggingPath, knownNames, errorMessage)) return false;
    miata::data::LogStorageManager storage;
    if (!storage.loadConfiguration(loggingPath, errorMessage)) return false;
    miata::dash::DashConfigStore dash;
    if (!dash.load(dashPath, errorMessage)) return false;
    const QSet<QString> known(knownNames.cbegin(), knownNames.cend());
    if (!validateDashReferences(dashPath, known, errorMessage)) return false;
    if (warnings) {
        *warnings = decoder.warnings();
        warnings->append(logging.warnings());
    }
    return true;
}

bool ServiceConfigManager::writeAtomic(
    const QString& path, const QByteArray& contents, QString* errorMessage) {
    const QFile::Permissions permissions = QFileInfo(path).exists()
        ? QFile::permissions(path)
        : QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther;
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) return fail(errorMessage, output.errorString());
    if (output.write(contents) != contents.size() || !output.commit())
        return fail(errorMessage, output.errorString());
    QFile::setPermissions(path, permissions);
    return true;
}

}  // namespace miata::service
