#include "platform/systemd_notifier.h"

#include <QtTest>

class SystemdNotifierTest final : public QObject {
    Q_OBJECT

private slots:
    void parsesWatchdogEnvironment();
};

void SystemdNotifierTest::parsesWatchdogEnvironment() {
    using miata::platform::SystemdNotifier;
    QCOMPARE(SystemdNotifier::watchdogPeriodMs({}, {}, 42), 0);
    QCOMPARE(SystemdNotifier::watchdogPeriodMs("invalid", {}, 42), 0);
    QCOMPARE(SystemdNotifier::watchdogPeriodMs("5000000", {}, 42), 2500);
    QCOMPARE(SystemdNotifier::watchdogPeriodMs("5000000", "42", 42), 2500);
    QCOMPARE(SystemdNotifier::watchdogPeriodMs("5000000", "41", 42), 0);
    QCOMPARE(SystemdNotifier::watchdogPeriodMs("1000", {}, 42), 1);
}

QTEST_MAIN(SystemdNotifierTest)
#include "systemd_notifier_test.moc"
