#include "backend/ipc_signal_source.h"
#include "ipc/signal_ipc_protocol.h"
#include "ipc/replay_control.h"
#include "platform/systemd_notifier.h"
#include "service_action_runner.h"
#include "service_config_manager.h"
#include "service_data_store.h"
#include "service_log_catalog.h"
#include "simple_http_server.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonArray>
#include <QUrl>

namespace {

QByteArray compactJson(const QJsonObject& object) {
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QByteArray compactJson(const QJsonArray& array) {
    return QJsonDocument(array).toJson(QJsonDocument::Compact);
}

}  // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("miata_service_web"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser arguments;
    arguments.setApplicationDescription(QStringLiteral("Miata local service web interface"));
    arguments.addHelpOption();
    arguments.addVersionOption();
    arguments.addOption({QStringLiteral("listen-address"), QStringLiteral("HTTP listen address"),
                         QStringLiteral("address"), QStringLiteral("0.0.0.0")});
    arguments.addOption({QStringLiteral("port"), QStringLiteral("HTTP listen port"),
                         QStringLiteral("port"), QStringLiteral("8080")});
    arguments.addOption({QStringLiteral("ipc-name"), QStringLiteral("Logger signal IPC name"),
                         QStringLiteral("name"), miata::ipc::kDefaultSignalIpcServerName});
    arguments.addOption({QStringLiteral("replay-control-name"),
                         QStringLiteral("Development replay control server name"),
                         QStringLiteral("name")});
    arguments.addOption({QStringLiteral("dbc"), QStringLiteral("Active DBC path"),
                         QStringLiteral("path"), QStringLiteral("/etc/miata/miata.dbc")});
    arguments.addOption({QStringLiteral("logging-config"), QStringLiteral("Active logging config"),
                         QStringLiteral("path"), QStringLiteral("/etc/miata/logging.json")});
    arguments.addOption({QStringLiteral("dash-config"), QStringLiteral("Active dash config"),
                         QStringLiteral("path"), QStringLiteral("/etc/miata/dash.json")});
    arguments.addOption({QStringLiteral("log-directory"), QStringLiteral("Vehicle log directory"),
                         QStringLiteral("path"), QStringLiteral("/var/lib/miata/logs")});
    const QCommandLineOption configUpdatesOption(
        QStringLiteral("enable-config-updates"),
        QStringLiteral("Allow validated configuration staging and atomic activation"));
    arguments.addOption(configUpdatesOption);
    arguments.process(application);

    bool portValid = false;
    const int rawPort = arguments.value(QStringLiteral("port")).toInt(&portValid);
    const QHostAddress address(arguments.value(QStringLiteral("listen-address")));
    if (!portValid || rawPort < 1 || rawPort > 65535 || address.isNull()) {
        qCritical() << "listen address or port is invalid";
        return 1;
    }

    QFile pageFile(QStringLiteral(":/service-web/static/index.html"));
    if (!pageFile.open(QIODevice::ReadOnly)) {
        qCritical() << "embedded service page is unavailable";
        return 2;
    }
    const QByteArray indexPage = pageFile.readAll();

