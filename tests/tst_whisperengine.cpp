#include <QtTest>
#include <QSignalSpy>
#include <QFile>
#include "whisperengine.h"

#ifndef TT_FIXTURE_DIR
#  define TT_FIXTURE_DIR ""
#endif

// Integration test: feed a short speech WAV through WhisperSttEngine end to end
// and assert the recognized text contains an expected word.
//
// Needs a real whisper model and a speech WAV.  Both are taken from the
// environment so CI can supply them without committing large binaries:
//   TRACKTYPE_TEST_MODEL — path to a ggml model (e.g. ggml-tiny.en.bin)
//   TRACKTYPE_TEST_WAV   — path to a 16 kHz mono 16-bit PCM WAV (falls back to
//                          <fixtures>/jfk.wav if that file is present)
// When neither is available (typical local dev) the test is skipped.
class WhisperEngineTest : public QObject
{
    Q_OBJECT

private slots:
    void recognizesSpeechFromWav();

private:
    // Minimal RIFF/WAVE reader: returns the PCM bytes of the "data" chunk.
    static QByteArray readWavPcm(const QString& path);
};

QByteArray WhisperEngineTest::readWavPcm(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QByteArray all = f.readAll();
    if (all.size() < 44 || all.left(4) != "RIFF" || all.mid(8, 4) != "WAVE")
        return {};

    // Walk the chunks looking for "data".
    int pos = 12;
    while (pos + 8 <= all.size()) {
        const QByteArray id = all.mid(pos, 4);
        const quint32 sz = quint32(quint8(all[pos + 4]))
                         | (quint32(quint8(all[pos + 5])) << 8)
                         | (quint32(quint8(all[pos + 6])) << 16)
                         | (quint32(quint8(all[pos + 7])) << 24);
        pos += 8;
        if (id == "data")
            return all.mid(pos, int(qMin<qint64>(sz, all.size() - pos)));
        pos += int(sz) + (int(sz) & 1);   // chunks are word-aligned
    }
    return {};
}

void WhisperEngineTest::recognizesSpeechFromWav()
{
    const QString model = qEnvironmentVariable("TRACKTYPE_TEST_MODEL");
    if (model.isEmpty() || !QFile::exists(model))
        QSKIP("TRACKTYPE_TEST_MODEL not set/found — skipping whisper integration test.");

    QString wav = qEnvironmentVariable("TRACKTYPE_TEST_WAV");
    if (wav.isEmpty())
        wav = QStringLiteral(TT_FIXTURE_DIR "/jfk.wav");
    if (wav.isEmpty() || !QFile::exists(wav))
        QSKIP("No speech WAV available — skipping whisper integration test.");

    const QByteArray pcm = readWavPcm(wav);
    QVERIFY2(!pcm.isEmpty(), "could not read PCM from WAV fixture");

    WhisperSttEngine engine;
    engine.setLanguage("en");

    QSignalSpy loaded(&engine, &SttEngine::modelLoaded);
    QSignalSpy errors(&engine, &SttEngine::errorOccurred);
    engine.loadModel(model);
    QVERIFY2(loaded.wait(60000) || !errors.isEmpty(), "model did not load");
    QVERIFY2(errors.isEmpty(), "engine reported an error while loading the model");
    QVERIFY(engine.isModelLoaded());

    engine.start();
    QVERIFY(engine.isRunning());

    QSignalSpy finals(&engine, &SttEngine::finalTranscript);

    // Feed the speech, then ~1 s of silence so the VAD finalizes the utterance.
    engine.feedAudio(pcm);
    QTest::qWait(50);
    engine.feedAudio(QByteArray(16000 * 2, '\0'));   // 1 s of silence at 16 kHz s16

    QVERIFY2(finals.wait(120000), "no finalTranscript was emitted in time");

    QString text;
    for (const QList<QVariant>& sig : finals)
        text += sig.at(0).toString() + ' ';

    QVERIFY2(text.contains("country", Qt::CaseInsensitive),
             qPrintable(QStringLiteral("unexpected transcript: '%1'").arg(text)));
}

QTEST_MAIN(WhisperEngineTest)
#include "tst_whisperengine.moc"
