#include "logging/decoded_log_policy.h"
#include "can/dbc_decoder.h"
#include "vn300/vn300_binary_parser.h"
#include "core/source_health_monitor.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class DecodedLogPolicyTest final : public QObject {
    Q_OBJECT

private slots:
    void appliesNativePeriodicAndOffGroups();
    void rejectsUnknownCanonicalSignal();
    void defaultConfigAssignsEveryDbcSignal();
};

void DecodedLogPolicyTest::appliesNativePeriodicAndOffGroups() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("logging.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"({
        "default_group": "off",
        "rate_groups": {
            "native": {"mode": "native"},
            "ten_hz": {"rate_hz": 10},
            "off": {"mode": "off"}
        },
        "signals": {
            "ECM.rpm": "native",
            "ECM.batt": "ten_hz"
        }
    })");
    file.close();

    miata::data::DecodedLogPolicy policy;
    QString error;
    QVERIFY2(
        policy.load(path, {QStringLiteral("ECM.rpm"), QStringLiteral("ECM.batt"),
                           QStringLiteral("ECM.clt")}, &error),
        qPrintable(error));
    QCOMPARE(policy.warnings().size(), 1);

    const auto sample = [](const QString& name, qint64 timestamp) {
        return miata::data::SignalSample{name, 1.0, {}, timestamp,
                                         miata::data::SignalSource::Can};
    };
    QVERIFY(policy.shouldLog(sample(QStringLiteral("ECM.rpm"), 0)));
    QVERIFY(policy.shouldLog(sample(QStringLiteral("ECM.rpm"), 1)));
    QVERIFY(policy.shouldLog(sample(QStringLiteral("ECM.batt"), 0)));
    QVERIFY(!policy.shouldLog(sample(QStringLiteral("ECM.batt"), 99'999'999)));
    QVERIFY(policy.shouldLog(sample(QStringLiteral("ECM.batt"), 100'000'000)));
    QVERIFY(!policy.shouldLog(sample(QStringLiteral("ECM.clt"), 100'000'000)));
}

void DecodedLogPolicyTest::rejectsUnknownCanonicalSignal() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("logging.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"({
        "default_group": "off",
        "rate_groups": {"off": {"mode": "off"}},
        "signals": {"ECM.typo": "off"}
    })");
    file.close();

    miata::data::DecodedLogPolicy policy;
    QString error;
    QVERIFY(!policy.load(path, {QStringLiteral("ECM.rpm")}, &error));
    QVERIFY(error.contains(QStringLiteral("unknown signal 'ECM.typo'")));
}

void DecodedLogPolicyTest::defaultConfigAssignsEveryDbcSignal() {
    miata::data::DbcDecoder decoder;
    QString error;
    QVERIFY2(decoder.load(QStringLiteral(MIATA_TEST_DBC_PATH), &error), qPrintable(error));

    miata::data::DecodedLogPolicy policy;
    QStringList names = decoder.canonicalSignalNames();
    names.append(miata::data::Vn300BinaryParser::canonicalSignalNames());
    names.append(miata::data::SourceHealthMonitor::canonicalSignalNames());
    QVERIFY2(
        policy.load(
            QStringLiteral(MIATA_TEST_LOGGING_CONFIG_PATH),
            names,
            &error),
        qPrintable(error));
    QVERIFY2(policy.warnings().isEmpty(), qPrintable(policy.warnings().join(QLatin1Char('\n'))));
}

QTEST_MAIN(DecodedLogPolicyTest)
#include "decoded_log_policy_test.moc"
