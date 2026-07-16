#include "backend/dash_config_store.h"
#include "backend/signal_value_provider.h"
#include "backend/warning_evaluator.h"
#include "backend/warning_manager.h"

#include <QTemporaryFile>
#include <QtTest>

class DashConfigurationTest final : public QObject {
    Q_OBJECT

private slots:
    void loadsGaugeAndStaticThresholdMetadata();
    void resolvesDynamicThresholdAndFallback();
    void exposesStableLiveSignalObjects();
    void evaluatesCautionEscalationHysteresisAndClear();
    void raisesClearsAndRearmsFreshnessWarnings();
    void rejectsInvalidFreshnessPolicy();
    void gatesWarningsWithAControlSignal();
};

namespace {

bool loadJson(miata::dash::DashConfigStore* store, const QByteArray& json, QString* error) {
    QTemporaryFile file;
    if (!file.open()) return false;
    file.write(json);
    file.flush();
    return store->load(file.fileName(), error);
}

QByteArray singleSignalConfig(const QByteArray& thresholds) {
    return QByteArray(R"JSON({
      "schema_version": 1,
      "signals": {
        "TEST.value": {
          "display_name": "Test Value",
          "precision": 1,
          "gauge": {"minimum": 0, "maximum": 30, "major_step": 5, "minor_step": 1},
          "thresholds": )JSON") + thresholds + QByteArray(R"JSON(
        }
      }
    })JSON");
}

QByteArray freshnessConfig(const QByteArray& freshness) {
    return QByteArray(R"JSON({
      "schema_version": 1,
      "signals": {
        "TEST.value": {
          "display_name": "Test Value",
          "precision": 1,
          "gauge": {"minimum": 0, "maximum": 30, "major_step": 5, "minor_step": 1},
          "freshness": )JSON") + freshness + QByteArray(R"JSON(
        }
      }
    })JSON");
}

QByteArray gatedFreshnessConfig() {
    return QByteArray(R"JSON({
      "schema_version": 1,
      "signals": {
        "TEST.value": {
          "display_name": "Test Value",
          "precision": 1,
          "gauge": {"minimum": 0, "maximum": 30, "major_step": 5, "minor_step": 1},
          "freshness": {"stale_after_ms": 20, "severity": "critical"},
          "enabled_when": {
            "signal": "PDM.test_source_powered",
            "equals": 1,
            "fallback": false,
            "source_stale_ms": 200
          }
        }
      }
    })JSON");
}

}  // namespace

void DashConfigurationTest::loadsGaugeAndStaticThresholdMetadata() {
    miata::dash::DashConfigStore store;
    QString error;
    QVERIFY2(store.load(QStringLiteral(MIATA_TEST_DASH_CONFIG_PATH), &error), qPrintable(error));
    auto* coolant = store.presentationObject(QStringLiteral("ECM.clt"));
    QVERIFY(coolant);
    QCOMPARE(coolant->displayName(), QStringLiteral("Coolant"));
    QCOMPARE(coolant->gaugeMinimum(), 100.0);
    QCOMPARE(coolant->gaugeMaximum(), 260.0);
    QVERIFY(coolant->highCautionValid());
    QCOMPARE(coolant->highCaution(), 220.0);
    QVERIFY(coolant->highWarningValid());
    QCOMPARE(coolant->highWarning(), 240.0);
}

