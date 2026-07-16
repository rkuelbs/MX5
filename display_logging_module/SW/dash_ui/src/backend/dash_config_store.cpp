#include "backend/dash_config_store.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>

namespace miata::dash {
namespace {

constexpr std::array<const char*, 4> kThresholdNames{
    "low_warning", "low_caution", "high_caution", "high_warning"};

bool finiteNumber(const QJsonValue& value) {
    return value.isDouble() && std::isfinite(value.toDouble());
}

bool parseThreshold(
    const QJsonObject& object, ThresholdRule* rule, const QString& context, QString* error) {
    const bool hasValue = finiteNumber(object.value(QStringLiteral("value")));
    const bool hasSignal = object.value(QStringLiteral("signal")).isString()
        && !object.value(QStringLiteral("signal")).toString().isEmpty();
    if (hasValue == hasSignal) {
        if (error) *error = context + QStringLiteral(" must define exactly one of value or signal");
        return false;
    }
    rule->configured = true;
    if (hasValue) rule->constantValue = object.value(QStringLiteral("value")).toDouble();
    else rule->signalName = object.value(QStringLiteral("signal")).toString();
    if (object.contains(QStringLiteral("fallback"))) {
        if (!finiteNumber(object.value(QStringLiteral("fallback")))) {
            if (error) *error = context + QStringLiteral(" fallback must be numeric");
            return false;
        }
        rule->fallback = object.value(QStringLiteral("fallback")).toDouble();
    }
    rule->hysteresis = object.value(QStringLiteral("hysteresis")).toDouble(0.0);
    rule->activationDelayMs = object.value(QStringLiteral("activation_delay_ms")).toInt(0);
    rule->clearDelayMs = object.value(QStringLiteral("clear_delay_ms")).toInt(0);
    rule->sourceStaleMs = object.value(QStringLiteral("source_stale_ms")).toInt(1000);
    if (rule->hysteresis < 0.0 || rule->activationDelayMs < 0 || rule->clearDelayMs < 0
        || rule->sourceStaleMs <= 0) {
        if (error) *error = context + QStringLiteral(" timing and hysteresis values are invalid");
        return false;
    }
    return true;
}

bool parseFreshness(
    const QJsonObject& object, FreshnessRule* rule, const QString& context, QString* error) {
    rule->configured = true;
    rule->staleAfterMs = object.value(QStringLiteral("stale_after_ms")).toInt(1000);
    rule->activationDelayMs = object.value(QStringLiteral("activation_delay_ms")).toInt(0);
    rule->clearDelayMs = object.value(QStringLiteral("clear_delay_ms")).toInt(0);
    rule->severity = object.value(QStringLiteral("severity")).toString(QStringLiteral("warning"));
    if (rule->staleAfterMs <= 0 || rule->activationDelayMs < 0 || rule->clearDelayMs < 0
        || (rule->severity != QStringLiteral("warning")
            && rule->severity != QStringLiteral("critical"))) {
        if (error) *error = context + QStringLiteral(" has invalid timing or severity");
        return false;
    }
    return true;
}

bool parseEnableCondition(
    const QJsonObject& object, EnableCondition* condition,
    const QString& context, QString* error) {
    const auto signal = object.value(QStringLiteral("signal"));
    const auto equals = object.value(QStringLiteral("equals"));
    const auto fallback = object.value(QStringLiteral("fallback"));
    condition->configured = true;
    condition->signalName = signal.toString();
    condition->equalsValue = equals.toDouble();
    condition->fallback = fallback.toBool(false);
    condition->sourceStaleMs = object.value(QStringLiteral("source_stale_ms")).toInt(1000);
    if (!signal.isString() || condition->signalName.isEmpty() || !finiteNumber(equals)
        || (object.contains(QStringLiteral("fallback")) && !fallback.isBool())
        || condition->sourceStaleMs <= 0) {
        if (error) *error = context + QStringLiteral(" is invalid");
        return false;
    }
    condition->resolvedEnabled = condition->fallback;
    return true;
}

}  // namespace

SignalPresentation::SignalPresentation(QString signalName, QObject* parent)
    : QObject(parent), signalName_(std::move(signalName)), displayName_(signalName_) {}

QString SignalPresentation::signalName() const { return signalName_; }
QString SignalPresentation::displayName() const { return displayName_; }
int SignalPresentation::precision() const { return precision_; }
double SignalPresentation::gaugeMinimum() const { return gaugeMinimum_; }
double SignalPresentation::gaugeMaximum() const { return gaugeMaximum_; }
double SignalPresentation::majorStep() const { return majorStep_; }
double SignalPresentation::minorStep() const { return minorStep_; }
bool SignalPresentation::lowWarningValid() const { return threshold(ThresholdKind::LowWarning).resolvedValid; }
double SignalPresentation::lowWarning() const { return threshold(ThresholdKind::LowWarning).resolvedValue; }
bool SignalPresentation::lowCautionValid() const { return threshold(ThresholdKind::LowCaution).resolvedValid; }
double SignalPresentation::lowCaution() const { return threshold(ThresholdKind::LowCaution).resolvedValue; }
bool SignalPresentation::highCautionValid() const { return threshold(ThresholdKind::HighCaution).resolvedValid; }
double SignalPresentation::highCaution() const { return threshold(ThresholdKind::HighCaution).resolvedValue; }
bool SignalPresentation::highWarningValid() const { return threshold(ThresholdKind::HighWarning).resolvedValid; }
double SignalPresentation::highWarning() const { return threshold(ThresholdKind::HighWarning).resolvedValue; }
bool SignalPresentation::freshnessConfigured() const { return freshness_.configured; }
int SignalPresentation::staleAfterMs() const { return freshness_.staleAfterMs; }
bool SignalPresentation::warningEnabled() const { return enableCondition_.resolvedEnabled; }

const ThresholdRule& SignalPresentation::threshold(ThresholdKind kind) const {
    return thresholds_[static_cast<std::size_t>(kind)];
}

const FreshnessRule& SignalPresentation::freshness() const { return freshness_; }

ThresholdRule& SignalPresentation::mutableThreshold(ThresholdKind kind) {
    return thresholds_[static_cast<std::size_t>(kind)];
}

bool SignalPresentation::setResolved(ThresholdKind kind, bool valid, double value) {
    auto& rule = mutableThreshold(kind);
    if (rule.resolvedValid == valid && (!valid || qFuzzyCompare(rule.resolvedValue, value)))
        return false;
    rule.resolvedValid = valid;
    rule.resolvedValue = value;
    return true;
}

DashConfigStore::DashConfigStore(QObject* parent) : QObject(parent) {
    clock_.start();
    freshnessTimer_.setInterval(100);
    connect(&freshnessTimer_, &QTimer::timeout, this, &DashConfigStore::recomputeAll);
    freshnessTimer_.start();
}

bool DashConfigStore::load(const QString& path, QString* errorMessage) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) *errorMessage = file.errorString();
        return false;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (!document.isObject()) {
        if (errorMessage) *errorMessage = QStringLiteral("Dash config JSON is invalid: %1")
            .arg(parseError.errorString());
        return false;
    }
    const auto root = document.object();
    if (root.value(QStringLiteral("schema_version")).toInt(-1) != 1
        || !root.value(QStringLiteral("signals")).isObject()) {
        if (errorMessage) *errorMessage = QStringLiteral("Dash config requires schema_version 1 and signals");
        return false;
    }

    qDeleteAll(presentations_);
    presentations_.clear();
    dynamicValues_.clear();
    const auto signalObjects = root.value(QStringLiteral("signals")).toObject();
    for (auto iterator = signalObjects.begin(); iterator != signalObjects.end(); ++iterator) {
        if (!iterator.value().isObject()) {
            if (errorMessage) *errorMessage = QStringLiteral("Presentation %1 must be an object").arg(iterator.key());
            return false;
        }
        const auto object = iterator.value().toObject();
        auto* presentation = new SignalPresentation(iterator.key(), this);
        presentation->displayName_ = object.value(QStringLiteral("display_name")).toString(iterator.key());
        presentation->precision_ = object.value(QStringLiteral("precision")).toInt(0);
        const auto gauge = object.value(QStringLiteral("gauge")).toObject();
        presentation->gaugeMinimum_ = gauge.value(QStringLiteral("minimum")).toDouble(0.0);
        presentation->gaugeMaximum_ = gauge.value(QStringLiteral("maximum")).toDouble(100.0);
        presentation->majorStep_ = gauge.value(QStringLiteral("major_step")).toDouble(10.0);
        presentation->minorStep_ = gauge.value(QStringLiteral("minor_step")).toDouble(5.0);
        if (presentation->precision_ < 0 || presentation->precision_ > 6
            || presentation->gaugeMaximum_ <= presentation->gaugeMinimum_
            || presentation->majorStep_ <= 0.0 || presentation->minorStep_ <= 0.0) {
            delete presentation;
            if (errorMessage) *errorMessage = QStringLiteral("Presentation %1 has an invalid gauge or precision").arg(iterator.key());
            return false;
        }

        const auto thresholds = object.value(QStringLiteral("thresholds")).toObject();
        for (std::size_t index = 0; index < kThresholdNames.size(); ++index) {
            const QString key = QString::fromLatin1(kThresholdNames[index]);
            if (!thresholds.contains(key)) continue;
            if (!thresholds.value(key).isObject()
                || !parseThreshold(thresholds.value(key).toObject(), &presentation->thresholds_[index],
                                   iterator.key() + QLatin1Char('.') + key, errorMessage)) {
                delete presentation;
                return false;
            }
        }
        if (object.contains(QStringLiteral("freshness"))) {
            if (!object.value(QStringLiteral("freshness")).isObject()
                || !parseFreshness(object.value(QStringLiteral("freshness")).toObject(),
                                   &presentation->freshness_,
                                   iterator.key() + QStringLiteral(".freshness"), errorMessage)) {
                delete presentation;
                return false;
            }
        }
        if (object.contains(QStringLiteral("enabled_when"))) {
            if (!object.value(QStringLiteral("enabled_when")).isObject()
                || !parseEnableCondition(object.value(QStringLiteral("enabled_when")).toObject(),
                                         &presentation->enableCondition_,
                                         iterator.key() + QStringLiteral(".enabled_when"),
                                         errorMessage)) {
                delete presentation;
                return false;
            }
        }
        presentations_.insert(iterator.key(), presentation);
    }
    recomputeAll();
    return true;
}

