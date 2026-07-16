#include "platform/systemd_notifier.h"

#include <QCoreApplication>

#include <algorithm>
#include <cstddef>
#include <cerrno>
#include <cstring>
#include <limits>

#ifdef Q_OS_UNIX
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace miata::platform {

SystemdNotifier::SystemdNotifier(QObject* parent)
    : QObject(parent), notifySocket_(qgetenv("NOTIFY_SOCKET")) {
    watchdogTimer_.setTimerType(Qt::CoarseTimer);
    const int periodMs = watchdogPeriodMs(
        qgetenv("WATCHDOG_USEC"), qgetenv("WATCHDOG_PID"),
        QCoreApplication::applicationPid());
    if (periodMs > 0) watchdogTimer_.setInterval(periodMs);
    connect(&watchdogTimer_, &QTimer::timeout, this, [this] {
        if (ready_) send(QByteArrayLiteral("WATCHDOG=1"));
    });
}

bool SystemdNotifier::available() const { return !notifySocket_.isEmpty(); }
bool SystemdNotifier::watchdogEnabled() const { return watchdogTimer_.interval() > 0; }

void SystemdNotifier::notifyReady(const QString& status) {
    QByteArray payload = QByteArrayLiteral("READY=1\nMAINPID=")
        + QByteArray::number(QCoreApplication::applicationPid());
    if (!status.isEmpty()) payload += QByteArrayLiteral("\nSTATUS=") + safeStatus(status);
    if (!send(payload)) return;
    ready_ = true;
    if (watchdogEnabled()) watchdogTimer_.start();
}

void SystemdNotifier::notifyStatus(const QString& status) {
    send(QByteArrayLiteral("STATUS=") + safeStatus(status));
}

void SystemdNotifier::notifyStopping(const QString& status) {
    watchdogTimer_.stop();
    ready_ = false;
    QByteArray payload = QByteArrayLiteral("STOPPING=1");
    if (!status.isEmpty()) payload += QByteArrayLiteral("\nSTATUS=") + safeStatus(status);
    send(payload);
}

int SystemdNotifier::watchdogPeriodMs(
    const QByteArray& watchdogUsec, const QByteArray& watchdogPid, qint64 processId) {
    bool usecValid = false;
    const qulonglong usec = watchdogUsec.toULongLong(&usecValid);
    if (!usecValid || usec == 0) return 0;
    if (!watchdogPid.isEmpty()) {
        bool pidValid = false;
        const qlonglong configuredPid = watchdogPid.toLongLong(&pidValid);
        if (!pidValid || configuredPid != processId) return 0;
    }
    const qulonglong halfPeriodMs = std::max<qulonglong>(1, usec / 2000);
    return static_cast<int>(std::min<qulonglong>(
        halfPeriodMs, static_cast<qulonglong>(std::numeric_limits<int>::max())));
}

QByteArray SystemdNotifier::safeStatus(const QString& status) {
    QByteArray result = status.toUtf8();
    result.replace('\n', ' ');
    result.replace('\r', ' ');
    return result;
}

bool SystemdNotifier::send(const QByteArray& payload) {
    if (notifySocket_.isEmpty()) return true;
#ifdef Q_OS_UNIX
    sockaddr_un address{};
    if (notifySocket_.size() >= int(sizeof(address.sun_path))) {
        emit notificationError(QStringLiteral("systemd notify socket path is too long"));
        return false;
    }
    const int descriptor = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (descriptor < 0) {
        emit notificationError(QStringLiteral("systemd notify socket creation failed: %1")
                                   .arg(QString::fromLocal8Bit(std::strerror(errno))));
        return false;
    }
    address.sun_family = AF_UNIX;
    socklen_t addressLength = 0;
    if (notifySocket_.startsWith('@')) {
        address.sun_path[0] = '\0';
        std::memcpy(address.sun_path + 1, notifySocket_.constData() + 1,
                    std::size_t(notifySocket_.size() - 1));
        addressLength = socklen_t(offsetof(sockaddr_un, sun_path) + notifySocket_.size());
    } else {
        std::memcpy(address.sun_path, notifySocket_.constData(), std::size_t(notifySocket_.size()));
        address.sun_path[notifySocket_.size()] = '\0';
        addressLength = socklen_t(offsetof(sockaddr_un, sun_path) + notifySocket_.size() + 1);
    }
    const auto sent = ::sendto(descriptor, payload.constData(), std::size_t(payload.size()), 0,
                               reinterpret_cast<const sockaddr*>(&address), addressLength);
    const int savedError = errno;
    ::close(descriptor);
    if (sent == payload.size()) return true;
    emit notificationError(QStringLiteral("systemd notification failed: %1")
                               .arg(QString::fromLocal8Bit(std::strerror(savedError))));
    return false;
#else
    Q_UNUSED(payload);
    emit notificationError(QStringLiteral("systemd notifications are unavailable on this platform"));
    return false;
#endif
}

}  // namespace miata::platform
