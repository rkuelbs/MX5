#include "service_data_store.h"
#include "service_config_manager.h"
#include "service_log_catalog.h"
#include "simple_http_server.h"

#include <QFile>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

class ServiceWebTest final : public QObject {
    Q_OBJECT

private slots:
    void servesBoundedHttpRequests();
    void exposesLatestSignalAndConfigurationStatus();
    void catalogsDownloadsAndDeletesOnlyCompletedLogs();
    void streamsFileResponses();
    void validatesAndAtomicallyActivatesCompleteConfigurationSet();
};

void ServiceWebTest::servesBoundedHttpRequests() {
    miata::service::SimpleHttpServer server;
    server.setHandler([](const miata::service::HttpRequest& request) {
        if (request.method == "POST" && request.path == "/test" && request.body == "{}")
            return miata::service::HttpResponse::json(202, QByteArrayLiteral("{\"ok\":true}"));
        return miata::service::HttpResponse::text(404, QByteArrayLiteral("missing"));
    });
    QString error;
    QVERIFY2(server.listen(QHostAddress::LocalHost, 0, &error), qPrintable(error));

    QTcpSocket client;
    client.connectToHost(QHostAddress::LocalHost, server.serverPort());
    QVERIFY(client.waitForConnected(1000));
    client.write("POST /test HTTP/1.1\r\nHost: localhost\r\nContent-Length: 2\r\n\r\n{}");
    QTRY_VERIFY_WITH_TIMEOUT(client.bytesAvailable() > 0, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), QAbstractSocket::UnconnectedState, 1000);
    const QByteArray response = client.readAll();
    QVERIFY(response.startsWith("HTTP/1.1 202 Accepted\r\n"));
    QVERIFY(response.contains("Content-Type: application/json"));
    QVERIFY(response.endsWith("{\"ok\":true}"));
}

void ServiceWebTest::exposesLatestSignalAndConfigurationStatus() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString configPath = directory.filePath(QStringLiteral("config.json"));
    QFile config(configPath);
    QVERIFY(config.open(QIODevice::WriteOnly));
    QCOMPARE(config.write("{}"), 2);
    config.close();

    miata::service::ServiceDataStore store;
    store.setLogDirectory(directory.path());
    store.setConfigurationPaths(configPath, configPath, configPath);
    store.setLoggerConnected(true);
    store.updateDefinitions({{QStringLiteral("ECM.rpm"), QStringLiteral("rpm")}});
    store.updateSamples({
        {QStringLiteral("ECM.rpm"), 4321.0, QStringLiteral("rpm"), 1,
         miata::data::SignalSource::Can},
    });

    const auto status = store.statusJson(false, false, {});
    QCOMPARE(status.value(QStringLiteral("logger_connected")).toBool(), true);
    QCOMPARE(status.value(QStringLiteral("signal_count")).toInt(), 1);
    QCOMPARE(status.value(QStringLiteral("configuration")).toObject()
                 .value(QStringLiteral("dbc_sha256")).toString().size(), 64);

    const auto signalValues = store.signalsJson();
    QCOMPARE(signalValues.size(), 1);
    const auto rpm = signalValues.at(0).toObject();
    QCOMPARE(rpm.value(QStringLiteral("name")).toString(), QStringLiteral("ECM.rpm"));
    QCOMPARE(rpm.value(QStringLiteral("value")).toDouble(), 4321.0);
    QCOMPARE(rpm.value(QStringLiteral("source")).toString(), QStringLiteral("CAN"));
}

void ServiceWebTest::catalogsDownloadsAndDeletesOnlyCompletedLogs() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString complete = QStringLiteral("vehicle_20260101_010203_004");
    const QString active = QStringLiteral("vehicle_20260102_010203_004");
    QFile completeFile(directory.filePath(complete + QStringLiteral(".mf4")));
    QVERIFY(completeFile.open(QIODevice::WriteOnly));
    QCOMPARE(completeFile.write("MDF test"), 8);
    completeFile.close();
    QFile activeFile(directory.filePath(active + QStringLiteral(".mf4")));
    QVERIFY(activeFile.open(QIODevice::WriteOnly));
    QCOMPARE(activeFile.write("active"), 6);
    activeFile.close();
    QFile marker(directory.filePath(active + QStringLiteral(".active")));
    QVERIFY(marker.open(QIODevice::WriteOnly));
    marker.close();

    miata::service::ServiceLogCatalog catalog;
    catalog.setDirectory(directory.path());
    const QJsonObject listing = catalog.catalogJson();
    QCOMPARE(listing.value(QStringLiteral("session_count")).toInt(), 2);
    QCOMPARE(listing.value(QStringLiteral("completed_count")).toInt(), 1);
    QCOMPARE(listing.value(QStringLiteral("protected_count")).toInt(), 1);
    QString error;
    QVERIFY(catalog.resolveDownload(QStringLiteral("../secret"), &error).isEmpty());
    QVERIFY(!catalog.deleteCompletedSession(active, &error));
    QVERIFY(error.contains(QStringLiteral("protected")));
    QVERIFY(catalog.deleteCompletedSession(complete, &error));
    QVERIFY(!QFile::exists(directory.filePath(complete + QStringLiteral(".mf4"))));
}

