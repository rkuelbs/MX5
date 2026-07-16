#pragma once

#include <QObject>
#include <QTimer>

namespace miata::platform {

class SystemdNotifier final : public QObject {
    Q_OBJECT

public:
    explicit SystemdNotifier(QObject* parent = nullptr);

    [[nodiscard]] bool available() const;
    [[nodiscard]] bool watchdogEnabled() const;
    void notifyReady(const QString& status = {});
    void notifyStatus(const QString& status);
    void notifyStopping(const QString& status = {});

    // Exposed for deterministic platform-independent configuration tests.
    [[nodiscard]] static int watchdogPeriodMs(
        const QByteArray& watchdogUsec, const QByteArray& watchdogPid, qint64 processId);

signals:
    void notificationError(const QString& message);

private:
    bool send(const QByteArray& payload);
    static QByteArray safeStatus(const QString& status);

    QByteArray notifySocket_;
    QTimer watchdogTimer_;
    bool ready_ = false;
};

}  // namespace miata::platform
