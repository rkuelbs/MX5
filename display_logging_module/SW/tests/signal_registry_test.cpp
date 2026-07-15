#include "core/signal_registry.h"

#include <QtTest>

class SignalRegistryTest final : public QObject {
    Q_OBJECT

private slots:
    void rejectsOlderSamples();
    void reportsFreshnessFromSampleAge();
};

void SignalRegistryTest::rejectsOlderSamples() {
    miata::data::SignalRegistry registry;
    registry.update({QStringLiteral("ECM.rpm"), 3000.0, QStringLiteral("RPM"), 200,
                     miata::data::SignalSource::Can});
    registry.update({QStringLiteral("ECM.rpm"), 1000.0, QStringLiteral("RPM"), 100,
                     miata::data::SignalSource::Can});

    const auto current = registry.sample(QStringLiteral("ECM.rpm"));
    QVERIFY(current.has_value());
    QCOMPARE(current->value.toDouble(), 3000.0);
    QCOMPARE(current->monotonicTimestampNs, 200);
}

void SignalRegistryTest::reportsFreshnessFromSampleAge() {
    miata::data::SignalRegistry registry;
    registry.update({QStringLiteral("ECM.rpm"), 3000.0, QStringLiteral("RPM"), 1'000,
                     miata::data::SignalSource::Can});
    QCOMPARE(registry.ageNs(QStringLiteral("ECM.rpm"), 1'250), std::optional<qint64>(250));
    QVERIFY(registry.isFresh(QStringLiteral("ECM.rpm"), 1'250, 250));
    QVERIFY(!registry.isFresh(QStringLiteral("ECM.rpm"), 1'251, 250));
    QVERIFY(!registry.isFresh(QStringLiteral("ECM.missing"), 1'250, 250));
}

QTEST_MAIN(SignalRegistryTest)
#include "signal_registry_test.moc"
