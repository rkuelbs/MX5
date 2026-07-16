#pragma once

#include <QObject>
#include <QTimer>

namespace miata::platform {

// Converts console/POSIX termination notifications into a Qt event-loop signal
// so aboutToQuit handlers can drain and finalize logs.
class ProcessTerminationMonitor final : public QObject {
    Q_OBJECT

public:
    explicit ProcessTerminationMonitor(QObject* parent = nullptr);
    ~ProcessTerminationMonitor() override;

signals:
    void terminationRequested();

private:
    QTimer pollTimer_;
};

}  // namespace miata::platform