void ServiceWebTest::streamsFileResponses() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("download.bin"));
    const QByteArray payload(400'000, 'z');
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(payload), payload.size());
    file.close();

    miata::service::SimpleHttpServer server;
    server.setHandler([&](const miata::service::HttpRequest&) {
        return miata::service::HttpResponse::file(
            path, QByteArrayLiteral("download.bin"));
    });
    QString error;
    QVERIFY2(server.listen(QHostAddress::LocalHost, 0, &error), qPrintable(error));
    QTcpSocket client;
    client.connectToHost(QHostAddress::LocalHost, server.serverPort());
    QVERIFY(client.waitForConnected(1000));
    client.write("GET /file HTTP/1.1\r\nHost: localhost\r\n\r\n");
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), QAbstractSocket::UnconnectedState, 3000);
    const QByteArray response = client.readAll();
    const qsizetype bodyStart = response.indexOf("\r\n\r\n") + 4;
    QVERIFY(bodyStart >= 4);
    QVERIFY(response.startsWith("HTTP/1.1 200 OK\r\n"));
    QVERIFY(response.contains("Content-Disposition: attachment; filename=\"download.bin\""));
    QCOMPARE(response.mid(bodyStart), payload);
}

void ServiceWebTest::validatesAndAtomicallyActivatesCompleteConfigurationSet() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto copy = [&](const QString& source, const QString& name) {
        QFile input(source);
        if (!input.open(QIODevice::ReadOnly)) return QString{};
        QFile output(directory.filePath(name));
        if (!output.open(QIODevice::WriteOnly) || output.write(input.readAll()) < 0) return QString{};
        return output.fileName();
    };
    const QString dbc = copy(QStringLiteral(MIATA_TEST_DBC_PATH), QStringLiteral("miata.dbc"));
    const QString logging = copy(
        QStringLiteral(MIATA_TEST_LOGGING_CONFIG_PATH), QStringLiteral("logging.json"));
    const QString dash = copy(
        QStringLiteral(MIATA_TEST_DASH_CONFIG_PATH), QStringLiteral("dash.json"));
    QVERIFY(!dbc.isEmpty() && !logging.isEmpty() && !dash.isEmpty());
    QFile originalDash(dash);
    QVERIFY(originalDash.open(QIODevice::ReadOnly));
    const QByteArray original = originalDash.readAll();
    originalDash.close();

    miata::service::ServiceConfigManager manager;
    manager.setPaths(dbc, logging, dash);
    miata::service::ConfigStageResult staged;
    QString error;
    QVERIFY(!manager.stage(QStringLiteral("dash"), original, &staged, &error));
    QVERIFY(error.contains(QStringLiteral("disabled")));
    manager.setUpdatesEnabled(true);
    const QByteArray candidate = original + '\n';
    QVERIFY2(manager.stage(QStringLiteral("dash"), candidate, &staged, &error), qPrintable(error));
    QCOMPARE(staged.sha256.size(), 64);
    QVERIFY(!staged.token.isEmpty());
    QVERIFY2(manager.activate(QStringLiteral("dash"), staged.token, &error), qPrintable(error));
    QFile activated(dash);
    QVERIFY(activated.open(QIODevice::ReadOnly));
    QCOMPARE(activated.readAll(), candidate);
    QFile backup(dash + QStringLiteral(".previous"));
    QVERIFY(backup.open(QIODevice::ReadOnly));
    QCOMPARE(backup.readAll(), original);

    QFile dbcFile(dbc);
    QVERIFY(dbcFile.open(QIODevice::ReadOnly));
    QVERIFY2(manager.stage(QStringLiteral("dbc"), dbcFile.readAll(), &staged, &error), qPrintable(error));
    manager.discard();

    QJsonDocument invalidDocument = QJsonDocument::fromJson(original);
    QJsonObject root = invalidDocument.object();
    QJsonObject presentations = root.value(QStringLiteral("signals")).toObject();
    presentations.insert(QStringLiteral("UNKNOWN.not_in_dbc"), QJsonObject{});
    root.insert(QStringLiteral("signals"), presentations);
    QVERIFY(!manager.stage(
        QStringLiteral("dash"), QJsonDocument(root).toJson(), &staged, &error));
    QVERIFY(error.contains(QStringLiteral("unknown signal")));
}

QTEST_MAIN(ServiceWebTest)
#include "service_web_test.moc"
