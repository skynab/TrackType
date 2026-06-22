#include "whisperengine.h"

#include "audiocapture.h"   // kSampleRate / kBytesPerSample (PCM contract)
#include "whisper.h"

#include <QThread>
#include <QString>
#include <cmath>
#include <vector>

namespace {
// Convert a byte count of the capture format to milliseconds.
inline int bytesToMs(int bytes)
{
    const int bytesPerMs =
        AudioCapture::kSampleRate * AudioCapture::kChannelCount
        * AudioCapture::kBytesPerSample / 1000;   // 32 bytes/ms at 16k mono s16
    return bytesPerMs > 0 ? bytes / bytesPerMs : 0;
}

// RMS amplitude (0..1) of an int16 buffer — the same energy metric the toolbar
// level meter shows; used here as a simple voice-activity detector.
float rms16(const QByteArray& pcm)
{
    const int n = int(pcm.size() / int(sizeof(qint16)));
    if (n <= 0)
        return 0.0f;
    const auto* s = reinterpret_cast<const qint16*>(pcm.constData());
    double sumsq = 0.0;
    for (int i = 0; i < n; ++i) {
        const double v = s[i] / 32768.0;
        sumsq += v * v;
    }
    return float(std::sqrt(sumsq / n));
}
} // namespace

// ─────────────────────────────────────────────────────────────────────────────
//  WhisperWorker — lives on the worker QThread; owns the whisper_context and runs
//  all blocking whisper.cpp calls.
//
//  Segmentation strategy (reduces whisper's chunked lag):
//   * An energy-threshold VAD splits the contiguous PCM stream into utterances.
//   * While speech continues, the growing utterance is re-decoded periodically
//     and emitted as partialTranscript (live, may change).
//   * After a short run of silence the utterance is finalized: decoded once more
//     and emitted as finalTranscript, then the buffer is reset.
// ─────────────────────────────────────────────────────────────────────────────
class WhisperWorker : public QObject
{
    Q_OBJECT
public:
    ~WhisperWorker() override
    {
        if (m_ctx) {
            whisper_free(m_ctx);
            m_ctx = nullptr;
        }
    }

public slots:
    void loadModel(const QString& modelPath)
    {
        if (m_ctx) {
            whisper_free(m_ctx);
            m_ctx = nullptr;
        }
        resetUtterance();

        whisper_context_params cparams = whisper_context_default_params();
        cparams.use_gpu = false;   // CPU-only build

        m_ctx = whisper_init_from_file_with_params(
            modelPath.toUtf8().constData(), cparams);

        if (m_ctx)
            emit modelLoaded();
        else
            emit errorOccurred(tr("Failed to load speech model: %1").arg(modelPath));
    }

    void setLanguage(const QString& lang)
    {
        m_language = lang.isEmpty() ? QStringLiteral("auto") : lang;
    }

    // A buffer of contiguous mono / 16 kHz / 16-bit PCM from AudioCapture.
    void process(const QByteArray& pcm)
    {
        if (m_ctx)
            ingest(pcm);
        emit chunkDone();   // always: keeps the engine's in-flight counter honest
    }

signals:
    void modelLoaded();
    void errorOccurred(const QString& message);
    void partialTranscript(const QString& text);
    void finalTranscript(const QString& text);
    void chunkDone();

private:
    // VAD / windowing tunables.
    static constexpr float kVadThreshold     = 0.015f;  // RMS above this = speech
    static constexpr int   kFinalizeSilenceMs = 700;    // trailing silence → finalize
    static constexpr int   kPartialIntervalMs = 800;    // min gap between partials
    static constexpr int   kMaxUtteranceMs    = 20000;  // force finalize past this
    static constexpr int   kMinUtteranceMs    = 300;    // ignore sub-blips

    void resetUtterance()
    {
        m_utter.clear();
        m_inSpeech       = false;
        m_silenceMs      = 0;
        m_msSincePartial = 0;
    }

    int utteranceMs() const { return bytesToMs(int(m_utter.size())); }

    void ingest(const QByteArray& pcm)
    {
        const int   durMs  = bytesToMs(int(pcm.size()));
        const bool  voiced = rms16(pcm) >= kVadThreshold;

        if (voiced) {
            m_inSpeech  = true;
            m_silenceMs = 0;
            m_utter.append(pcm);
            m_msSincePartial += durMs;

            if (m_msSincePartial >= kPartialIntervalMs) {
                const QString t = transcribe(m_utter, /*singleSegment*/ true);
                if (!t.isEmpty())
                    emit partialTranscript(t);
                m_msSincePartial = 0;
            }
            if (utteranceMs() >= kMaxUtteranceMs)
                finalizeUtterance();
        } else if (m_inSpeech) {
            // Keep a little trailing silence — it helps whisper close the segment.
            m_utter.append(pcm);
            m_silenceMs += durMs;
            if (m_silenceMs >= kFinalizeSilenceMs)
                finalizeUtterance();
        }
        // else: idle silence before any speech — ignore.
    }

