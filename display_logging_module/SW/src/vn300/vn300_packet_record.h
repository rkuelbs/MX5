#pragma once

#include <QByteArray>
#include <QMetaType>
#include <QtTypes>

namespace miata::data {

struct Vn300PacketRecord {
    QByteArray packet;
    qint64 monotonicTimestampNs = 0;
};

}  // namespace miata::data

Q_DECLARE_METATYPE(miata::data::Vn300PacketRecord)
