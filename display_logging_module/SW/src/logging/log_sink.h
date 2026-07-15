#pragma once

#include "can/can_frame_record.h"
#include "core/signal_sample.h"

#include <QString>

namespace miata::data {

class LogSink {
public:
    virtual ~LogSink() = default;

    virtual bool writeRawFrame(
        const CanFrameRecord& record,
        QString* errorMessage = nullptr) = 0;
    virtual bool writeSignalSample(
        const SignalSample& sample,
        QString* errorMessage = nullptr) = 0;
    virtual bool flush(QString* errorMessage = nullptr) = 0;
};

}  // namespace miata::data