void DashConfigurationTest::resolvesDynamicThresholdAndFallback() {
    miata::dash::DashConfigStore store;
    QString error;
    QVERIFY2(loadJson(&store, singleSignalConfig(R"JSON({
      "high_caution": {
        "signal": "PDM.boost_caution_limit",
        "fallback": 12,
        "source_stale_ms": 20
      }
    })JSON"), &error), qPrintable(error));
    auto* presentation = store.presentationObject(QStringLiteral("TEST.value"));
    QVERIFY(presentation);
    QCOMPARE(presentation->highCaution(), 12.0);

    store.updateSamples({
        {QStringLiteral("PDM.boost_caution_limit"), 18.0, {}, 1,
         miata::data::SignalSource::Can},
    });
    QCOMPARE(presentation->highCaution(), 18.0);
    QTRY_COMPARE_WITH_TIMEOUT(presentation->highCaution(), 12.0, 1000);
}

void DashConfigurationTest::exposesStableLiveSignalObjects() {
    miata::dash::SignalValueProvider provider;
    provider.setStaleAfterMs(1000);
    provider.setSignalStaleAfterMs(QStringLiteral("ECM.rpm"), 20);
    auto* first = provider.channelObject(QStringLiteral("ECM.rpm"));
    auto* second = provider.channelObject(QStringLiteral("ECM.rpm"));
    QCOMPARE(first, second);
    QVERIFY(!first->valid());

    provider.setDefinitions({{QStringLiteral("ECM.rpm"), QStringLiteral("RPM")}});
    provider.updateSamples({
        {QStringLiteral("ECM.rpm"), 4500.0, QStringLiteral("RPM"), 1,
         miata::data::SignalSource::Can},
    });
    QCOMPARE(first->value().toDouble(), 4500.0);
    QCOMPARE(first->unit(), QStringLiteral("RPM"));
    QVERIFY(!first->stale());
    QTRY_VERIFY_WITH_TIMEOUT(first->stale(), 1000);
}

void DashConfigurationTest::evaluatesCautionEscalationHysteresisAndClear() {
    miata::dash::DashConfigStore store;
    QString error;
    QVERIFY2(loadJson(&store, singleSignalConfig(R"JSON({
      "high_caution": {"value": 10, "hysteresis": 2},
      "high_warning": {"value": 20, "hysteresis": 2}
    })JSON"), &error), qPrintable(error));
    miata::dash::WarningManager warnings;
    miata::dash::WarningEvaluator evaluator(&store, &warnings);
    const auto send = [&evaluator](double value) {
        evaluator.processSamples({
            {QStringLiteral("TEST.value"), value, QStringLiteral("psi"), 1,
             miata::data::SignalSource::Can},
        });
    };

    send(11.0);
    QTRY_COMPARE(warnings.currentSeverity(), QStringLiteral("warning"));
    warnings.acknowledgeCurrent();
    QVERIFY(!warnings.overlayVisible());

    send(21.0);
    QTRY_COMPARE(warnings.currentSeverity(), QStringLiteral("critical"));
    QVERIFY(warnings.overlayVisible());

    send(19.0);  // Warning remains active inside its 2 psi hysteresis band.
    QCOMPARE(warnings.currentSeverity(), QStringLiteral("critical"));
    send(17.0);
    QTRY_COMPARE(warnings.currentSeverity(), QStringLiteral("warning"));
    send(7.0);
    QTRY_COMPARE(warnings.activeCount(), 0);
}

void DashConfigurationTest::raisesClearsAndRearmsFreshnessWarnings() {
    miata::dash::DashConfigStore store;
    QString error;
    QVERIFY2(loadJson(&store, freshnessConfig(R"JSON({
      "stale_after_ms": 20,
      "activation_delay_ms": 20,
      "clear_delay_ms": 20,
      "severity": "critical"
    })JSON"), &error), qPrintable(error));
    auto* presentation = store.presentationObject(QStringLiteral("TEST.value"));
    QVERIFY(presentation);
    QVERIFY(presentation->freshnessConfigured());
    QCOMPARE(presentation->staleAfterMs(), 20);

    miata::dash::WarningManager warnings;
    miata::dash::WarningEvaluator evaluator(&store, &warnings);
    QTRY_COMPARE_WITH_TIMEOUT(warnings.currentSeverity(), QStringLiteral("critical"), 1000);
    QVERIFY(warnings.currentTitle().contains(QStringLiteral("DATA STALE")));

    warnings.acknowledgeCurrent();
    QVERIFY(!warnings.overlayVisible());
    const auto sendFresh = [&evaluator] {
        evaluator.processSamples({
            {QStringLiteral("TEST.value"), 12.0, QStringLiteral("psi"), 1,
             miata::data::SignalSource::Can},
        });
    };
    sendFresh();
    QTest::qWait(12);
    sendFresh();
    QTest::qWait(12);
    sendFresh();
    QTRY_COMPARE_WITH_TIMEOUT(warnings.activeCount(), 0, 1000);

    // A later loss is a new warning and must not inherit the old acknowledgement.
    QTRY_VERIFY_WITH_TIMEOUT(warnings.overlayVisible(), 1000);
    QCOMPARE(warnings.currentSeverity(), QStringLiteral("critical"));
}

void DashConfigurationTest::rejectsInvalidFreshnessPolicy() {
    miata::dash::DashConfigStore store;
    QString error;
    QVERIFY(!loadJson(&store, freshnessConfig(R"JSON({
      "stale_after_ms": 0,
      "severity": "notice"
    })JSON"), &error));
    QVERIFY(error.contains(QStringLiteral("freshness")));
}

void DashConfigurationTest::gatesWarningsWithAControlSignal() {
    miata::dash::DashConfigStore store;
    QString error;
    QVERIFY2(loadJson(&store, gatedFreshnessConfig(), &error), qPrintable(error));
    auto* presentation = store.presentationObject(QStringLiteral("TEST.value"));
    QVERIFY(presentation);
    QVERIFY(!presentation->warningEnabled());

    miata::dash::WarningManager warnings;
    miata::dash::WarningEvaluator evaluator(&store, &warnings);
    QTest::qWait(100);
    QCOMPARE(warnings.activeCount(), 0);

    const auto setPower = [&](double powered) {
        const QList<miata::data::SignalSample> samples{
            {QStringLiteral("PDM.test_source_powered"), powered, {}, 1,
             miata::data::SignalSource::Can},
        };
        store.updateSamples(samples);
        evaluator.processSamples(samples);
    };
    setPower(1.0);
    QVERIFY(presentation->warningEnabled());
    QTRY_VERIFY_WITH_TIMEOUT(warnings.overlayVisible(), 1000);

    const QList<miata::data::SignalSample> freshValue{
        {QStringLiteral("TEST.value"), 12.0, QStringLiteral("psi"), 2,
         miata::data::SignalSource::Can},
    };
    store.updateSamples(freshValue);
    evaluator.processSamples(freshValue);
    QTRY_COMPARE_WITH_TIMEOUT(warnings.activeCount(), 0, 1000);

    QTest::qWait(25);
    setPower(1.0);  // PDM power-state messages are periodic while the source is enabled.
    QTRY_VERIFY_WITH_TIMEOUT(warnings.overlayVisible(), 1000);
    setPower(0.0);
    QVERIFY(!presentation->warningEnabled());
    QTRY_COMPARE_WITH_TIMEOUT(warnings.activeCount(), 0, 1000);
}

QTEST_MAIN(DashConfigurationTest)
#include "dash_configuration_test.moc"
