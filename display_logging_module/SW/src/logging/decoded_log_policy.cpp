#include "logging/decoded_log_policy.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <cmath>
#include <limits>

namespace miata::data {
namespace {

bool fail(QString* errorMessage, const QString& message) {
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
    return false;
}

}  // namespace

bool DecodedLogPolicy::load(
    const QString& configPath,
    const QStringList& knownCanonicalNames,
    QString* errorMessage) {
    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return fail(errorMessage, file.errorString());
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return fail(
            errorMessage,
            QStringLiteral("invalid logging config: %1").arg(parseError.errorString()));
    }

    const QJsonObject root = document.object();
    const QJsonObject groupObjects = root.value(QStringLiteral("rate_groups")).toObject();
    if (groupObjects.isEmpty()) {
        return fail(errorMessage, QStringLiteral("logging config has no rate_groups"));
    }

    QHash<QString, RateGroup> newGroups;
    for (auto iterator = groupObjects.begin(); iterator != groupObjects.end(); ++iterator) {
        if (!iterator.value().isObject()) {
            return fail(
                errorMessage,
                QStringLiteral("rate group '%1' must be an object").arg(iterator.key()));
        }

        const QJsonObject object = iterator.value().toObject();
        const QString mode = object.value(QStringLiteral("mode")).toString();
        RateGroup group;
        if (mode == QStringLiteral("native")) {
            group.enabled = true;
            group.nativeRate = true;
        } else if (mode == QStringLiteral("off")) {
            group.enabled = false;
        } else if (object.value(QStringLiteral("rate_hz")).isDouble()) {
            const double rateHz = object.value(QStringLiteral("rate_hz")).toDouble();
            if (!std::isfinite(rateHz) || rateHz <= 0.0) {
                return fail(
                    errorMessage,
                    QStringLiteral("rate group '%1' must have rate_hz > 0").arg(iterator.key()));
            }
            const double interval = 1.0e9 / rateHz;
            if (interval > static_cast<double>(std::numeric_limits<qint64>::max())) {
                return fail(errorMessage, QStringLiteral("rate group '%1' is too slow").arg(iterator.key()));
            }
            group.enabled = true;
            group.minimumIntervalNs = static_cast<qint64>(std::ceil(interval));
        } else {
            return fail(
                errorMessage,
                QStringLiteral("rate group '%1' needs mode 'native', mode 'off', or rate_hz")
                    .arg(iterator.key()));
        }
        newGroups.insert(iterator.key(), group);
    }

    const QString newDefaultGroup = root.value(QStringLiteral("default_group")).toString();
    if (!newGroups.contains(newDefaultGroup)) {
        return fail(
            errorMessage,
            QStringLiteral("default_group '%1' is not defined").arg(newDefaultGroup));
    }

    const QSet<QString> knownNames(knownCanonicalNames.cbegin(), knownCanonicalNames.cend());
    const QJsonObject assignments = root.value(QStringLiteral("signals")).toObject();
    QHash<QString, QString> newSignalGroups;
    for (auto iterator = assignments.begin(); iterator != assignments.end(); ++iterator) {
        if (!knownNames.contains(iterator.key())) {
            return fail(
                errorMessage,
                QStringLiteral("logging config references unknown signal '%1'").arg(iterator.key()));
        }
        if (!iterator.value().isString() || !newGroups.contains(iterator.value().toString())) {
            return fail(
                errorMessage,
                QStringLiteral("signal '%1' references an undefined rate group").arg(iterator.key()));
        }
        newSignalGroups.insert(iterator.key(), iterator.value().toString());
    }

    QStringList newWarnings;
    for (const QString& name : knownCanonicalNames) {
        if (!newSignalGroups.contains(name)) {
            newWarnings.append(
                QStringLiteral("signal '%1' uses default group '%2'").arg(name, newDefaultGroup));
        }
    }

    groups_ = std::move(newGroups);
    signalGroups_ = std::move(newSignalGroups);
    defaultGroup_ = newDefaultGroup;
    warnings_ = std::move(newWarnings);
    lastLoggedTimestampNs_.clear();
    return true;
}

bool DecodedLogPolicy::shouldLog(const SignalSample& sample) {
    const QString groupName = groupForSignal(sample.canonicalName);
    const auto groupIterator = groups_.constFind(groupName);
    if (groupIterator == groups_.cend() || !groupIterator->enabled) {
        return false;
    }
    if (groupIterator->nativeRate) {
        return true;
    }

    const auto previous = lastLoggedTimestampNs_.constFind(sample.canonicalName);
    if (previous != lastLoggedTimestampNs_.cend()
        && (sample.monotonicTimestampNs < *previous
            || sample.monotonicTimestampNs - *previous < groupIterator->minimumIntervalNs)) {
        return false;
    }
    lastLoggedTimestampNs_.insert(sample.canonicalName, sample.monotonicTimestampNs);
    return true;
}

QString DecodedLogPolicy::groupForSignal(const QString& canonicalName) const {
    return signalGroups_.value(canonicalName, defaultGroup_);
}

QStringList DecodedLogPolicy::warnings() const {
    return warnings_;
}

}  // namespace miata::data
