#pragma once

#include "sttengine.h"
#include <QString>
#include <atomic>

QT_BEGIN_NAMESPACE
class QThread;
QT_END_NAMESPACE

class WhisperWorker;   // defined in whisperengine.cpp

// SttEngine backed by whisper.cpp.  All model loading and inference run on a
// dedicated worker thread so neither audio capture nor the UI ever block on the
// (CPU-heavy) recognizer.  feedAudio() hands PCM to the worker via a queued
// connection; transcripts come back as queued signals on the owning thread.
class WhisperSttEngine : public SttEngine
{
    Q_OBJECT
public:
    explicit WhisperSttEngine(QObject* parent = nullptr);
    ~WhisperSttEngine() override;

    void loadModel(const QString& modelPath) override;
    bool isModelLoaded() const override { return m_modelLoaded.load(); }
    bool isRunning() const override     { return m_running.load(); }

public slots:
    void start() override;
    void stop() override;
    void feedAudio(const QByteArray& pcm) override;

signals:
    // Internal: marshalled to the worker thread via queued connections.
    void requestLoadModel(const QString& modelPath);
    void requestProcess(const QByteArray& pcm);

private:
    // Cap on in-flight chunks so a recognizer slower than real time cannot grow
    // an unbounded backlog — excess chunks are dropped (this is a live STT, not
    // a transcription job).
    static constexpr int kMaxQueued = 4;

    QThread*          m_thread = nullptr;
    WhisperWorker*    m_worker = nullptr;
    std::atomic<bool> m_modelLoaded{false};
    std::atomic<bool> m_running{false};
    std::atomic<int>  m_queued{0};
};
