#include "backend/signal_list_model.h"

#include <algorithm>
#include <cmath>

namespace miata::dash {
namespace {

QString sourceName(miata::data::SignalSource source) {
    switch (source) {
    case miata::data::SignalSource::Can: return QStringLiteral("CAN");
    case miata::data::SignalSource::Vn300: return QStringLiteral("VN300");
    case miata::data::SignalSource::Replay: return QStringLiteral("Replay");
    case miata::data::SignalSource::Derived: return QStringLiteral("Derived");
    }
    return QStringLiteral("Unknown");
}

bool isIntegerLike(const QString& name) {
    return name.endsWith(QStringLiteral("status"), Qt::CaseInsensitive)
        || name.endsWith(QStringLiteral("count"), Qt::CaseInsensitive)
        || name.endsWith(QStringLiteral("errors"), Qt::CaseInsensitive)
        || name.endsWith(QStringLiteral("enabled"), Qt::CaseInsensitive)
        || name.endsWith(QStringLiteral("healthy"), Qt::CaseInsensitive);
}

}  // namespace

SignalListModel::SignalListModel(QObject* parent) : QAbstractListModel(parent) {
    localClock_.start();
    freshnessTimer_.setInterval(100);
    connect(&freshnessTimer_, &QTimer::timeout, this, &SignalListModel::refreshAges);
    freshnessTimer_.start();
}

int SignalListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : rows_.size();
}

QVariant SignalListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rows_.size()) return {};
    const Row& row = rows_.at(index.row());
    const qint64 currentAgeMs = ageMs(row);
    switch (role) {
    case SignalNameRole: return row.definition.canonicalName;
    case ValueRole: return row.sample ? row.sample->value : QVariant{};
    case FormattedValueRole: return formattedValue(row);
    case UnitRole: return row.sample && !row.sample->unit.isEmpty()
        ? row.sample->unit : row.definition.unit;
    case SourceNameRole: return row.sample ? sourceName(row.sample->source) : QStringLiteral("—");
    case AgeMsRole: return currentAgeMs;
    case AgeTextRole:
        if (currentAgeMs < 0) return QStringLiteral("never");
        if (currentAgeMs < 1000) return QStringLiteral("%1 ms").arg(currentAgeMs);
        return QStringLiteral("%1 s").arg(currentAgeMs / 1000.0, 0, 'f', 1);
    case StaleRole: return currentAgeMs < 0 || currentAgeMs > staleAfterMs_;
    case SelectedRole: return row.selected;
    default: return {};
    }
}

QHash<int, QByteArray> SignalListModel::roleNames() const {
    return {
        {SignalNameRole, "signalName"},
        {ValueRole, "signalValue"},
        {FormattedValueRole, "formattedValue"},
        {UnitRole, "unit"},
        {SourceNameRole, "sourceName"},
        {AgeMsRole, "ageMs"},
        {AgeTextRole, "ageText"},
        {StaleRole, "stale"},
        {SelectedRole, "selected"},
    };
}

int SignalListModel::staleAfterMs() const { return staleAfterMs_; }

void SignalListModel::setStaleAfterMs(int milliseconds) {
    const int bounded = std::max(1, milliseconds);
    if (bounded == staleAfterMs_) return;
    staleAfterMs_ = bounded;
    emit staleAfterMsChanged();
    refreshAges();
}

void SignalListModel::setDefinitions(
    const QList<miata::data::SignalDefinition>& definitions) {
    QHash<QString, Row> previous;
    for (const Row& row : std::as_const(rows_)) previous.insert(row.definition.canonicalName, row);

    QList<miata::data::SignalDefinition> sorted = definitions;
    std::sort(sorted.begin(), sorted.end(), [](const auto& left, const auto& right) {
        return left.canonicalName < right.canonicalName;
    });
    sorted.erase(std::unique(sorted.begin(), sorted.end(), [](const auto& left, const auto& right) {
        return left.canonicalName == right.canonicalName;
    }), sorted.end());

    beginResetModel();
    rows_.clear();
    for (const auto& definition : sorted) {
        Row row = previous.value(definition.canonicalName);
        row.definition = definition;
        rows_.append(row);
    }
    rebuildIndex();
    endResetModel();
}

