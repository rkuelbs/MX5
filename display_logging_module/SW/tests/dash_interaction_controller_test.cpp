#include "backend/navigation_controller.h"
#include "backend/warning_manager.h"

#include <QSignalSpy>
#include <QtTest>

using miata::dash::DashAction;
using miata::dash::NavigationController;
using miata::dash::WarningManager;

class DashInteractionControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void navigatesPagesAndMenu();
    void warningLayerConsumesAndAcknowledgesActions();
};

void DashInteractionControllerTest::navigatesPagesAndMenu() {
    NavigationController navigation;
    QCOMPARE(navigation.pageIndex(), 0);

    navigation.handleAction(DashAction::NavigateUp);
    QCOMPARE(navigation.pageIndex(), 1);
    navigation.handleAction(DashAction::NavigateDown);
    QCOMPARE(navigation.pageIndex(), 0);

    navigation.handleAction(DashAction::MenuBack);
    QVERIFY(navigation.menuOpen());
    QCOMPARE(navigation.menuIndex(), 0);
    navigation.handleAction(DashAction::NavigateDown);
    QCOMPARE(navigation.menuIndex(), 1);
    navigation.handleAction(DashAction::Activate);
    QCOMPARE(navigation.pageIndex(), 1);
    QVERIFY(!navigation.menuOpen());

    navigation.handleAction(DashAction::MenuBack);
    navigation.handleAction(DashAction::MenuBack);
    QVERIFY(!navigation.menuOpen());
}

void DashInteractionControllerTest::warningLayerConsumesAndAcknowledgesActions() {
    WarningManager warnings;
    QSignalSpy acknowledgements(&warnings, &WarningManager::warningAcknowledged);

    warnings.raiseWarning(
        QStringLiteral("oil_pressure"), QStringLiteral("OIL PRESSURE"),
        QStringLiteral("Oil pressure is below its warning threshold."),
        QStringLiteral("critical"));
    QVERIFY(warnings.overlayVisible());
    QCOMPARE(warnings.activeCount(), 1);
    QCOMPARE(warnings.currentSeverity(), QStringLiteral("critical"));

    QVERIFY(warnings.handleAction(DashAction::MenuBack));
    QVERIFY(warnings.overlayVisible());
    QVERIFY(warnings.handleAction(DashAction::Activate));
    QVERIFY(!warnings.overlayVisible());
    QCOMPARE(warnings.activeCount(), 1);
    QCOMPARE(acknowledgements.size(), 1);

    QVERIFY(!warnings.handleAction(DashAction::NavigateDown));
    warnings.clearWarning(QStringLiteral("oil_pressure"));
    QCOMPARE(warnings.activeCount(), 0);
}

QTEST_MAIN(DashInteractionControllerTest)
#include "dash_interaction_controller_test.moc"
