#include "core/source_health_monitor.h"

#include <QtTest>

class SourceHealthMonitorTest final : public QObject {
    Q_OBJECT

private slots:
    void transitionsFromFreshToStale();
};

namespace {
double valueOf(const QList<miata::data::SignalSample>& samples, const QString& name) {
    for (const auto& sample : samples) if (sample.canonicalName == name) return sample.value.toDouble();
    return -999.0;
}
}

void SourceHealthMonitorTest::transitionsFromFreshToStale() {
    miata::data::SourceHealthMonitor monitor;
    monitor.setTimeouts(500, 200);
    monitor.setCanState(true, true);
    monitor.setVn300State(true, true);
    monitor.noteCanReception(1'000);
    monitor.noteVn300Reception(1'000);

    auto samples = monitor.samples(1'150);
    QCOMPARE(valueOf(samples, QStringLiteral("LOGGER.can_healthy")), 1.0);
    QCOMPARE(valueOf(samples, QStringLiteral("LOGGER.vn300_healthy")), 1.0);
    samples = monitor.samples(1'250);
    QCOMPARE(valueOf(samples, QStringLiteral("LOGGER.can_healthy")), 1.0);
    QCOMPARE(valueOf(samples, QStringLiteral("LOGGER.vn300_healthy")), 0.0);
}

QTEST_MAIN(SourceHealthMonitorTest)
#include "source_health_monitor_test.moc"