    miata::service::ServiceDataStore dataStore;
    dataStore.setLogDirectory(arguments.value(QStringLiteral("log-directory")));
    dataStore.setConfigurationPaths(
        arguments.value(QStringLiteral("dbc")),
        arguments.value(QStringLiteral("logging-config")),
        arguments.value(QStringLiteral("dash-config")));
    miata::service::ServiceActionRunner actions;
    miata::service::ServiceConfigManager configManager;
    configManager.setPaths(
        arguments.value(QStringLiteral("dbc")),
        arguments.value(QStringLiteral("logging-config")),
        arguments.value(QStringLiteral("dash-config")));
    configManager.setUpdatesEnabled(arguments.isSet(configUpdatesOption));
    miata::service::ServiceLogCatalog logCatalog;
    logCatalog.setDirectory(arguments.value(QStringLiteral("log-directory")));
    miata::dash::IpcSignalSource loggerSource;
    miata::ipc::ReplayControlClient replayController;
    if (arguments.isSet(QStringLiteral("replay-control-name"))) {
        replayController.setServerName(arguments.value(QStringLiteral("replay-control-name")));
        replayController.start();
    }
    loggerSource.setServerName(arguments.value(QStringLiteral("ipc-name")));
    QObject::connect(&loggerSource, &miata::dash::SignalDataSource::connectedChanged,
                     &dataStore, &miata::service::ServiceDataStore::setLoggerConnected);
    QObject::connect(&loggerSource, &miata::dash::SignalDataSource::definitionsAvailable,
                     &dataStore, &miata::service::ServiceDataStore::updateDefinitions);
    QObject::connect(&loggerSource, &miata::dash::SignalDataSource::samplesAvailable,
                     &dataStore, &miata::service::ServiceDataStore::updateSamples);
    QObject::connect(&loggerSource, &miata::dash::SignalDataSource::sourceError,
                     [](const QString& message) { qWarning().noquote() << message; });

