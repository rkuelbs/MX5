#pragma once

#include "core/signal_sample.h"

#include <QHash>
#include <QString>
#include <QStringList>

namespace miata::data {

// Selects which fully decoded samples are persisted. Acquisition, warning
// evaluation, and the latest-value registry remain at the source update rate.
class DecodedLogPolicy final {
public:
    bool load(
        const QString& configPath,
        const QStringList& knownCanonicalNames,
        QString* errorMessage = nullptr);

    [[nodiscard]] bool shouldLog(const SignalSample& sample);
    [[nodiscard]] QString groupForSignal(const QString& canonicalName) const;
    [[nodiscard]] QStringList warnings() const;

private:
    struct RateGroup {
        bool enabled = false;
        bool nativeRate = false;
        qint64 minimumIntervalNs = 0;
    };

    QHash<QString, RateGroup> groups_;
    QHash<QString, QString> signalGroups_;
    QHash<QString, qint64> lastLoggedTimestampNs_;
    QString defaultGroup_;
    QStringList warnings_;
};

}  // namespace miata::data
