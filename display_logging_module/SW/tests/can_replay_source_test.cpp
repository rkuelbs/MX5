#include "can/can_replay_source.h"

#include <QSignalSpy>
#include <QtTest>

class CanReplaySourceTest final : public QObject {
    Q_OBJECT

private slots:
    void replaysFixtureInFastMode();
    void pausesAndSeeksRealtimeReplay();
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

void CanReplaySourceTest::pausesAndSeeksRealtimeReplay() {
    qRegisterMetaType<miata::data::CanFrameRecord>();
    miata::data::CanReplaySource replay;
    QString error;
    QVERIFY2(replay.load(QStringLiteral(MIATA_TEST_REPLAY_PATH), &error), qPrintable(error));
    QCOMPARE(replay.durationNs(), 100'000'000LL);
    QVERIFY2(replay.setSpeedFactor(2.0, &error), qPrintable(error));
    QCOMPARE(replay.speedFactor(), 2.0);
    QVERIFY(!replay.setSpeedFactor(0.0, &error));
    QVERIFY(!replay.setSpeedFactor(101.0, &error));

    QSignalSpy frames(&replay, &miata::data::CanReplaySource::frameReceived);
    QSignalSpy finished(&replay, &miata::data::CanReplaySource::replayFinished);
    QVERIFY(replay.start(miata::data::CanReplaySource::TimingMode::Realtime, 2.0, &error));
    QTRY_VERIFY_WITH_TIMEOUT(frames.count() >= 1, 500);
    replay.pause();
    QVERIFY(replay.isRunning());
    QVERIFY(replay.isPaused());
    const int pausedFrameCount = frames.count();
    QTest::qWait(120);
    QCOMPARE(frames.count(), pausedFrameCount);

    QVERIFY2(replay.seekToNs(50'000'000LL, &error), qPrintable(error));
    QCOMPARE(replay.positionNs(), 50'000'000LL);
    replay.resume();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 1000);
    QCOMPARE(frames.count(), pausedFrameCount + 2);
    QCOMPARE(replay.positionNs(), replay.durationNs());
}

QTEST_MAIN(CanReplaySourceTest)
#include "can_replay_source_test.moc"
