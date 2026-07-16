#include "platform/process_termination_monitor.h"

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <csignal>
#endif

namespace miata::platform {
namespace {

#ifdef Q_OS_WIN
volatile LONG terminationFlag = 0;

BOOL WINAPI consoleHandler(DWORD event) {
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT || event == CTRL_CLOSE_EVENT
        || event == CTRL_LOGOFF_EVENT || event == CTRL_SHUTDOWN_EVENT) {
        InterlockedExchange(&terminationFlag, 1);
        return TRUE;
    }
    return FALSE;
}
#else
volatile std::sig_atomic_t terminationFlag = 0;

void signalHandler(int) { terminationFlag = 1; }
#endif

}  // namespace

ProcessTerminationMonitor::ProcessTerminationMonitor(QObject* parent) : QObject(parent) {
#ifdef Q_OS_WIN
    SetConsoleCtrlHandler(consoleHandler, TRUE);
#else
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
#endif
    pollTimer_.setInterval(50);
    connect(&pollTimer_, &QTimer::timeout, this, [this] {
#ifdef Q_OS_WIN
        const bool requested = InterlockedExchange(&terminationFlag, 0) != 0;
#else
        const bool requested = terminationFlag != 0;
        terminationFlag = 0;
#endif
        if (!requested) return;
        pollTimer_.stop();
        emit terminationRequested();
    });
    pollTimer_.start();
}

ProcessTerminationMonitor::~ProcessTerminationMonitor() {
#ifdef Q_OS_WIN
    SetConsoleCtrlHandler(consoleHandler, FALSE);
#else
    std::signal(SIGINT, SIG_DFL);
    std::signal(SIGTERM, SIG_DFL);
#endif
}

}  // namespace miata::platform