void SignalListModel::updateSamples(const QList<miata::data::SignalSample>& samples) {
    QList<miata::data::SignalDefinition> unknownDefinitions;
    for (const auto& sample : samples) {
        if (!sample.canonicalName.isEmpty() && !rowByName_.contains(sample.canonicalName)) {
            unknownDefinitions.append({sample.canonicalName, sample.unit});
        }
    }
    if (!unknownDefinitions.isEmpty()) {
        std::sort(unknownDefinitions.begin(), unknownDefinitions.end(), [](const auto& left, const auto& right) {
            return left.canonicalName < right.canonicalName;
        });
        unknownDefinitions.erase(
            std::unique(unknownDefinitions.begin(), unknownDefinitions.end(), [](const auto& left, const auto& right) {
                return left.canonicalName == right.canonicalName;
            }),
            unknownDefinitions.end());
        beginResetModel();
        for (const auto& definition : unknownDefinitions) {
            if (!rowByName_.contains(definition.canonicalName))
                rows_.append({definition, std::nullopt, -1, false});
        }
        std::sort(rows_.begin(), rows_.end(), [](const Row& left, const Row& right) {
            return left.definition.canonicalName < right.definition.canonicalName;
        });
        rebuildIndex();
        endResetModel();
    }

    int minimumRow = rows_.size();
    int maximumRow = -1;
    const qint64 receiptTime = localClock_.nsecsElapsed();
    for (const auto& sample : samples) {
        const auto found = rowByName_.constFind(sample.canonicalName);
        if (found == rowByName_.cend()) continue;
        Row& row = rows_[*found];
        row.sample = sample;
        row.localReceiptNs = receiptTime;
        minimumRow = std::min(minimumRow, *found);
        maximumRow = std::max(maximumRow, *found);
    }
    if (maximumRow >= minimumRow) {
        emit dataChanged(index(minimumRow), index(maximumRow),
                         {ValueRole, FormattedValueRole, UnitRole, SourceNameRole,
                          AgeMsRole, AgeTextRole, StaleRole});
    }
}

void SignalListModel::setSelected(const QString& canonicalName, bool selected) {
    const auto found = rowByName_.constFind(canonicalName);
    if (found == rowByName_.cend() || rows_[*found].selected == selected) return;
    rows_[*found].selected = selected;
    emit dataChanged(index(*found), index(*found), {SelectedRole});
}

qint64 SignalListModel::ageMs(const Row& row) const {
    if (row.localReceiptNs < 0) return -1;
    return std::max<qint64>(0, localClock_.nsecsElapsed() - row.localReceiptNs) / 1'000'000;
}

QString SignalListModel::formattedValue(const Row& row) const {
    if (!row.sample) return QStringLiteral("—");
    bool numeric = false;
    const double value = row.sample->value.toDouble(&numeric);
    if (!numeric || !std::isfinite(value)) return row.sample->value.toString();
    if (isIntegerLike(row.definition.canonicalName)) return QString::number(std::llround(value));
    const double magnitude = std::abs(value);
    const int precision = magnitude >= 1000.0 ? 0 : magnitude >= 100.0 ? 1 : magnitude >= 10.0 ? 2 : 3;
    return QString::number(value, 'f', precision);
}

void SignalListModel::rebuildIndex() {
    rowByName_.clear();
    for (int row = 0; row < rows_.size(); ++row)
        rowByName_.insert(rows_.at(row).definition.canonicalName, row);
}

void SignalListModel::refreshAges() {
    if (rows_.isEmpty()) return;
    emit dataChanged(index(0), index(rows_.size() - 1), {AgeMsRole, AgeTextRole, StaleRole});
}

}  // namespace miata::dash
