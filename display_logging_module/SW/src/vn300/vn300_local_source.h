#pragma once

#include "vn300/vn300_binary_parser.h"

#include <QElapsedTimer>
#include <QLocalSocket>
#include <QObject>
#include <QTimer>

namespace miata::data {

// Development transport for feeding the production VN300 binary parser from
// the simulator without requiring a virtual serial-port driver.
class Vn300LocalSource final : public QObject {
    Q_OBJECT

public:
    explicit Vn300LocalSource(QObject* parent = nullptr);
    void setTimestampClock(const QElapsedTimer* clock);
    bool start(const QString& serverName, QString* errorMessage = nullptr);
    void stop();
    [[nodiscard]] bool isConnected() const;
    [[nodiscard]] const Vn300BinaryParser& parser() const;

signals:
    void samplesReceived(const QList<miata::data::SignalSample>& samples);
    void sourceError(const QString& message);

private:
    void connectToServer();
    void readAvailable();

    QLocalSocket socket_;
    QTimer reconnectTimer_;
    QElapsedTimer monotonicClock_;
    const QElapsedTimer* timestampClock_ = nullptr;
    Vn300BinaryParser parser_;
    QString serverName_;
    bool running_ = false;
};

}  // namespace miata::data
