#include "sim/vn300_simulator.h"
#include "vn300/vn300_log_codec.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QSerialPort>
#include <QTextStream>
#include <QTimer>

using miata::data::Vn300LogCodec;
using miata::data::Vn300Simulator;

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vn300_simulator"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser arguments;
    arguments.setApplicationDescription(
        QStringLiteral("Animated VN300 circle-driving binary-output simulator"));
    arguments.addHelpOption();
    arguments.addVersionOption();
    arguments.addOption({QStringLiteral("scenario"), QStringLiteral("Simulator JSON scenario"),
                         QStringLiteral("path"),
                         QStringLiteral("display_logging_module/SW/config/vn300_simulator.json")});
    arguments.addOption({QStringLiteral("serial-port"),
                         QStringLiteral("Optional serial port or virtual null-modem endpoint"),
                         QStringLiteral("name")});
    arguments.addOption({QStringLiteral("baud"), QStringLiteral("Serial baud rate"),
                         QStringLiteral("baud"), QStringLiteral("921600")});
    arguments.addOption({QStringLiteral("record"),
                         QStringLiteral("Optional timestamped VN300 replay capture"),
                         QStringLiteral("path")});
    arguments.process(application);

    if (!arguments.isSet(QStringLiteral("serial-port"))
        && !arguments.isSet(QStringLiteral("record"))) {
        qCritical() << "Specify --serial-port, --record, or both";
        return 1;
    }
    bool baudOk = false;
    const qint32 baud = arguments.value(QStringLiteral("baud")).toInt(&baudOk);
    if (!baudOk || baud <= 0) {
        qCritical() << "--baud must be a positive integer";
        return 1;
    }

    QString error;
    Vn300Simulator simulator;
    if (!simulator.watchScenario(arguments.value(QStringLiteral("scenario")), &error)) {
        qCritical().noquote() << "Scenario load failed:" << error;
        return 2;
    }

    QSerialPort serial;
    if (arguments.isSet(QStringLiteral("serial-port"))) {
        serial.setPortName(arguments.value(QStringLiteral("serial-port")));
        serial.setBaudRate(baud);
        serial.setDataBits(QSerialPort::Data8);
        serial.setParity(QSerialPort::NoParity);
        serial.setStopBits(QSerialPort::OneStop);
        serial.setFlowControl(QSerialPort::NoFlowControl);
        if (!serial.open(QIODevice::WriteOnly)) {
            qCritical().noquote() << "Serial open failed:" << serial.errorString();
            return 3;
        }
    }

    QFile capture;
    QTextStream captureStream;
    if (arguments.isSet(QStringLiteral("record"))) {
        capture.setFileName(arguments.value(QStringLiteral("record")));
        if (!capture.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            qCritical().noquote() << "Capture open failed:" << capture.errorString();
            return 4;
        }
        captureStream.setDevice(&capture);
        captureStream << "; VN300 timestamped binary packets\n";
    }

    QObject::connect(&simulator, &Vn300Simulator::packetGenerated, &application,
                     [&](const miata::data::Vn300PacketRecord& record) {
        if (serial.isOpen() && serial.write(record.packet) != record.packet.size()) {
            qWarning().noquote() << "Serial write failed:" << serial.errorString();
        }
        if (capture.isOpen()) captureStream << Vn300LogCodec::formatLine(record) << '\n';
    });
    QObject::connect(&simulator, &Vn300Simulator::scenarioReloaded,
                     [](const QString& path) { qInfo().noquote() << "Reloaded scenario" << path; });
    QObject::connect(&simulator, &Vn300Simulator::simulatorError,
                     [](const QString& message) { qWarning().noquote() << message; });
    QObject::connect(&application, &QCoreApplication::aboutToQuit, [&] {
        captureStream.flush();
        if (serial.isOpen()) serial.waitForBytesWritten(1000);
    });
    QTimer flushTimer;
    flushTimer.setInterval(1000);
    QObject::connect(&flushTimer, &QTimer::timeout, [&] { captureStream.flush(); });
    flushTimer.start();

    simulator.start();
    qInfo() << "VN300 simulator running at" << simulator.rateHz() << "Hz";
    return application.exec();
}
