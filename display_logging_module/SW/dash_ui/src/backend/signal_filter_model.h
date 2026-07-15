#pragma once

#include "backend/signal_list_model.h"

#include <QSortFilterProxyModel>

namespace miata::dash {

class SignalFilterModel final : public QSortFilterProxyModel {
    Q_OBJECT
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    explicit SignalFilterModel(QObject* parent = nullptr);

    [[nodiscard]] QString filterText() const;
    void setFilterText(const QString& text);
    void setSignalModel(SignalListModel* model);

    Q_INVOKABLE void setSelected(const QString& canonicalName, bool selected);

signals:
    void filterTextChanged();
    void countChanged();

private:
    SignalListModel* signalModel_ = nullptr;
    QString filterText_;
};

}  // namespace miata::dash
