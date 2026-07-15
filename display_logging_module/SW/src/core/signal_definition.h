#pragma once

#include <QString>
#include <QMetaType>

namespace miata::data {

struct SignalDefinition {
    QString canonicalName;
    QString unit;
};

}  // namespace miata::data

Q_DECLARE_METATYPE(miata::data::SignalDefinition)
