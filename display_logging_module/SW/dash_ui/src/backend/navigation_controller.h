#pragma once

#include "backend/dash_action.h"

#include <QObject>
#include <QStringList>

namespace miata::dash {

class NavigationController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int pageIndex READ pageIndex NOTIFY pageIndexChanged)
    Q_PROPERTY(int menuIndex READ menuIndex NOTIFY menuIndexChanged)
    Q_PROPERTY(bool menuOpen READ menuOpen NOTIFY menuOpenChanged)
    Q_PROPERTY(QStringList pageTitles READ pageTitles CONSTANT)

public:
    explicit NavigationController(QObject* parent = nullptr);

    [[nodiscard]] int pageIndex() const;
    [[nodiscard]] int menuIndex() const;
    [[nodiscard]] bool menuOpen() const;
    [[nodiscard]] QStringList pageTitles() const;

    void handleAction(miata::dash::DashAction action);
    Q_INVOKABLE void activateMenuIndex(int index);
    Q_INVOKABLE void closeMenu();

signals:
    void pageIndexChanged();
    void menuIndexChanged();
    void menuOpenChanged();

private:
    void setPageIndex(int index);
    void setMenuIndex(int index);
    void setMenuOpen(bool open);

    QStringList pageTitles_{QStringLiteral("Drive"), QStringLiteral("Diagnostics")};
    int pageIndex_ = 0;
    int menuIndex_ = 0;
    bool menuOpen_ = false;
};

}  // namespace miata::dash
