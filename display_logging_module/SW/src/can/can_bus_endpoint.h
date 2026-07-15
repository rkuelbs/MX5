#pragma once

#include "can/can_frame_record.h"

#include <QCanBusDevice>
#include <QElapsedTimer>
#include <QObject>
#include <QString>

#include <memory>

namespace miata::data {

class CanBusEndpoint final : public QObject {
    Q_OBJECT

public:
    explicit CanBusEndpoint(QObject* parent = nullptr);

    // The logger supplies one shared clock to all live acquisition sources so
    // samples remain globally ordered. The clock must outlive this endpoint.
    void setTimestampClock(const QElapsedTimer* clock);

    bool start(
        const QString& pluginName,
        const QString& interfaceName,
        QString* errorMessage = nullptr);
    void stop();
    [[nodiscard]] bool isConnected() const;
    bool writeFrame(const QCanBusFrame& frame, QString* errorMessage = nullptr);

signals:
    void frameReceived(const miata::data::CanFrameRecord& record);
    void endpointError(const QString& message);

private slots:
    void readFrames();
    void handleError(QCanBusDevice::CanBusError error);

private:
    std::unique_ptr<QCanBusDevice> device_;
    QElapsedTimer monotonicClock_;
    const QElapsedTimer* timestampClock_ = nullptr;
    QString interfaceName_;
};

}  // namespace miata::data
