#include "core/source_health_monitor.h"

#include <algorithm>

namespace miata::data {
namespace {

SignalSample sample(const QString& name, double value, const QString& unit, qint64 timestamp) {
    return {name, value, unit, timestamp, SignalSource::Derived};
}

double ageSeconds(qint64 now, qint64 last) {
    return last < 0 ? -1.0 : static_cast<double>(std::max<qint64>(0, now - last)) * 1e-9;
}

}  // namespace

void SourceHealthMonitor::setCanState(bool enabled, bool connected) {
    canEnabled_ = enabled;
    canConnected_ = connected;
}

void SourceHealthMonitor::setVn300State(bool enabled, bool connected) {
    vn300Enabled_ = enabled;
    vn300Connected_ = connected;
}

void SourceHealthMonitor::setTimeouts(qint64 canTimeoutNs, qint64 vn300TimeoutNs) {
    canTimeoutNs_ = std::max<qint64>(1, canTimeoutNs);
    vn300TimeoutNs_ = std::max<qint64>(1, vn300TimeoutNs);
}

void SourceHealthMonitor::noteCanReception(qint64 timestampNs) { lastCanRxNs_ = timestampNs; }
void SourceHealthMonitor::noteCanDecodeError() { ++canDecodeErrors_; }
void SourceHealthMonitor::noteVn300Reception(qint64 timestampNs) { lastVn300RxNs_ = timestampNs; }
void SourceHealthMonitor::setVn300ErrorCounts(quint64 crcErrors, quint64 formatErrors) {
    vn300CrcErrors_ = crcErrors;
    vn300FormatErrors_ = formatErrors;
}

QList<SignalSample> SourceHealthMonitor::samples(qint64 now) const {
    const bool canHealthy = !canEnabled_ || (canConnected_ && lastCanRxNs_ >= 0
        && now - lastCanRxNs_ <= canTimeoutNs_);
    const bool vnHealthy = !vn300Enabled_ || (vn300Connected_ && lastVn300RxNs_ >= 0
        && now - lastVn300RxNs_ <= vn300TimeoutNs_);
    return {
        sample(QStringLiteral("LOGGER.can_enabled"), canEnabled_, {}, now),
        sample(QStringLiteral("LOGGER.can_healthy"), canHealthy, {}, now),
        sample(QStringLiteral("LOGGER.can_rx_age"), ageSeconds(now, lastCanRxNs_), QStringLiteral("s"), now),
        sample(QStringLiteral("LOGGER.can_decode_errors"), static_cast<double>(canDecodeErrors_), {}, now),
        sample(QStringLiteral("LOGGER.vn300_enabled"), vn300Enabled_, {}, now),
        sample(QStringLiteral("LOGGER.vn300_healthy"), vnHealthy, {}, now),
        sample(QStringLiteral("LOGGER.vn300_rx_age"), ageSeconds(now, lastVn300RxNs_), QStringLiteral("s"), now),
        sample(QStringLiteral("LOGGER.vn300_crc_errors"), static_cast<double>(vn300CrcErrors_), {}, now),
        sample(QStringLiteral("LOGGER.vn300_format_errors"), static_cast<double>(vn300FormatErrors_), {}, now),
    };
}

QList<SignalDefinition> SourceHealthMonitor::signalDefinitions() {
    return {
        {QStringLiteral("LOGGER.can_enabled"), {}},
        {QStringLiteral("LOGGER.can_healthy"), {}},
        {QStringLiteral("LOGGER.can_rx_age"), QStringLiteral("s")},
        {QStringLiteral("LOGGER.can_decode_errors"), {}},
        {QStringLiteral("LOGGER.vn300_enabled"), {}},
        {QStringLiteral("LOGGER.vn300_healthy"), {}},
        {QStringLiteral("LOGGER.vn300_rx_age"), QStringLiteral("s")},
        {QStringLiteral("LOGGER.vn300_crc_errors"), {}},
        {QStringLiteral("LOGGER.vn300_format_errors"), {}},
    };
}

QStringList SourceHealthMonitor::canonicalSignalNames() {
    QStringList names;
    for (const auto& definition : signalDefinitions()) names.append(definition.canonicalName);
    return names;
}

}  // namespace miata::data