    void finalizeUtterance()
    {
        if (utteranceMs() >= kMinUtteranceMs) {
            const QString t = transcribe(m_utter, /*singleSegment*/ false);
            if (!t.isEmpty())
                emit finalTranscript(t);
        }
        resetUtterance();
    }

    QString transcribe(const QByteArray& pcm, bool singleSegment)
    {
        const int n = int(pcm.size() / AudioCapture::kBytesPerSample);
        if (n <= 0 || !m_ctx)
            return {};

        // int16 LE → float32 in [-1, 1].
        const auto* s = reinterpret_cast<const qint16*>(pcm.constData());
        std::vector<float> f(n);
        for (int i = 0; i < n; ++i)
            f[i] = s[i] / 32768.0f;

        whisper_full_params p = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
        p.n_threads        = qBound(1, QThread::idealThreadCount(), 8);
        p.print_progress   = false;
        p.print_realtime   = false;
        p.print_special    = false;
        p.print_timestamps = false;
        p.no_timestamps    = true;
        p.single_segment   = singleSegment;
        p.no_context       = true;     // utterances are independent
        p.translate        = false;
        p.suppress_blank   = true;

        // Keep the language bytes alive for the duration of the call.
        const QByteArray lang = m_language.toUtf8();
        p.language = lang.constData();

        if (whisper_full(m_ctx, p, f.data(), n) != 0) {
            emit errorOccurred(tr("Speech recognition failed."));
            return {};
        }

        QString text;
        const int segs = whisper_full_n_segments(m_ctx);
        for (int i = 0; i < segs; ++i)
            text += QString::fromUtf8(whisper_full_get_segment_text(m_ctx, i));
        return text.trimmed();
    }

    whisper_context* m_ctx = nullptr;
    QString          m_language = QStringLiteral("en");

    QByteArray m_utter;            // current utterance PCM (contiguous)
    bool       m_inSpeech       = false;
    int        m_silenceMs      = 0;
    int        m_msSincePartial = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
//  WhisperSttEngine
// ─────────────────────────────────────────────────────────────────────────────
WhisperSttEngine::WhisperSttEngine(QObject* parent)
    : SttEngine(parent)
    , m_thread(new QThread(this))
    , m_worker(new WhisperWorker)
{
    m_worker->moveToThread(m_thread);
    // Worker is destroyed by the thread on shutdown (canonical QThread pattern).
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    // Engine → worker (queued across threads).
    connect(this, &WhisperSttEngine::requestLoadModel,
            m_worker, &WhisperWorker::loadModel);
    connect(this, &WhisperSttEngine::requestProcess,
            m_worker, &WhisperWorker::process);
    connect(this, &WhisperSttEngine::requestSetLanguage,
            m_worker, &WhisperWorker::setLanguage);

    // Worker → engine: update state and re-emit on the owning thread.
    connect(m_worker, &WhisperWorker::modelLoaded, this, [this]{
        m_modelLoaded.store(true);
        emit modelLoaded();
    });
    connect(m_worker, &WhisperWorker::errorOccurred,
            this, &WhisperSttEngine::errorOccurred);
    connect(m_worker, &WhisperWorker::partialTranscript,
            this, &WhisperSttEngine::partialTranscript);
    connect(m_worker, &WhisperWorker::finalTranscript,
            this, &WhisperSttEngine::finalTranscript);
    connect(m_worker, &WhisperWorker::chunkDone, this, [this]{
        if (m_queued.load() > 0)
            m_queued.fetch_sub(1);
    });

    m_thread->start();
}

WhisperSttEngine::~WhisperSttEngine()
{
    // Thread teardown also runs the worker's deferred deletion, which frees the
    // whisper_context in ~WhisperWorker.
    m_thread->quit();
    m_thread->wait();
}

void WhisperSttEngine::loadModel(const QString& modelPath)
{
    m_modelLoaded.store(false);
    emit requestLoadModel(modelPath);
}

void WhisperSttEngine::setLanguage(const QString& lang)
{
    emit requestSetLanguage(lang);
}

void WhisperSttEngine::start()
{
    if (!m_modelLoaded.load()) {
        emit errorOccurred(tr("Cannot start: speech model is not loaded yet."));
        return;
    }
    if (m_running.exchange(true))
        return;   // already running
    emit started();
}

void WhisperSttEngine::stop()
{
    if (!m_running.exchange(false))
        return;
    emit stopped();
}

void WhisperSttEngine::feedAudio(const QByteArray& pcm)
{
    if (!m_running.load() || !m_modelLoaded.load())
        return;
    // Most process() calls are cheap (VAD only); drop rather than grow without
    // bound if the recognizer falls behind during a decode burst.
    if (m_queued.load() >= kMaxQueued)
        return;
    m_queued.fetch_add(1);
    emit requestProcess(pcm);
}

#include "whisperengine.moc"
