#include "ipc/signal_ipc_protocol.h"

#include <QtTest>

class SignalIpcProtocolTest final : public QObject {
    Q_OBJECT

private slots:
    void roundTripsFragmentedDefinitionsAndSamples();
    void rejectsOversizedFrames();
};

void SignalIpcProtocolTest::roundTripsFragmentedDefinitionsAndSamples() {
    using namespace miata;
    const QList<data::SignalDefinition> definitions{
        {QStringLiteral("ECM.rpm"), QStringLiteral("RPM")},
        {QStringLiteral("WCM.inputs"), QString{}},
    };
    const QList<data::SignalSample> samples{
        {QStringLiteral("ECM.rpm"), 4321.5, {}, 100, data::SignalSource::Can},
        {QStringLiteral("WCM.inputs"), quint64(3), {}, 101, data::SignalSource::Can},
        {QStringLiteral("LOGGER.can_healthy"), true, {}, 102, data::SignalSource::Derived},
        {QStringLiteral("TEST.text"), QStringLiteral("ready"), {}, 103,
         data::SignalSource::Replay},
    };

    const QByteArray wire = ipc::encodeDefinitions(definitions) + ipc::encodeSamples(samples);
    QByteArray buffer = wire.left(7);
    QList<ipc::SignalIpcMessage> messages;
    QString error;
    QVERIFY2(ipc::decodeAvailableMessages(&buffer, &messages, &error), qPrintable(error));
    QVERIFY(messages.isEmpty());

    buffer.append(wire.mid(7));
    QVERIFY2(ipc::decodeAvailableMessages(&buffer, &messages, &error), qPrintable(error));
    QCOMPARE(buffer.size(), 0);
    QCOMPARE(messages.size(), 2);
    QCOMPARE(messages[0].definitions.size(), 2);
    QCOMPARE(messages[0].definitions[0].canonicalName, QStringLiteral("ECM.rpm"));
    QCOMPARE(messages[1].samples.size(), 4);
    QCOMPARE(messages[1].samples[0].value.toDouble(), 4321.5);
    QCOMPARE(messages[1].samples[1].value.toULongLong(), quint64(3));
    QCOMPARE(messages[1].samples[2].value.toBool(), true);
    QCOMPARE(messages[1].samples[3].value.toString(), QStringLiteral("ready"));
    QCOMPARE(messages[1].samples[3].source, data::SignalSource::Replay);
}

void SignalIpcProtocolTest::rejectsOversizedFrames() {
    QByteArray buffer(4, '\0');
    const quint32 invalidLength = miata::ipc::kMaximumSignalIpcFrameBytes + 1;
    buffer[0] = char((invalidLength >> 24) & 0xff);
    buffer[1] = char((invalidLength >> 16) & 0xff);
    buffer[2] = char((invalidLength >> 8) & 0xff);
    buffer[3] = char(invalidLength & 0xff);
    QList<miata::ipc::SignalIpcMessage> messages;
    QString error;
    QVERIFY(!miata::ipc::decodeAvailableMessages(&buffer, &messages, &error));
    QVERIFY(error.contains(QStringLiteral("length")));
}

QTEST_MAIN(SignalIpcProtocolTest)
#include "signal_ipc_protocol_test.moc"
