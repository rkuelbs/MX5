#include "backend/warning_manager.h"

#include <algorithm>

namespace miata::dash {
namespace {

int severityRank(const QString& severity) {
    if (severity == QStringLiteral("critical")) return 2;
    if (severity == QStringLiteral("warning")) return 1;
    return 0;
}

}  // namespace

WarningManager::WarningManager(QObject* parent) : QObject(parent) {}

bool WarningManager::overlayVisible() const { return currentWarning() != nullptr; }
int WarningManager::activeCount() const { return warnings_.size(); }

QString WarningManager::currentTitle() const {
    const auto* warning = currentWarning();
    return warning ? warning->title : QString{};
}

QString WarningManager::currentMessage() const {
    const auto* warning = currentWarning();
    return warning ? warning->message : QString{};
}

QString WarningManager::currentSeverity() const {
    const auto* warning = currentWarning();
    return warning ? warning->severity : QString{};
}

bool WarningManager::handleAction(DashAction action) {
    if (!overlayVisible()) return false;
    switch (action) {
    case DashAction::NavigateUp:
        moveCurrent(-1);
        break;
    case DashAction::NavigateDown:
        moveCurrent(1);
        break;
    case DashAction::Activate:
        acknowledgeCurrent();
        break;
    case DashAction::MenuBack:
        break;  // An unacknowledged warning remains the active interaction layer.
    }
    return true;
}

void WarningManager::raiseWarning(
    const QString& id, const QString& title, const QString& message, const QString& severity) {
    if (id.isEmpty()) return;
    const auto existing = std::find_if(warnings_.begin(), warnings_.end(), [&id](const auto& item) {
        return item.id == id;
    });
    if (existing == warnings_.end()) {
        warnings_.append({id, title, message, severity, false});
    } else {
        const bool escalated = severityRank(severity) > severityRank(existing->severity);
        existing->title = title;
        existing->message = message;
        existing->severity = severity;
        if (escalated) existing->acknowledged = false;
    }
    const auto pending = unacknowledgedIndices();
    const int pendingCount = static_cast<int>(pending.size());
    currentUnacknowledged_ = std::max(0, pendingCount - 1);
    emit stateChanged();
}

void WarningManager::clearWarning(const QString& id) {
    const auto previousSize = warnings_.size();
    warnings_.erase(std::remove_if(warnings_.begin(), warnings_.end(), [&id](const auto& item) {
        return item.id == id;
    }), warnings_.end());
    if (warnings_.size() == previousSize) return;
    const auto pending = unacknowledgedIndices();
    const int pendingCount = static_cast<int>(pending.size());
    currentUnacknowledged_ = pending.isEmpty()
        ? 0 : std::min(currentUnacknowledged_, pendingCount - 1);
    emit stateChanged();
}

void WarningManager::acknowledgeCurrent() {
    const auto pending = unacknowledgedIndices();
    if (pending.isEmpty()) return;
    const int pendingCount = static_cast<int>(pending.size());
    const int pendingIndex = std::clamp(currentUnacknowledged_, 0, pendingCount - 1);
    auto& warning = warnings_[pending[pendingIndex]];
    warning.acknowledged = true;
    emit warningAcknowledged(warning.id);
    const auto remaining = unacknowledgedIndices();
    const int remainingCount = static_cast<int>(remaining.size());
    currentUnacknowledged_ = remaining.isEmpty()
        ? 0 : std::min(pendingIndex, remainingCount - 1);
    emit stateChanged();
}

QList<int> WarningManager::unacknowledgedIndices() const {
    QList<int> indices;
    for (int index = 0; index < warnings_.size(); ++index)
        if (!warnings_[index].acknowledged) indices.append(index);
    return indices;
}

const WarningManager::Warning* WarningManager::currentWarning() const {
    const auto pending = unacknowledgedIndices();
    if (pending.isEmpty()) return nullptr;
    const int pendingCount = static_cast<int>(pending.size());
    const int index = std::clamp(currentUnacknowledged_, 0, pendingCount - 1);
    return &warnings_[pending[index]];
}

void WarningManager::moveCurrent(int delta) {
    const int count = unacknowledgedIndices().size();
    if (count < 2) return;
    currentUnacknowledged_ = (currentUnacknowledged_ + delta + count) % count;
    emit stateChanged();
}

}  // namespace miata::dash
