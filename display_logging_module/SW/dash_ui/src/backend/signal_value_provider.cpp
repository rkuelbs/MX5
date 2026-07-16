#include "backend/signal_value_provider.h"

#include <algorithm>

namespace miata::dash {
namespace {

QString sourceName(miata::data::SignalSource source) {
    switch (source) {
    case miata::data::SignalSource::Can: return QStringLiteral("CAN");
    case miata::data::SignalSource::Vn300: return QStringLiteral("VN300");
    case miata::data::SignalSource::Replay: return QStringLiteral("Replay");
    case miata::data::SignalSource::Derived: return QStringLiteral("Derived");
    }
    return QStringLiteral("Unknown");
}

}  // namespace

SignalChannel::SignalChannel(QString signalName, QObject* parent)
    : QObject(parent), signalName_(std::move(signalName)) {}

QString SignalChannel::signalName() const { return signalName_; }
QVariant SignalChannel::value() const { return value_; }
bool SignalChannel::valid() const { return value_.isValid(); }
QString SignalChannel::unit() const { return unit_; }
QString SignalChannel::sourceName() const { return sourceName_; }
bool SignalChannel::stale() const { return stale_; }

SignalValueProvider::SignalValueProvider(QObject* parent) : QObject(parent) {
    clock_.start();
    freshnessTimer_.setInterval(100);
    connect(&freshnessTimer_, &QTimer::timeout, this, &SignalValueProvider::refreshFreshness);
    freshnessTimer_.start();
}

QObject* SignalValueProvider::channel(const QString& signalName) {
    return channelObject(signalName);
}

SignalChannel* SignalValueProvider::channelObject(const QString& signalName) {
    auto* existing = channels_.value(signalName, nullptr);
    if (existing) return existing;
    auto* created = new SignalChannel(signalName, this);
    channels_.insert(signalName, created);
    return created;
}

int SignalValueProvider::staleAfterMs() const { return staleAfterMs_; }

void SignalValueProvider::setStaleAfterMs(int milliseconds) {
    const int bounded = std::max(1, milliseconds);
    if (bounded == staleAfterMs_) return;
    staleAfterMs_ = bounded;
    emit staleAfterMsChanged();
    refreshFreshness();
}

void SignalValueProvider::setSignalStaleAfterMs(
    const QString& signalName, int milliseconds) {
    auto* signal = channelObject(signalName);
    signal->staleAfterMs_ = std::max(1, milliseconds);
    refreshFreshness();
}

void SignalValueProvider::setDefinitions(
    const QList<miata::data::SignalDefinition>& definitions) {
    for (const auto& definition : definitions) {
        auto* signal = channelObject(definition.canonicalName);
        if (signal->unit_ == definition.unit) continue;
        signal->unit_ = definition.unit;
        emit signal->valueChanged();
    }
}

void SignalValueProvider::updateSamples(const QList<miata::data::SignalSample>& samples) {
    const qint64 now = clock_.nsecsElapsed();
    for (const auto& sample : samples) {
        if (sample.canonicalName.isEmpty()) continue;
        auto* signal = channelObject(sample.canonicalName);
        signal->value_ = sample.value;
        if (!sample.unit.isEmpty()) signal->unit_ = sample.unit;
        signal->sourceName_ = sourceName(sample.source);
        signal->localReceiptNs_ = now;
        const bool wasStale = signal->stale_;
        signal->stale_ = false;
        emit signal->valueChanged();
        if (wasStale) emit signal->staleChanged();
    }
}

void SignalValueProvider::refreshFreshness() {
    const qint64 now = clock_.nsecsElapsed();
    for (auto* signal : std::as_const(channels_)) {
        const int timeoutMs = signal->staleAfterMs_ > 0
            ? signal->staleAfterMs_ : staleAfterMs_;
        const bool stale = signal->localReceiptNs_ < 0
            || now - signal->localReceiptNs_ > qint64(timeoutMs) * 1'000'000;
        if (stale == signal->stale_) continue;
        signal->stale_ = stale;
        emit signal->staleChanged();
    }
}

}  // namespace miata::dash
