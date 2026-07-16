#include "backend/warning_evaluator.h"

#include <cmath>

namespace miata::dash {
namespace {

int levelRank(WarningEvaluator::Level level) {
    return static_cast<int>(level);
}

QString levelText(WarningEvaluator::Level level) {
    return level == WarningEvaluator::Level::Warning
        ? QStringLiteral("warning") : QStringLiteral("caution");
}

}  // namespace

WarningEvaluator::WarningEvaluator(
    DashConfigStore* configuration, WarningManager* warningManager, QObject* parent)
    : QObject(parent), configuration_(configuration), warningManager_(warningManager) {
    clock_.start();
    evaluationTimer_.setInterval(50);
    connect(&evaluationTimer_, &QTimer::timeout, this, &WarningEvaluator::evaluateAll);
    evaluationTimer_.start();
}

void WarningEvaluator::processSamples(const QList<miata::data::SignalSample>& samples) {
    const qint64 nowNs = clock_.nsecsElapsed();
    for (const auto& sample : samples) {
        auto& latest = latestValues_[sample.canonicalName];
        latest.receiptNs = nowNs;
        latest.unit = sample.unit;
        bool numeric = false;
        const double value = sample.value.toDouble(&numeric);
        latest.valid = numeric && std::isfinite(value);
        if (latest.valid) latest.value = value;
    }
    evaluateAll();
}

void WarningEvaluator::evaluateAll() {
    if (!configuration_ || !warningManager_) return;
    const qint64 now = clock_.nsecsElapsed();
    for (const auto& signalName : configuration_->configuredSignalNames())
        evaluateSignal(signalName, now);
}

WarningEvaluator::DesiredState WarningEvaluator::desiredState(
    const SignalPresentation& presentation, const State& state, double value) const {
    constexpr std::array<ThresholdKind, 2> warningPriority{
        ThresholdKind::LowWarning, ThresholdKind::HighWarning};
    for (const auto kind : warningPriority) {
        const auto& rule = presentation.threshold(kind);
        if (rule.resolvedValid && thresholdBreached(kind, rule, value, false))
            return {Level::Warning, kind};
    }

    if (state.current != Level::Normal) {
        const auto& active = presentation.threshold(state.activeKind);
        if (active.resolvedValid && thresholdBreached(state.activeKind, active, value, true))
            return {state.current, state.activeKind};
    }

    constexpr std::array<ThresholdKind, 2> cautionPriority{
        ThresholdKind::LowCaution, ThresholdKind::HighCaution};
    for (const auto kind : cautionPriority) {
        const auto& rule = presentation.threshold(kind);
        if (!rule.resolvedValid || !thresholdBreached(kind, rule, value, false)) continue;
        return {Level::Caution, kind};
    }
    return {};
}

bool WarningEvaluator::thresholdBreached(
    ThresholdKind kind, const ThresholdRule& rule, double value, bool withHysteresis) const {
    const double hysteresis = withHysteresis ? rule.hysteresis : 0.0;
    if (kind == ThresholdKind::LowWarning || kind == ThresholdKind::LowCaution)
        return value <= rule.resolvedValue + hysteresis;
    return value >= rule.resolvedValue - hysteresis;
}

void WarningEvaluator::evaluateSignal(const QString& signalName, qint64 nowNs) {
    auto* presentation = configuration_->presentationObject(signalName);
    if (!presentation) return;
    auto& freshnessState = freshnessStates_[signalName];
    if (!presentation->warningEnabled()) {
        warningManager_->clearWarning(QStringLiteral("threshold:%1").arg(signalName));
        warningManager_->clearWarning(QStringLiteral("freshness:%1").arg(signalName));
        states_.remove(signalName);
        freshnessState = {};
        return;
    }
    if (!freshnessState.enabledLast) {
        freshnessState.enabledLast = true;
        freshnessState.enabledSinceNs = nowNs;
    }
    evaluateFreshness(signalName, *presentation, nowNs);
    const auto latest = latestValues_.constFind(signalName);
    if (latest == latestValues_.cend() || !latest->valid) return;
    if (presentation->freshness().configured
        && latest->receiptNs < freshnessState.enabledSinceNs) return;

    auto& state = states_[signalName];
    const DesiredState desired = desiredState(*presentation, state, latest->value);
    if (desired.level == state.current
        && (desired.level == Level::Normal || desired.kind == state.activeKind)) {
        state.candidateSinceNs = -1;
        return;
    }
    if (state.candidateSinceNs < 0 || desired.level != state.candidate
        || desired.kind != state.candidateKind) {
        state.candidate = desired.level;
        state.candidateKind = desired.kind;
        state.candidateSinceNs = nowNs;
    }

    int delayMs = 0;
    if (levelRank(desired.level) > levelRank(state.current)) {
        delayMs = presentation->threshold(desired.kind).activationDelayMs;
    } else if (state.current != Level::Normal) {
        delayMs = presentation->threshold(state.activeKind).clearDelayMs;
    }
    if (nowNs - state.candidateSinceNs >= qint64(delayMs) * 1'000'000)
        commit(signalName, *presentation, state, desired);
}

void WarningEvaluator::evaluateFreshness(
    const QString& signalName, SignalPresentation& presentation, qint64 nowNs) {
    const auto& rule = presentation.freshness();
    if (!rule.configured) return;

    const auto latest = latestValues_.constFind(signalName);
    const qint64 receiptNs = latest == latestValues_.cend() ? -1 : latest->receiptNs;
    auto& state = freshnessStates_[signalName];
    const qint64 effectiveReceiptNs = std::max(receiptNs, state.enabledSinceNs);
    const bool enabledStale = effectiveReceiptNs < 0
        ? nowNs >= qint64(rule.staleAfterMs) * 1'000'000
        : nowNs - effectiveReceiptNs > qint64(rule.staleAfterMs) * 1'000'000;
    if (enabledStale == state.active) {
        state.candidateSinceNs = -1;
        return;
    }
    if (state.candidateSinceNs < 0 || state.candidateStale != enabledStale) {
        state.candidateStale = enabledStale;
        state.candidateSinceNs = nowNs;
    }
    const int delayMs = enabledStale ? rule.activationDelayMs : rule.clearDelayMs;
    if (nowNs - state.candidateSinceNs < qint64(delayMs) * 1'000'000) return;

    state.active = enabledStale;
    state.candidateSinceNs = -1;
    const QString id = QStringLiteral("freshness:%1").arg(signalName);
    if (!enabledStale) {
        warningManager_->clearWarning(id);
        return;
    }
    warningManager_->raiseWarning(
        id,
        QStringLiteral("%1 DATA STALE").arg(presentation.displayName().toUpper()),
        QStringLiteral("No fresh %1 data has been received within %2 ms.")
            .arg(presentation.displayName()).arg(rule.staleAfterMs),
        rule.severity);
}

void WarningEvaluator::commit(
    const QString& signalName,
    SignalPresentation& presentation,
    State& state,
    const DesiredState& desired) {
    state.current = desired.level;
    state.activeKind = desired.kind;
    state.candidateSinceNs = -1;
    const QString id = QStringLiteral("threshold:%1").arg(signalName);
    if (desired.level == Level::Normal) {
        warningManager_->clearWarning(id);
        return;
    }

    const auto latest = latestValues_.value(signalName);
    const auto& rule = presentation.threshold(desired.kind);
    const bool low = desired.kind == ThresholdKind::LowWarning
        || desired.kind == ThresholdKind::LowCaution;
    const QString message = QStringLiteral("%1 is %2 %3; %4 threshold is %5 %3.")
        .arg(presentation.displayName())
        .arg(latest.value, 0, 'f', presentation.precision())
        .arg(latest.unit)
        .arg(levelText(desired.level))
        .arg(rule.resolvedValue, 0, 'f', presentation.precision())
        + (low ? QStringLiteral(" Value is below the configured limit.")
               : QStringLiteral(" Value is above the configured limit."));
    warningManager_->raiseWarning(
        id, presentation.displayName().toUpper(), message,
        desired.level == Level::Warning ? QStringLiteral("critical")
                                        : QStringLiteral("warning"));
}

}  // namespace miata::dash
