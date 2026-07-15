#include "core/signal_registry.h"

#include <algorithm>

namespace miata::data {

SignalRegistry::SignalRegistry(QObject* parent) : QObject(parent) {}

bool SignalRegistry::contains(const QString& canonicalName) const {
    return samples_.contains(canonicalName);
}

std::optional<SignalSample> SignalRegistry::sample(const QString& canonicalName) const {
    const auto iterator = samples_.constFind(canonicalName);
    if (iterator == samples_.cend()) {
        return std::nullopt;
    }
    return iterator.value();
}

QStringList SignalRegistry::signalNames() const {
    QStringList names = samples_.keys();
    std::sort(names.begin(), names.end());
    return names;
}

std::optional<qint64> SignalRegistry::ageNs(
    const QString& canonicalName,
    qint64 nowNs) const {
    const auto current = sample(canonicalName);
    if (!current) return std::nullopt;
    return std::max<qint64>(0, nowNs - current->monotonicTimestampNs);
}

bool SignalRegistry::isFresh(
    const QString& canonicalName,
    qint64 nowNs,
    qint64 maximumAgeNs) const {
    const auto age = ageNs(canonicalName, nowNs);
    return age.has_value() && maximumAgeNs >= 0 && *age <= maximumAgeNs;
}

void SignalRegistry::update(const SignalSample& sample) {
    if (sample.canonicalName.isEmpty()) {
        return;
    }

    const auto existing = samples_.constFind(sample.canonicalName);
    if (existing != samples_.cend()
        && sample.monotonicTimestampNs < existing->monotonicTimestampNs) {
        return;
    }

    samples_.insert(sample.canonicalName, sample);
    emit sampleUpdated(sample);
}

}  // namespace miata::data
