#include <QtTest>
#include "audiocapture.h"

// Tests for AudioCapture and its PcmChunker accumulator.
//
// Audio is hardware/environment dependent (CI runs headless with no microphone),
// so the start/stop test is written to be correct whether or not an input device
// is present: it asserts state consistency and crash-free teardown rather than
// that bytes actually flow.  The chunker tests are fully deterministic.
class AudioCaptureTest : public QObject
{
    Q_OBJECT

private slots:
    void chunker_windowsAndOverlap();
    void chunker_reconfigureResets();
    void chunker_noWindowWhenUnconfigured();
    void startStop_loopIsClean();
};

void AudioCaptureTest::chunker_windowsAndOverlap()
{
    PcmChunker c;
    c.configure(/*window*/ 8, /*overlap*/ 2);   // hop = window - overlap = 6
    QCOMPARE(c.windowBytes(), 8);
    QCOMPARE(c.hopBytes(), 6);

    // 6 bytes is not yet a full window.
    QVERIFY(c.append(QByteArray(6, '\x01')).isEmpty());
    QCOMPARE(c.pending(), 6);

    // Two more bytes complete exactly one window; the hop drops 6, leaving 2
    // bytes of overlap as the seed for the next window.
    const QList<QByteArray> w1 = c.append(QByteArray(2, '\x02'));
    QCOMPARE(w1.size(), 1);
    QCOMPARE(w1.first().size(), 8);
    QCOMPARE(c.pending(), 2);

    // A single large push must yield every window that fits, in order.
    // pending 2 + 20 = 22 → windows at 22,16,10 (each removes 6) → 3 windows.
    const QList<QByteArray> w2 = c.append(QByteArray(20, '\x03'));
    QCOMPARE(w2.size(), 3);
    for (const QByteArray& w : w2)
        QCOMPARE(w.size(), 8);
    QCOMPARE(c.pending(), 4);
}

void AudioCaptureTest::chunker_reconfigureResets()
{
    PcmChunker c;
    c.configure(10, 0);
    c.append(QByteArray(5, '\0'));
    QCOMPARE(c.pending(), 5);

    // Reconfiguring clears buffered data and recomputes the hop.
    c.configure(4, 1);
    QCOMPARE(c.pending(), 0);
    QCOMPARE(c.hopBytes(), 3);

    // Overlap is clamped below the window size so the hop is always positive.
    c.configure(4, 99);
    QVERIFY(c.hopBytes() >= 1);
}

void AudioCaptureTest::chunker_noWindowWhenUnconfigured()
{
    PcmChunker c;   // window defaults to 0 → never emits
    QVERIFY(c.append(QByteArray(1000, '\x05')).isEmpty());
}

void AudioCaptureTest::startStop_loopIsClean()
{
    AudioCapture cap;
    QVERIFY(!cap.isCapturing());

    // Rapid open/close cycles must never crash and must always end stopped.
    // With synchronous teardown this also guarantees no audio backend thread is
    // left running between cycles.
    for (int i = 0; i < 50; ++i) {
        cap.start();
        QTest::qWait(1);            // let the backend spin up / deliver buffers
        cap.stop();
        QVERIFY(!cap.isCapturing());
    }

    // start() is idempotent — calling it twice must not create a second source.
    cap.start();
    const bool capturing = cap.isCapturing();
    cap.start();
    QCOMPARE(cap.isCapturing(), capturing);

    cap.stop();
    QVERIFY(!cap.isCapturing());
    cap.stop();                     // stopping when already stopped is a no-op
    QVERIFY(!cap.isCapturing());
}

QTEST_MAIN(AudioCaptureTest)
#include "tst_audiocapture.moc"
