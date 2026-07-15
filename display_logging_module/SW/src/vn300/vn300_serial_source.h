#pragma once

#include "vn300/vn300_binary_parser.h"

#include <QElapsedTimer>
#include <QObject>
#include <QSerialPort>

namespace miata::data {

class Vn300SerialSource final : public QObject {
    Q_OBJECT

public:
    explicit Vn300SerialSource(QObject* parent = nullptr);
    void setTimestampClock(const QElapsedTimer* clock);
    bool start(const QString& portName, qint32 baudRate, QString* errorMessage = nullptr);
    void stop();
    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] const Vn300BinaryParser& parser() const;

signals:
    void samplesReceived(const QList<miata::data::SignalSample>& samples);
    void sourceError(const QString& message);

private slots:
    void readAvailable();
    void handleError(QSerialPort::SerialPortError error);

private:
    QSerialPort serial_;
    QElapsedTimer monotonicClock_;
    const QElapsedTimer* timestampClock_ = nullptr;
    Vn300BinaryParser parser_;
};

}  // namespace miata::data
