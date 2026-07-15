#include "can/can_replay_source.h"

#include <QSignalSpy>
#include <QtTest>

class CanReplaySourceTest final : public QObject {
    Q_OBJECT

private slots:
    void replaysFixtureInFastMode();
};

void CanReplaySourceTest::replaysFixtureInFastMode() {
    qRegisterMetaType<miata::data::CanFrameRecord>();
    miata::data::CanReplaySource replay;
    QString error;
    QVERIFY2(replay.load(QStringLiteral(MIATA_TEST_REPLAY_PATH), &error), qPrintable(error));
    QCOMPARE(replay.frameCount(), 3);

    QSignalSpy frames(&replay, &miata::data::CanReplaySource::frameReceived);
    QSignalSpy finished(&replay, &miata::data::CanReplaySource::replayFinished);
    QVERIFY(replay.start(miata::data::CanReplaySource::TimingMode::Fast, 1.0, &error));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 1000);
    QCOMPARE(frames.count(), 3);

    const auto first = qvariant_cast<miata::data::CanFrameRecord>(frames.at(0).at(0));
    const auto last = qvariant_cast<miata::data::CanFrameRecord>(frames.at(2).at(0));
    QCOMPARE(first.monotonicTimestampNs, 0);
    QCOMPARE(last.monotonicTimestampNs, 100'000'000LL);
    QCOMPARE(last.frame.frameId(), 0x5F2U);
}

QTEST_MAIN(CanReplaySourceTest)
#include "can_replay_source_test.moc"
