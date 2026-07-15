#pragma once

#include "core/signal_definition.h"
#include "core/signal_sample.h"

#include <QList>
#include <QStringList>

namespace miata::data {

class SourceHealthMonitor final {
public:
    void setCanState(bool enabled, bool connected);
    void setVn300State(bool enabled, bool connected);
    void setTimeouts(qint64 canTimeoutNs, qint64 vn300TimeoutNs);
    void noteCanReception(qint64 timestampNs);
    void noteCanDecodeError();
    void noteVn300Reception(qint64 timestampNs);
    void setVn300ErrorCounts(quint64 crcErrors, quint64 formatErrors);

    [[nodiscard]] QList<SignalSample> samples(qint64 nowNs) const;
    [[nodiscard]] static QList<SignalDefinition> signalDefinitions();
    [[nodiscard]] static QStringList canonicalSignalNames();

private:
    bool canEnabled_ = true;
    bool canConnected_ = false;
    bool vn300Enabled_ = false;
    bool vn300Connected_ = false;
    qint64 canTimeoutNs_ = 500'000'000;
    qint64 vn300TimeoutNs_ = 500'000'000;
    qint64 lastCanRxNs_ = -1;
    qint64 lastVn300RxNs_ = -1;
    quint64 canDecodeErrors_ = 0;
    quint64 vn300CrcErrors_ = 0;
    quint64 vn300FormatErrors_ = 0;
};

}  // namespace miata::data
