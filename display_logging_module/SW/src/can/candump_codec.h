#pragma once

#include "can/can_frame_record.h"

#include <QString>
#include <QStringView>

#include <optional>

namespace miata::data {

class CandumpCodec final {
public:
    // Parses the common candump form: (seconds.nanoseconds) interface ID#DATA.
    // Blank lines and lines beginning with ';' are reported as ignored.
    static std::optional<CanFrameRecord> parseLine(
        QStringView line,
        QString* errorMessage = nullptr,
        bool* ignored = nullptr);

    static QString formatLine(const CanFrameRecord& record);
};

}  // namespace miata::data
