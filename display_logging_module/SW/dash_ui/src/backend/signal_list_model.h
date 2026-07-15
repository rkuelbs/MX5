#pragma once

#include "core/signal_definition.h"
#include "core/signal_sample.h"

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QHash>
#include <QTimer>

#include <optional>

namespace miata::dash {

class SignalListModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int staleAfterMs READ staleAfterMs WRITE setStaleAfterMs NOTIFY staleAfterMsChanged)

public:
    enum Role {
        SignalNameRole = Qt::UserRole + 1,
        ValueRole,
        FormattedValueRole,
        UnitRole,
        SourceNameRole,
        AgeMsRole,
        AgeTextRole,
        StaleRole,
        SelectedRole,
    };
    Q_ENUM(Role)

    explicit SignalListModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int staleAfterMs() const;
    void setStaleAfterMs(int milliseconds);

public slots:
    void setDefinitions(const QList<miata::data::SignalDefinition>& definitions);
    void updateSamples(const QList<miata::data::SignalSample>& samples);
    void setSelected(const QString& canonicalName, bool selected);

signals:
    void staleAfterMsChanged();

private:
    struct Row {
        miata::data::SignalDefinition definition;
        std::optional<miata::data::SignalSample> sample;
        qint64 localReceiptNs = -1;
        bool selected = false;
    };

    [[nodiscard]] qint64 ageMs(const Row& row) const;
    [[nodiscard]] QString formattedValue(const Row& row) const;
    void rebuildIndex();
    void refreshAges();

    QList<Row> rows_;
    QHash<QString, int> rowByName_;
    QElapsedTimer localClock_;
    QTimer freshnessTimer_;
    int staleAfterMs_ = 1000;
};

}  // namespace miata::dash
