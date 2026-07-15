#pragma once

#include <QCanBusFrame>
#include <QMetaType>
#include <QString>

namespace miata::data {

struct CanFrameRecord {
    QCanBusFrame frame;
    qint64 monotonicTimestampNs = 0;
    QString interfaceName;
};

}  // namespace miata::data

Q_DECLARE_METATYPE(miata::data::CanFrameRecord)
