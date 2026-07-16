#include "service_data_store.h"

#include <QCryptographicHash>
#include <QFile>
#include <QHostInfo>
#include <QJsonValue>
#include <QJsonDocument>
#include <QStorageInfo>
#include <QSysInfo>

#include <algorithm>

namespace miata::service {

ServiceDataStore::ServiceDataStore(QObject* parent) : QObject(parent) { clock_.start(); }

void ServiceDataStore::setLogDirectory(const QString& path) { logDirectory_ = path; }

void ServiceDataStore::setConfigurationPaths(
    const QString& dbcPath, const QString& loggingPath, const QString& dashPath) {
    dbcPath_ = dbcPath;
    loggingPath_ = loggingPath;
    dashPath_ = dashPath;
}

void ServiceDataStore::setLoggerConnected(bool connected) { loggerConnected_ = connected; }

void ServiceDataStore::updateDefinitions(
    const QList<miata::data::SignalDefinition>& definitions) {
    unitsByName_.clear();
    for (const auto& definition : definitions)
        unitsByName_.insert(definition.canonicalName, definition.unit);
}

void ServiceDataStore::updateSamples(const QList<miata::data::SignalSample>& samples) {
    const qint64 nowNs = clock_.nsecsElapsed();
    for (const auto& sample : samples) {
        if (sample.canonicalName.isEmpty()) continue;
        auto& latest = latestByName_[sample.canonicalName];
        latest.value = sample.value;
        latest.unit = sample.unit.isEmpty()
            ? unitsByName_.value(sample.canonicalName) : sample.unit;
        latest.source = sourceName(sample.source);
        latest.receiptNs = nowNs;
    }
    if (!samples.isEmpty()) lastBatchReceiptNs_ = nowNs;
}

QJsonObject ServiceDataStore::statusJson(
    bool actionsAvailable, bool actionBusy, const QString& lastActionResult) const {
    const qint64 nowNs = clock_.nsecsElapsed();
    const QStorageInfo storage(logDirectory_);
    QJsonObject configuration{
        {QStringLiteral("dbc_sha256"), fileSha256(dbcPath_)},
        {QStringLiteral("logging_sha256"), fileSha256(loggingPath_)},
        {QStringLiteral("dash_sha256"), fileSha256(dashPath_)},
    };
    return {
        {QStringLiteral("service_version"), QStringLiteral("0.1.0")},
        {QStringLiteral("host"), QHostInfo::localHostName()},
        {QStringLiteral("os"), QSysInfo::prettyProductName()},
        {QStringLiteral("architecture"), QSysInfo::currentCpuArchitecture()},
        {QStringLiteral("web_uptime_ms"), double(nowNs / 1'000'000)},
        {QStringLiteral("logger_connected"), loggerConnected_},
        {QStringLiteral("signal_count"), latestByName_.size()},
        {QStringLiteral("last_signal_batch_age_ms"), lastBatchReceiptNs_ < 0
             ? QJsonValue() : QJsonValue(double((nowNs - lastBatchReceiptNs_) / 1'000'000))},
        {QStringLiteral("log_directory"), logDirectory_},
        {QStringLiteral("storage_ready"), storage.isReady()},
        {QStringLiteral("storage_available_bytes"), double(storage.bytesAvailable())},
        {QStringLiteral("storage_total_bytes"), double(storage.bytesTotal())},
        {QStringLiteral("actions_available"), actionsAvailable},
        {QStringLiteral("action_busy"), actionBusy},
        {QStringLiteral("last_action_result"), lastActionResult},
        {QStringLiteral("configuration"), configuration},
        {QStringLiteral("retention"), storagePolicy(loggingPath_)},
    };
}

QJsonArray ServiceDataStore::signalsJson() const {
    QStringList names = latestByName_.keys();
    std::sort(names.begin(), names.end());
    const qint64 nowNs = clock_.nsecsElapsed();
    QJsonArray result;
    for (const auto& name : names) {
        const auto& latest = latestByName_[name];
        result.append(QJsonObject{
            {QStringLiteral("name"), name},
            {QStringLiteral("value"), QJsonValue::fromVariant(latest.value)},
            {QStringLiteral("unit"), latest.unit},
            {QStringLiteral("source"), latest.source},
            {QStringLiteral("age_ms"), double((nowNs - latest.receiptNs) / 1'000'000)},
        });
    }
    return result;
}

QString ServiceDataStore::sourceName(miata::data::SignalSource source) {
    switch (source) {
    case miata::data::SignalSource::Can: return QStringLiteral("CAN");
    case miata::data::SignalSource::Vn300: return QStringLiteral("VN300");
    case miata::data::SignalSource::Replay: return QStringLiteral("Replay");
    case miata::data::SignalSource::Derived: return QStringLiteral("Derived");
    }
    return QStringLiteral("Unknown");
}

QString ServiceDataStore::fileSha256(const QString& path) {
    QFile file(path);
    if (path.isEmpty() || !file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) return {};
    return QString::fromLatin1(hash.result().toHex());
}

QJsonObject ServiceDataStore::storagePolicy(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    return document.isObject()
        ? document.object().value(QStringLiteral("storage")).toObject() : QJsonObject{};
}

}  // namespace miata::service
