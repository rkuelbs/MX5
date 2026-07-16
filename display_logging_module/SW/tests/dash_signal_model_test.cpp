#include "backend/signal_filter_model.h"
#include "backend/signal_list_model.h"
#include "backend/signal_history_store.h"

#include <QtTest>

class DashSignalModelTest final : public QObject {
    Q_OBJECT

private slots:
    void exposesValuesFilteringSelectionAndFreshness();
    void buffersOnlySelectedNumericHistory();
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

void DashSignalModelTest::buffersOnlySelectedNumericHistory() {
    miata::dash::SignalHistoryStore history;
    QCOMPARE(history.maxSignals(), 4);
    history.setSelectedSignals({QStringLiteral("ECM.rpm"), QStringLiteral("ECM.clt")});
    history.updateSamples({
        {QStringLiteral("ECM.rpm"), 4000.0, QStringLiteral("rpm"), 1,
         miata::data::SignalSource::Can},
        {QStringLiteral("ECM.clt"), 195.0, QStringLiteral("deg F"), 1,
         miata::data::SignalSource::Can},
        {QStringLiteral("ECM.map"), 120.0, QStringLiteral("kPa"), 1,
         miata::data::SignalSource::Can},
    });
    const auto traces = history.series();
    QCOMPARE(traces.size(), 2);
    const auto rpm = traces.front().toMap();
    QCOMPARE(rpm.value(QStringLiteral("name")).toString(), QStringLiteral("ECM.rpm"));
    QCOMPARE(rpm.value(QStringLiteral("unit")).toString(), QStringLiteral("rpm"));
    QCOMPARE(rpm.value(QStringLiteral("points")).toList().size(), 1);

    history.setWindowSeconds(1);
    QCOMPARE(history.windowSeconds(), 2);
    history.clear();
    QCOMPARE(history.series().front().toMap()
                 .value(QStringLiteral("points")).toList().size(), 0);
}

QTEST_MAIN(DashSignalModelTest)
#include "dash_signal_model_test.moc"
