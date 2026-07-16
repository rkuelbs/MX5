#pragma once

#include <QMetaType>

namespace miata::dash {

// Hardware, keyboard, replay, and automated test inputs all resolve to this
// small presentation-only action set before reaching navigation or warnings.
enum class DashAction {
    NavigateUp,
    NavigateDown,
    Activate,
    MenuBack,
};

}  // namespace miata::dash

Q_DECLARE_METATYPE(miata::dash::DashAction)
