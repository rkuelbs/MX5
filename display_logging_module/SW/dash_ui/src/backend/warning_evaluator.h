#pragma once

#include "backend/dash_config_store.h"
#include "backend/warning_manager.h"
#include "core/signal_sample.h"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QTimer>

namespace miata::dash {

class WarningEvaluator final : public QObject {
    Q_OBJECT

public:
    enum class Level { Normal = 0, Caution = 1, Warning = 2 };

    WarningEvaluator(
        DashConfigStore* configuration,
        WarningManager* warningManager,
        QObject* parent = nullptr);

public slots:
    void processSamples(const QList<miata::data::SignalSample>& samples);

private slots:
    void evaluateAll();

private:
    struct LatestValue {
        double value = 0.0;
        QString unit;
        bool valid = false;
        qint64 receiptNs = -1;
    };

    struct State {
        Level current = Level::Normal;
        ThresholdKind activeKind = ThresholdKind::HighCaution;
        Level candidate = Level::Normal;
        ThresholdKind candidateKind = ThresholdKind::HighCaution;
        qint64 candidateSinceNs = -1;
    };

    struct DesiredState {
        Level level = Level::Normal;
        ThresholdKind kind = ThresholdKind::HighCaution;
    };

    struct FreshnessState {
        bool active = false;
        bool candidateStale = false;
        qint64 candidateSinceNs = -1;
        bool enabledLast = false;
        qint64 enabledSinceNs = -1;
    };

    [[nodiscard]] DesiredState desiredState(
        const SignalPresentation& presentation,
        const State& state,
        double value) const;
    [[nodiscard]] bool thresholdBreached(
        ThresholdKind kind, const ThresholdRule& rule, double value, bool withHysteresis) const;
    void evaluateSignal(const QString& signalName, qint64 nowNs);
    void evaluateFreshness(
        const QString& signalName, SignalPresentation& presentation, qint64 nowNs);
    void commit(
        const QString& signalName,
        SignalPresentation& presentation,
        State& state,
        const DesiredState& desired);

    DashConfigStore* configuration_ = nullptr;
    WarningManager* warningManager_ = nullptr;
    QHash<QString, LatestValue> latestValues_;
    QHash<QString, State> states_;
    QHash<QString, FreshnessState> freshnessStates_;
    QElapsedTimer clock_;
    QTimer evaluationTimer_;
};

}  // namespace miata::dash