    miata::service::SimpleHttpServer server;
    server.setHandler([&](const miata::service::HttpRequest& request) {
        if (request.method == QByteArrayLiteral("GET") && request.path == QByteArrayLiteral("/")) {
            return miata::service::HttpResponse{
                200, QByteArrayLiteral("text/html; charset=utf-8"), indexPage};
        }
        if (request.method == QByteArrayLiteral("GET")
            && request.path == QByteArrayLiteral("/api/status")) {
            QJsonObject status = dataStore.statusJson(
                actions.available(), actions.busy(), actions.lastResult());
            status.insert(QStringLiteral("config_service"), configManager.statusJson());
            status.insert(QStringLiteral("replay"), replayController.statusJson());
            return miata::service::HttpResponse::json(200, compactJson(status));
        }
        if (request.method == QByteArrayLiteral("GET")
            && request.path == QByteArrayLiteral("/api/replay")) {
            return miata::service::HttpResponse::json(200, compactJson(replayController.statusJson()));
        }
        if (request.path.startsWith(QByteArrayLiteral("/api/replay/"))) {
            if (request.method != QByteArrayLiteral("POST"))
                return miata::service::HttpResponse::text(405, QByteArrayLiteral("POST required"));
            if (!request.headers.value(QByteArrayLiteral("content-type"))
                     .startsWith(QByteArrayLiteral("application/json"))
                || request.headers.value(QByteArrayLiteral("x-miata-service")) != QByteArrayLiteral("1")) {
                return miata::service::HttpResponse::text(
                    403, QByteArrayLiteral("service request header is missing"));
            }
            const QJsonDocument body = QJsonDocument::fromJson(request.body);
            if (!body.isObject())
                return miata::service::HttpResponse::text(400, QByteArrayLiteral("JSON object required"));
            const QString command = QString::fromLatin1(
                request.path.mid(qsizetype(QByteArrayLiteral("/api/replay/").size())));
            bool accepted = false;
            if (command == QStringLiteral("play")) accepted = replayController.play();
            else if (command == QStringLiteral("pause")) accepted = replayController.pause();
            else if (command == QStringLiteral("seek"))
                accepted = replayController.seekMs(body.object().value(QStringLiteral("position_ms")).toDouble(-1));
            else if (command == QStringLiteral("speed"))
                accepted = replayController.setPlaybackSpeed(body.object().value(QStringLiteral("factor")).toDouble(-1));
            else return miata::service::HttpResponse::text(404, QByteArrayLiteral("unknown replay command"));
            return miata::service::HttpResponse::json(
                accepted ? 202 : 503,
                compactJson(QJsonObject{{QStringLiteral("ok"), accepted},
                                        {QStringLiteral("command"), command}}));
        }
        if (request.method == QByteArrayLiteral("GET")
            && request.path == QByteArrayLiteral("/api/signals")) {
            return miata::service::HttpResponse::json(200, compactJson(dataStore.signalsJson()));
        }
        if (request.method == QByteArrayLiteral("GET")
            && request.path == QByteArrayLiteral("/api/logs")) {
            return miata::service::HttpResponse::json(200, compactJson(logCatalog.catalogJson()));
        }
        if (request.method == QByteArrayLiteral("GET")
            && request.path == QByteArrayLiteral("/api/config")) {
            return miata::service::HttpResponse::json(200, compactJson(configManager.statusJson()));
        }
        if (request.method == QByteArrayLiteral("POST")
            && request.path.startsWith(QByteArrayLiteral("/api/config/"))
            && request.path.endsWith(QByteArrayLiteral("/stage"))) {
            if (!request.headers.value(QByteArrayLiteral("content-type"))
                     .startsWith(QByteArrayLiteral("application/octet-stream"))
                || request.headers.value(QByteArrayLiteral("x-miata-service")) != QByteArrayLiteral("1")) {
                return miata::service::HttpResponse::text(
                    403, QByteArrayLiteral("service request header is missing"));
            }
            const qsizetype prefix = qsizetype(QByteArrayLiteral("/api/config/").size());
            const qsizetype suffix = qsizetype(QByteArrayLiteral("/stage").size());
            const QString type = QString::fromLatin1(
                request.path.mid(prefix, request.path.size() - prefix - suffix));
            miata::service::ConfigStageResult staged;
            QString stageError;
            if (!configManager.stage(type, request.body, &staged, &stageError))
                return miata::service::HttpResponse::text(400, stageError.toUtf8());
            return miata::service::HttpResponse::json(200, compactJson(QJsonObject{
                {QStringLiteral("ok"), true},
                {QStringLiteral("type"), type},
                {QStringLiteral("token"), staged.token},
                {QStringLiteral("sha256"), staged.sha256},
                {QStringLiteral("warnings"), QJsonArray::fromStringList(staged.warnings)},
            }));
        }
        if (request.method == QByteArrayLiteral("POST")
            && request.path.startsWith(QByteArrayLiteral("/api/config/"))
            && request.path.endsWith(QByteArrayLiteral("/activate"))) {
            if (!request.headers.value(QByteArrayLiteral("content-type"))
                     .startsWith(QByteArrayLiteral("application/json"))
                || request.headers.value(QByteArrayLiteral("x-miata-service")) != QByteArrayLiteral("1")) {
                return miata::service::HttpResponse::text(
                    403, QByteArrayLiteral("service request header is missing"));
            }
            const QJsonDocument body = QJsonDocument::fromJson(request.body);
            const QString token = body.object().value(QStringLiteral("token")).toString();
            const qsizetype prefix = qsizetype(QByteArrayLiteral("/api/config/").size());
            const qsizetype suffix = qsizetype(QByteArrayLiteral("/activate").size());
            const QString type = QString::fromLatin1(
                request.path.mid(prefix, request.path.size() - prefix - suffix));
            QString activationError;
            if (!body.isObject() || !configManager.activate(type, token, &activationError))
                return miata::service::HttpResponse::text(409, activationError.toUtf8());
            return miata::service::HttpResponse::json(200, compactJson(QJsonObject{
                {QStringLiteral("ok"), true},
                {QStringLiteral("type"), type},
                {QStringLiteral("restart_required"), type == QStringLiteral("dash")
                    ? QStringLiteral("dash") : QStringLiteral("logger")},
            }));
        }
        if (request.method == QByteArrayLiteral("GET")
            && request.path.startsWith(QByteArrayLiteral("/api/log-file/"))) {
            const QString fileName = QUrl::fromPercentEncoding(
                request.path.mid(qsizetype(QByteArrayLiteral("/api/log-file/").size())));
            QString fileError;
            const QString path = logCatalog.resolveDownload(fileName, &fileError);
            if (path.isEmpty())
                return miata::service::HttpResponse::text(404, fileError.toUtf8());
            return miata::service::HttpResponse::file(
                path, fileName.toUtf8(), fileName.endsWith(QStringLiteral(".mf4"))
                    ? QByteArrayLiteral("application/octet-stream")
                    : QByteArrayLiteral("text/plain; charset=utf-8"));
        }
        if (request.method == QByteArrayLiteral("POST")
            && request.path.startsWith(QByteArrayLiteral("/api/log-session/"))
            && request.path.endsWith(QByteArrayLiteral("/delete"))) {
            if (!request.headers.value(QByteArrayLiteral("content-type"))
                     .startsWith(QByteArrayLiteral("application/json"))
                || request.headers.value(QByteArrayLiteral("x-miata-service")) != QByteArrayLiteral("1")) {
                return miata::service::HttpResponse::text(
                    403, QByteArrayLiteral("service request header is missing"));
            }
            const qsizetype prefixLength = qsizetype(QByteArrayLiteral("/api/log-session/").size());
            const qsizetype suffixLength = qsizetype(QByteArrayLiteral("/delete").size());
            const QString sessionId = QString::fromLatin1(
                request.path.mid(prefixLength, request.path.size() - prefixLength - suffixLength));
            QString deleteError;
            if (!logCatalog.deleteCompletedSession(sessionId, &deleteError))
                return miata::service::HttpResponse::text(409, deleteError.toUtf8());
            return miata::service::HttpResponse::json(
                200, compactJson(QJsonObject{{QStringLiteral("ok"), true},
                                             {QStringLiteral("session"), sessionId}}));
        }
        if (request.path.startsWith(QByteArrayLiteral("/api/action/"))) {
            if (request.method != QByteArrayLiteral("POST"))
                return miata::service::HttpResponse::text(405, QByteArrayLiteral("POST required"));
            if (!request.headers.value(QByteArrayLiteral("content-type"))
                     .startsWith(QByteArrayLiteral("application/json"))
                || request.headers.value(QByteArrayLiteral("x-miata-service")) != QByteArrayLiteral("1")) {
                return miata::service::HttpResponse::text(
                    403, QByteArrayLiteral("service request header is missing"));
            }
            const QString action = QString::fromLatin1(
                request.path.mid(qsizetype(QByteArrayLiteral("/api/action/").size())));
            QString error;
            if (!actions.run(action, &error)) {
                return miata::service::HttpResponse::json(
                    actions.busy() ? 409 : 503,
                    compactJson(QJsonObject{{QStringLiteral("ok"), false},
                                            {QStringLiteral("error"), error}}));
            }
            return miata::service::HttpResponse::json(
                202, compactJson(QJsonObject{{QStringLiteral("ok"), true},
                                             {QStringLiteral("action"), action}}));
        }
        return miata::service::HttpResponse::text(404, QByteArrayLiteral("not found"));
    });
    QObject::connect(&server, &miata::service::SimpleHttpServer::serverError,
                     [](const QString& message) { qWarning().noquote() << "HTTP:" << message; });
    QString error;
    if (!server.listen(address, static_cast<quint16>(rawPort), &error)) {
        qCritical().noquote() << "HTTP listen failed:" << error;
        return 3;
    }

    miata::platform::SystemdNotifier systemdNotifier;
    QObject::connect(&systemdNotifier, &miata::platform::SystemdNotifier::notificationError,
                     [](const QString& message) { qWarning().noquote() << message; });
    QObject::connect(&application, &QCoreApplication::aboutToQuit, [&] {
        systemdNotifier.notifyStopping(QStringLiteral("Service web interface stopping"));
        loggerSource.stop();
        replayController.stop();
        server.close();
    });

    loggerSource.start();
    qInfo().noquote() << QStringLiteral("Service web interface listening on http://%1:%2")
                            .arg(address.toString()).arg(server.serverPort());
    systemdNotifier.notifyReady(QStringLiteral("Service web interface ready"));
    return application.exec();
}
