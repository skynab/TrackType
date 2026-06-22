#include "whisperengine.h"

#include "audiocapture.h"   // kSampleRate / kBytesPerSample (PCM contract)
#include "whisper.h"

#include <QThread>
#include <QString>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  WhisperWorker — lives on the worker QThread; owns the whisper_context and runs
//  all blocking whisper.cpp calls.  Created on the main thread then moved to the
//  worker thread; every slot below therefore executes on the worker thread.
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

        whisper_context_params cparams = whisper_context_default_params();
        cparams.use_gpu = false;   // CPU-only build

        m_ctx = whisper_init_from_file_with_params(
            modelPath.toUtf8().constData(), cparams);

        if (m_ctx)
            emit modelLoaded();
        else
            emit errorOccurred(tr("Failed to load whisper model: %1").arg(modelPath));
    }

    // One analysis window of mono / 16 kHz / 16-bit PCM (an AudioCapture chunk).
    void process(const QByteArray& pcm)
    {
        if (m_ctx)
            runInference(pcm);
        emit chunkDone();   // always: keeps the engine's in-flight counter honest
    }

signals:
    void modelLoaded();
    void errorOccurred(const QString& message);
    void finalTranscript(const QString& text);
    void chunkDone();

private:
    void runInference(const QByteArray& pcm)
    {
        // int16 LE → float32 in [-1, 1], the format whisper expects.
        const int n = int(pcm.size() / AudioCapture::kBytesPerSample);
        if (n <= 0)
            return;
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
        p.single_segment   = true;    // one segment per window (streaming-style)
        p.no_context       = true;    // windows are processed independently
        p.translate        = false;
        p.language         = "en";
        p.suppress_blank   = true;

        if (whisper_full(m_ctx, p, f.data(), n) != 0) {
            emit errorOccurred(tr("whisper inference failed."));
            return;
        }

        QString text;
        const int segs = whisper_full_n_segments(m_ctx);
        for (int i = 0; i < segs; ++i)
            text += QString::fromUtf8(whisper_full_get_segment_text(m_ctx, i));

        text = text.trimmed();
        if (!text.isEmpty())
            emit finalTranscript(text);
    }

    whisper_context* m_ctx = nullptr;
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

    // Worker → engine: update state and re-emit on the owning thread.
    connect(m_worker, &WhisperWorker::modelLoaded, this, [this]{
        m_modelLoaded.store(true);
        emit modelLoaded();
    });
    connect(m_worker, &WhisperWorker::errorOccurred,
            this, &WhisperSttEngine::errorOccurred);
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
    // Drop rather than queue without bound when the recognizer falls behind.
    if (m_queued.load() >= kMaxQueued)
        return;
    m_queued.fetch_add(1);
    emit requestProcess(pcm);
}

#include "whisperengine.moc"
