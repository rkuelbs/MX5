#include "backend/signal_filter_model.h"
#include "backend/signal_list_model.h"

#include <QtTest>

class DashSignalModelTest final : public QObject {
    Q_OBJECT

private slots:
    void exposesValuesFilteringSelectionAndFreshness();
};

void DashSignalModelTest::exposesValuesFilteringSelectionAndFreshness() {
    miata::dash::SignalListModel source;
    source.setStaleAfterMs(20);
    source.setDefinitions({
        {QStringLiteral("VN300.yaw"), QStringLiteral("deg")},
        {QStringLiteral("ECM.rpm"), QStringLiteral("rpm")},
    });
    QCOMPARE(source.rowCount(), 2);
    QCOMPARE(source.data(source.index(0), miata::dash::SignalListModel::SignalNameRole).toString(),
             QStringLiteral("ECM.rpm"));

    source.updateSamples({
        {QStringLiteral("ECM.rpm"), 4321.0, QStringLiteral("rpm"), 10,
         miata::data::SignalSource::Can},
        {QStringLiteral("VN300.yaw"), 92.25, QStringLiteral("deg"), 10,
         miata::data::SignalSource::Vn300},
    });
    QCOMPARE(source.data(source.index(0), miata::dash::SignalListModel::FormattedValueRole).toString(),
             QStringLiteral("4321"));
    QCOMPARE(source.data(source.index(1), miata::dash::SignalListModel::SourceNameRole).toString(),
             QStringLiteral("VN300"));
    QVERIFY(!source.data(source.index(0), miata::dash::SignalListModel::StaleRole).toBool());

    miata::dash::SignalFilterModel filtered;
    filtered.setSignalModel(&source);
    filtered.setFilterText(QStringLiteral("rpm"));
    QCOMPARE(filtered.rowCount(), 1);
    filtered.setSelected(QStringLiteral("ECM.rpm"), true);
    QVERIFY(source.data(source.index(0), miata::dash::SignalListModel::SelectedRole).toBool());

    QTest::qWait(30);
    QVERIFY(source.data(source.index(0), miata::dash::SignalListModel::StaleRole).toBool());
}

QTEST_MAIN(DashSignalModelTest)
#include "dash_signal_model_test.moc"
