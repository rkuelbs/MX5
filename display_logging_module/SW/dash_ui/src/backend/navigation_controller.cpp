#include "backend/navigation_controller.h"

namespace miata::dash {

NavigationController::NavigationController(QObject* parent) : QObject(parent) {}

int NavigationController::pageIndex() const { return pageIndex_; }
int NavigationController::menuIndex() const { return menuIndex_; }
bool NavigationController::menuOpen() const { return menuOpen_; }
QStringList NavigationController::pageTitles() const { return pageTitles_; }

void NavigationController::handleAction(DashAction action) {
    if (menuOpen_) {
        switch (action) {
        case DashAction::NavigateUp:
            setMenuIndex((menuIndex_ - 1 + pageTitles_.size()) % pageTitles_.size());
            return;
        case DashAction::NavigateDown:
            setMenuIndex((menuIndex_ + 1) % pageTitles_.size());
            return;
        case DashAction::Activate:
            activateMenuIndex(menuIndex_);
            return;
        case DashAction::MenuBack:
            closeMenu();
            return;
        }
    }

    switch (action) {
    case DashAction::NavigateUp:
        setPageIndex((pageIndex_ - 1 + pageTitles_.size()) % pageTitles_.size());
        break;
    case DashAction::NavigateDown:
        setPageIndex((pageIndex_ + 1) % pageTitles_.size());
        break;
    case DashAction::MenuBack:
        setMenuIndex(pageIndex_);
        setMenuOpen(true);
        break;
    case DashAction::Activate:
        break;
    }
}

void NavigationController::activateMenuIndex(int index) {
    if (index < 0 || index >= pageTitles_.size()) return;
    setPageIndex(index);
    setMenuIndex(index);
    setMenuOpen(false);
}

void NavigationController::closeMenu() { setMenuOpen(false); }

void NavigationController::setPageIndex(int index) {
    if (index == pageIndex_) return;
    pageIndex_ = index;
    emit pageIndexChanged();
}

void NavigationController::setMenuIndex(int index) {
    if (index == menuIndex_) return;
    menuIndex_ = index;
    emit menuIndexChanged();
}

void NavigationController::setMenuOpen(bool open) {
    if (open == menuOpen_) return;
    menuOpen_ = open;
    emit menuOpenChanged();
}

}  // namespace miata::dash
