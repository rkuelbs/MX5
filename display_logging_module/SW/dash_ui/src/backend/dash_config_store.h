#pragma once

#include "core/signal_sample.h"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QTimer>

#include <array>
#include <optional>

namespace miata::dash {

enum class ThresholdKind {
    LowWarning = 0,
    LowCaution,
    HighCaution,
    HighWarning,
};

struct ThresholdRule {
    bool configured = false;
    std::optional<double> constantValue;
    QString signalName;
    std::optional<double> fallback;
    double hysteresis = 0.0;
    int activationDelayMs = 0;
    int clearDelayMs = 0;
    int sourceStaleMs = 1000;
    bool resolvedValid = false;
    double resolvedValue = 0.0;
};

struct FreshnessRule {
    bool configured = false;
    int staleAfterMs = 1000;
    int activationDelayMs = 0;
    int clearDelayMs = 0;
    QString severity = QStringLiteral("warning");
};

struct EnableCondition {
    bool configured = false;
    QString signalName;
    double equalsValue = 1.0;
    bool fallback = false;
    int sourceStaleMs = 1000;
    bool resolvedEnabled = true;
};

class SignalPresentation final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString signalName READ signalName CONSTANT)
    Q_PROPERTY(QString displayName READ displayName CONSTANT)
    Q_PROPERTY(int precision READ precision CONSTANT)
    Q_PROPERTY(double gaugeMinimum READ gaugeMinimum CONSTANT)
    Q_PROPERTY(double gaugeMaximum READ gaugeMaximum CONSTANT)
    Q_PROPERTY(double majorStep READ majorStep CONSTANT)
    Q_PROPERTY(double minorStep READ minorStep CONSTANT)
    Q_PROPERTY(bool lowWarningValid READ lowWarningValid NOTIFY thresholdsChanged)
    Q_PROPERTY(double lowWarning READ lowWarning NOTIFY thresholdsChanged)
    Q_PROPERTY(bool lowCautionValid READ lowCautionValid NOTIFY thresholdsChanged)
    Q_PROPERTY(double lowCaution READ lowCaution NOTIFY thresholdsChanged)
    Q_PROPERTY(bool highCautionValid READ highCautionValid NOTIFY thresholdsChanged)
    Q_PROPERTY(double highCaution READ highCaution NOTIFY thresholdsChanged)
    Q_PROPERTY(bool highWarningValid READ highWarningValid NOTIFY thresholdsChanged)
    Q_PROPERTY(double highWarning READ highWarning NOTIFY thresholdsChanged)
    Q_PROPERTY(bool freshnessConfigured READ freshnessConfigured CONSTANT)
    Q_PROPERTY(int staleAfterMs READ staleAfterMs CONSTANT)
    Q_PROPERTY(bool warningEnabled READ warningEnabled NOTIFY thresholdsChanged)

public:
    explicit SignalPresentation(QString signalName, QObject* parent = nullptr);

    [[nodiscard]] QString signalName() const;
    [[nodiscard]] QString displayName() const;
    [[nodiscard]] int precision() const;
    [[nodiscard]] double gaugeMinimum() const;
    [[nodiscard]] double gaugeMaximum() const;
    [[nodiscard]] double majorStep() const;
    [[nodiscard]] double minorStep() const;
    [[nodiscard]] bool lowWarningValid() const;
    [[nodiscard]] double lowWarning() const;
    [[nodiscard]] bool lowCautionValid() const;
    [[nodiscard]] double lowCaution() const;
    [[nodiscard]] bool highCautionValid() const;
    [[nodiscard]] double highCaution() const;
    [[nodiscard]] bool highWarningValid() const;
    [[nodiscard]] double highWarning() const;
    [[nodiscard]] bool freshnessConfigured() const;
    [[nodiscard]] int staleAfterMs() const;
    [[nodiscard]] bool warningEnabled() const;
    [[nodiscard]] const ThresholdRule& threshold(ThresholdKind kind) const;
    [[nodiscard]] const FreshnessRule& freshness() const;

signals:
    void thresholdsChanged();

private:
    friend class DashConfigStore;
    ThresholdRule& mutableThreshold(ThresholdKind kind);
    bool setResolved(ThresholdKind kind, bool valid, double value);

    QString signalName_;
    QString displayName_;
    int precision_ = 0;
    double gaugeMinimum_ = 0.0;
    double gaugeMaximum_ = 100.0;
    double majorStep_ = 10.0;
    double minorStep_ = 5.0;
    std::array<ThresholdRule, 4> thresholds_;
    FreshnessRule freshness_;
    EnableCondition enableCondition_;
};

class DashConfigStore final : public QObject {
    Q_OBJECT

public:
    explicit DashConfigStore(QObject* parent = nullptr);

    bool load(const QString& path, QString* errorMessage = nullptr);
    Q_INVOKABLE QObject* presentation(const QString& signalName) const;
    [[nodiscard]] SignalPresentation* presentationObject(const QString& signalName) const;
    [[nodiscard]] QStringList configuredSignalNames() const;

public slots:
    void updateSamples(const QList<miata::data::SignalSample>& samples);

signals:
    void thresholdsChanged(const QString& signalName);

private:
    struct DynamicValue {
        double value = 0.0;
        qint64 receiptNs = -1;
    };

    void recomputeAll();
    void recompute(SignalPresentation* presentation);

    QHash<QString, SignalPresentation*> presentations_;
    QHash<QString, DynamicValue> dynamicValues_;
    QElapsedTimer clock_;
    QTimer freshnessTimer_;
};

}  // namespace miata::dash
