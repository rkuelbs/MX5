#pragma once

#include "vn300/vn300_packet_record.h"

#include <QString>
#include <QStringView>

#include <optional>

namespace miata::data {

class Vn300LogCodec final {
public:
    // Text capture form: (seconds.nanoseconds) HEX_PACKET
    static std::optional<Vn300PacketRecord> parseLine(
        QStringView line,
        QString* errorMessage = nullptr,
        bool* ignored = nullptr);
    static QString formatLine(const Vn300PacketRecord& record);
};

}  // namespace miata::data
