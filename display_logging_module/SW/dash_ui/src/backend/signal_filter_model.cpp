#include "backend/signal_filter_model.h"

namespace miata::dash {

SignalFilterModel::SignalFilterModel(QObject* parent) : QSortFilterProxyModel(parent) {
    setFilterRole(SignalListModel::SignalNameRole);
    setFilterCaseSensitivity(Qt::CaseInsensitive);
    setDynamicSortFilter(true);
    connect(this, &QAbstractItemModel::modelReset, this, &SignalFilterModel::countChanged);
    connect(this, &QAbstractItemModel::rowsInserted, this, &SignalFilterModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &SignalFilterModel::countChanged);
}

QString SignalFilterModel::filterText() const { return filterText_; }

void SignalFilterModel::setFilterText(const QString& text) {
    if (text == filterText_) return;
    filterText_ = text;
    setFilterFixedString(text);
    emit filterTextChanged();
    emit countChanged();
}

void SignalFilterModel::setSignalModel(SignalListModel* model) {
    signalModel_ = model;
    setSourceModel(model);
    emit countChanged();
}

void SignalFilterModel::setSelected(const QString& canonicalName, bool selected) {
    if (signalModel_) signalModel_->setSelected(canonicalName, selected);
}

}  // namespace miata::dash
