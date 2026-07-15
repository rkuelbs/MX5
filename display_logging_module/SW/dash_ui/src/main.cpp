#include "backend/fake_signal_source.h"
#include "backend/signal_filter_model.h"
#include "backend/signal_list_model.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>

int main(int argc, char* argv[]) {
    QGuiApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("miata_dash"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser arguments;
    arguments.setApplicationDescription(QStringLiteral("Miata Qt Quick dashboard"));
    arguments.addHelpOption();
    arguments.addVersionOption();
    const QCommandLineOption fullscreenOption(
        QStringLiteral("fullscreen"), QStringLiteral("Start in full-screen mode"));
    arguments.addOption(fullscreenOption);
    arguments.process(application);

    miata::dash::SignalListModel sourceModel;
    miata::dash::SignalFilterModel filteredModel;
    filteredModel.setSignalModel(&sourceModel);
    miata::dash::FakeSignalSource fakeSource;

    QObject::connect(&fakeSource, &miata::dash::SignalDataSource::definitionsAvailable,
                     &sourceModel, &miata::dash::SignalListModel::setDefinitions);
    QObject::connect(&fakeSource, &miata::dash::SignalDataSource::samplesAvailable,
                     &sourceModel, &miata::dash::SignalListModel::updateSamples);

    QQmlApplicationEngine engine;
    engine.setInitialProperties({
        {QStringLiteral("signalModel"), QVariant::fromValue(&filteredModel)},
        {QStringLiteral("showFullScreen"), arguments.isSet(fullscreenOption)},
        {QStringLiteral("dataSourceName"), QStringLiteral("Animated simulation")},
    });
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &application, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);

    fakeSource.start();
    engine.loadFromModule(QStringLiteral("Miata.Dash"), QStringLiteral("Main"));
    return application.exec();
}
