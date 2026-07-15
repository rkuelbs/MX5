#pragma once

#include "core/signal_definition.h"
#include "core/signal_sample.h"

#include <QByteArray>
#include <QList>
#include <QStringList>

namespace miata::data {

// Streaming parser for VN-300 binary output containing any Common-group
// subset and the Attitude-group LinBodyAcc field. Feed boundaries need not
// coincide with packet boundaries.
class Vn300BinaryParser final {
public:
    [[nodiscard]] QList<SignalSample> consume(
        const QByteArray& bytes,
        qint64 monotonicTimestampNs);
    void reset();

    [[nodiscard]] quint64 validPacketCount() const;
    [[nodiscard]] quint64 crcErrorCount() const;
    [[nodiscard]] quint64 formatErrorCount() const;

    [[nodiscard]] static QList<SignalDefinition> signalDefinitions();
    [[nodiscard]] static QStringList canonicalSignalNames();
    [[nodiscard]] static quint16 calculateCrc(const char* data, qsizetype length);

private:
    QByteArray buffer_;
    quint64 validPacketCount_ = 0;
    quint64 crcErrorCount_ = 0;
    quint64 formatErrorCount_ = 0;
};

}  // namespace miata::data
