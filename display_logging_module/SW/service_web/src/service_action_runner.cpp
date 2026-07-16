#include "service_action_runner.h"

#include <QHash>

namespace miata::service {

ServiceActionRunner::ServiceActionRunner(QObject* parent) : QObject(parent) {
    connect(&process_, &QProcess::finished, this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
        const QString detail = QString::fromUtf8(process_.readAllStandardError()).trimmed();
        lastResult_ = exitStatus == QProcess::NormalExit && exitCode == 0
            ? QStringLiteral("%1 accepted").arg(activeAction_)
            : QStringLiteral("%1 failed%2").arg(
                  activeAction_, detail.isEmpty() ? QString{} : QStringLiteral(": %1").arg(detail));
        activeAction_.clear();
        emit stateChanged();
    });
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        lastResult_ = QStringLiteral("%1 failed: %2").arg(activeAction_, process_.errorString());
        activeAction_.clear();
        emit stateChanged();
    });
}

bool ServiceActionRunner::available() const {
#ifdef Q_OS_LINUX
    return true;
#else
    return false;
#endif
}

bool ServiceActionRunner::busy() const { return process_.state() != QProcess::NotRunning; }
QString ServiceActionRunner::lastResult() const { return lastResult_; }

bool ServiceActionRunner::run(const QString& action, QString* errorMessage) {
    if (!available()) {
        if (errorMessage) *errorMessage = QStringLiteral("service actions are available only on Linux");
        return false;
    }
    if (busy()) {
        if (errorMessage) *errorMessage = QStringLiteral("another service action is still running");
        return false;
    }
    static const QHash<QString, QStringList> commands{
        {QStringLiteral("restart-dash"),
         {QStringLiteral("-n"), QStringLiteral("/usr/bin/systemctl"),
          QStringLiteral("restart"), QStringLiteral("miata-dash.service")}},
        {QStringLiteral("restart-logger"),
         {QStringLiteral("-n"), QStringLiteral("/usr/bin/systemctl"),
          QStringLiteral("restart"), QStringLiteral("vehicle-loggerd.service")}},
        {QStringLiteral("reboot"),
         {QStringLiteral("-n"), QStringLiteral("/usr/bin/systemctl"),
          QStringLiteral("reboot")}},
    };
    const auto command = commands.constFind(action);
    if (command == commands.cend()) {
        if (errorMessage) *errorMessage = QStringLiteral("unknown service action");
        return false;
    }
    activeAction_ = action;
    lastResult_ = QStringLiteral("%1 requested").arg(action);
    process_.start(QStringLiteral("/usr/bin/sudo"), *command, QIODevice::ReadOnly);
    emit stateChanged();
    return true;
}

}  // namespace miata::service
