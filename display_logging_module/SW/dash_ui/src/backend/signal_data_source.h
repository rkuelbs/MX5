#pragma once

#include "core/signal_definition.h"
#include "core/signal_sample.h"

#include <QObject>

namespace miata::dash {

// Common input contract for fake data now and logger IPC later. Sources send
// definitions infrequently and samples in batches to avoid per-signal IPC and
// event-loop overhead.
class SignalDataSource : public QObject {
    Q_OBJECT

public:
    explicit SignalDataSource(QObject* parent = nullptr) : QObject(parent) {}
    ~SignalDataSource() override = default;

    virtual void start() = 0;
    virtual void stop() = 0;

signals:
    void definitionsAvailable(const QList<miata::data::SignalDefinition>& definitions);
    void samplesAvailable(const QList<miata::data::SignalSample>& samples);
    void connectedChanged(bool connected);
    void sourceError(const QString& message);
};

}  // namespace miata::dash
