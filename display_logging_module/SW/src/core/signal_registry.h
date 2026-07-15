#pragma once

#include "core/signal_sample.h"

#include <QHash>
#include <QObject>
#include <QStringList>

#include <optional>

namespace miata::data {

// Stores the latest value for every canonical signal. All calls currently run
// on the owning Qt thread; input workers will publish through queued signals.
class SignalRegistry final : public QObject {
    Q_OBJECT

public:
    explicit SignalRegistry(QObject* parent = nullptr);

    [[nodiscard]] bool contains(const QString& canonicalName) const;
    [[nodiscard]] std::optional<SignalSample> sample(const QString& canonicalName) const;
    [[nodiscard]] QStringList signalNames() const;
    [[nodiscard]] std::optional<qint64> ageNs(
        const QString& canonicalName,
        qint64 nowNs) const;
    [[nodiscard]] bool isFresh(
        const QString& canonicalName,
        qint64 nowNs,
        qint64 maximumAgeNs) const;

public slots:
    void update(const miata::data::SignalSample& sample);

signals:
    void sampleUpdated(const miata::data::SignalSample& sample);

private:
    QHash<QString, SignalSample> samples_;
};

}  // namespace miata::data
