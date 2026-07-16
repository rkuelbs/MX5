#pragma once

#include "backend/dash_action.h"

#include <QObject>
#include <QList>

namespace miata::dash {

class WarningManager final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool overlayVisible READ overlayVisible NOTIFY stateChanged)
    Q_PROPERTY(int activeCount READ activeCount NOTIFY stateChanged)
    Q_PROPERTY(QString currentTitle READ currentTitle NOTIFY stateChanged)
    Q_PROPERTY(QString currentMessage READ currentMessage NOTIFY stateChanged)
    Q_PROPERTY(QString currentSeverity READ currentSeverity NOTIFY stateChanged)

public:
    explicit WarningManager(QObject* parent = nullptr);

    [[nodiscard]] bool overlayVisible() const;
    [[nodiscard]] int activeCount() const;
    [[nodiscard]] QString currentTitle() const;
    [[nodiscard]] QString currentMessage() const;
    [[nodiscard]] QString currentSeverity() const;

    // Returns true when a visible warning consumes the action.
    bool handleAction(miata::dash::DashAction action);
    Q_INVOKABLE void raiseWarning(
        const QString& id, const QString& title, const QString& message,
        const QString& severity = QStringLiteral("warning"));
    Q_INVOKABLE void clearWarning(const QString& id);
    Q_INVOKABLE void acknowledgeCurrent();

signals:
    void stateChanged();
    void warningAcknowledged(const QString& id);

private:
    struct Warning {
        QString id;
        QString title;
        QString message;
        QString severity;
        bool acknowledged = false;
    };

    [[nodiscard]] QList<int> unacknowledgedIndices() const;
    [[nodiscard]] const Warning* currentWarning() const;
    void moveCurrent(int delta);

    QList<Warning> warnings_;
    int currentUnacknowledged_ = 0;
};

}  // namespace miata::dash
