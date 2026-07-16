#include "backend/signal_history_store.h"

#include <algorithm>
#include <cmath>

namespace miata::dash {

SignalHistoryStore::SignalHistoryStore(QObject* parent) : QObject(parent) {
    clock_.start();
    publishTimer_.setInterval(50);
    connect(&publishTimer_, &QTimer::timeout, this, &SignalHistoryStore::publishIfDirty);
    publishTimer_.start();
}

QVariantList SignalHistoryStore::series() const {
    QVariantList result;
    const qint64 nowNs = clock_.nsecsElapsed();
    for (const auto& name : selectedSignals_) {
        const auto trace = traces_.constFind(name);
        if (trace == traces_.cend()) continue;
        QVariantList points;
        points.reserve(trace->points.size());
        for (const auto& point : trace->points) {
            points.append(QVariantMap{
                {QStringLiteral("x"), double(point.receiptNs - nowNs) * 1e-9},
                {QStringLiteral("y"), point.value},
            });
        }
        result.append(QVariantMap{
            {QStringLiteral("name"), name},
            {QStringLiteral("unit"), trace->unit},
            {QStringLiteral("points"), points},
        });
    }
    return result;
}

int SignalHistoryStore::windowSeconds() const { return windowSeconds_; }
int SignalHistoryStore::maxSignals() const { return maxSignals_; }

void SignalHistoryStore::setWindowSeconds(int seconds) {
    const int bounded = std::clamp(seconds, 2, 120);
    if (bounded == windowSeconds_) return;
    windowSeconds_ = bounded;
    prune(clock_.nsecsElapsed());
    emit windowSecondsChanged();
    dirty_ = true;
    publishIfDirty();
}

void SignalHistoryStore::setSelectedSignals(const QStringList& signalNames) {
    QStringList bounded;
    for (const auto& name : signalNames) {
        if (name.isEmpty() || bounded.contains(name)) continue;
        bounded.append(name);
        if (bounded.size() == maxSignals_) break;
    }
    if (bounded == selectedSignals_) return;
    selectedSignals_ = bounded;
    for (auto iterator = traces_.begin(); iterator != traces_.end();) {
        if (!selectedSignals_.contains(iterator.key())) iterator = traces_.erase(iterator);
        else ++iterator;
    }
    for (const auto& name : selectedSignals_) traces_.tryInsert(name, {});
    dirty_ = true;
    publishIfDirty();
}

void SignalHistoryStore::updateSamples(const QList<miata::data::SignalSample>& samples) {
    if (selectedSignals_.isEmpty()) return;
    const qint64 nowNs = clock_.nsecsElapsed();
    for (const auto& sample : samples) {
        if (!selectedSignals_.contains(sample.canonicalName)) continue;
        bool numeric = false;
        const double value = sample.value.toDouble(&numeric);
        if (!numeric || !std::isfinite(value)) continue;
        auto& trace = traces_[sample.canonicalName];
        if (!sample.unit.isEmpty()) trace.unit = sample.unit;
        trace.points.append({nowNs, value});
        while (trace.points.size() > maxPointsPerSignal_) trace.points.removeFirst();
        dirty_ = true;
    }
    prune(nowNs);
}

void SignalHistoryStore::clear() {
    for (auto& trace : traces_) trace.points.clear();
    dirty_ = true;
    publishIfDirty();
}

void SignalHistoryStore::prune(qint64 nowNs) {
    const qint64 oldestNs = nowNs - qint64(windowSeconds_) * 1'000'000'000;
    for (auto& trace : traces_) {
        while (!trace.points.isEmpty() && trace.points.front().receiptNs < oldestNs)
            trace.points.removeFirst();
    }
}

void SignalHistoryStore::publishIfDirty() {
    if (!dirty_) return;
    dirty_ = false;
    emit seriesChanged();
}

}  // namespace miata::dash