QObject* DashConfigStore::presentation(const QString& signalName) const {
    return presentationObject(signalName);
}

SignalPresentation* DashConfigStore::presentationObject(const QString& signalName) const {
    return presentations_.value(signalName, nullptr);
}

QStringList DashConfigStore::configuredSignalNames() const {
    QStringList names = presentations_.keys();
    std::sort(names.begin(), names.end());
    return names;
}

void DashConfigStore::updateSamples(const QList<miata::data::SignalSample>& samples) {
    const qint64 now = clock_.nsecsElapsed();
    bool changed = false;
    for (const auto& sample : samples) {
        bool valid = false;
        const double value = sample.value.toDouble(&valid);
        if (valid && std::isfinite(value)) {
            dynamicValues_.insert(sample.canonicalName, {value, now});
            changed = true;
        }
    }
    if (changed) recomputeAll();
}

void DashConfigStore::recomputeAll() {
    for (auto* presentation : std::as_const(presentations_)) recompute(presentation);
}

void DashConfigStore::recompute(SignalPresentation* presentation) {
    bool changed = false;
    const qint64 now = clock_.nsecsElapsed();
    auto& condition = presentation->enableCondition_;
    if (condition.configured) {
        bool enabled = condition.fallback;
        const auto dynamic = dynamicValues_.constFind(condition.signalName);
        if (dynamic != dynamicValues_.cend()
            && now - dynamic->receiptNs <= qint64(condition.sourceStaleMs) * 1'000'000) {
            enabled = qFuzzyCompare(dynamic->value + 1.0, condition.equalsValue + 1.0);
        }
        if (enabled != condition.resolvedEnabled) {
            condition.resolvedEnabled = enabled;
            changed = true;
        }
    }
    for (int rawKind = 0; rawKind < 4; ++rawKind) {
        const auto kind = static_cast<ThresholdKind>(rawKind);
        const auto& rule = presentation->threshold(kind);
        if (!rule.configured) continue;
        bool valid = false;
        double value = 0.0;
        if (rule.constantValue) {
            valid = true;
            value = *rule.constantValue;
        } else {
            const auto dynamic = dynamicValues_.constFind(rule.signalName);
            if (dynamic != dynamicValues_.cend()
                && now - dynamic->receiptNs <= qint64(rule.sourceStaleMs) * 1'000'000) {
                valid = true;
                value = dynamic->value;
            } else if (rule.fallback) {
                valid = true;
                value = *rule.fallback;
            }
        }
        changed |= presentation->setResolved(kind, valid, value);
    }
    const auto& lowWarning = presentation->threshold(ThresholdKind::LowWarning);
    const auto& lowCaution = presentation->threshold(ThresholdKind::LowCaution);
    if (lowWarning.resolvedValid && lowCaution.resolvedValid
        && lowWarning.resolvedValue > lowCaution.resolvedValue) {
        changed |= presentation->setResolved(ThresholdKind::LowWarning, false, 0.0);
        changed |= presentation->setResolved(ThresholdKind::LowCaution, false, 0.0);
    }
    const auto& highCaution = presentation->threshold(ThresholdKind::HighCaution);
    const auto& highWarning = presentation->threshold(ThresholdKind::HighWarning);
    if (highCaution.resolvedValid && highWarning.resolvedValid
        && highCaution.resolvedValue > highWarning.resolvedValue) {
        changed |= presentation->setResolved(ThresholdKind::HighCaution, false, 0.0);
        changed |= presentation->setResolved(ThresholdKind::HighWarning, false, 0.0);
    }
    if (changed) {
        emit presentation->thresholdsChanged();
        emit thresholdsChanged(presentation->signalName());
    }
}

}  // namespace miata::dash
