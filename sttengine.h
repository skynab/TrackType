#pragma once

#include <QObject>
#include <QByteArray>
#include <QString>

// ─────────────────────────────────────────────────────────────────────────────
//  SttEngine — abstract speech-to-text backend interface.
//
//  This is the swap seam between the audio pipeline and a concrete recognizer
//  (whisper.cpp, Vosk, a cloud API, …).  The capture and UI layers talk only to
//  this interface, so the backend can be replaced without touching either.  A
//  typical wiring connects AudioCapture::chunkReady → SttEngine::feedAudio and
//  SttEngine::partialTranscript/finalTranscript → the text-injection layer.
//
//  Audio contract: feedAudio() expects the same PCM that AudioCapture produces —
//  mono, 16 kHz, signed 16-bit little-endian (see AudioCapture::kSampleRate /
//  kChannelCount / kBytesPerSample).  A backend that needs a different rate or
//  sample format must convert internally.
//
//  Threading: an implementation may run recognition on a worker thread.  Signals
//  are delivered to the thread that owns the SttEngine (normal Qt queued
//  connections); the public methods are expected to be invoked from that same
//  owning thread.
// ─────────────────────────────────────────────────────────────────────────────
class SttEngine : public QObject
{
    Q_OBJECT
public:
    explicit SttEngine(QObject* parent = nullptr) : QObject(parent) {}
    ~SttEngine() override = default;

    // ── Model lifecycle ───────────────────────────────────────
    // Begin loading the recognition model from `modelPath` (a file or directory;
    // interpretation is backend-specific).  Loading may be asynchronous: the
    // outcome is reported via modelLoaded() on success or errorOccurred() on
    // failure.  Calling start() before the model is ready is an error.
    virtual void loadModel(const QString& modelPath) = 0;

    // True once a model is loaded and the engine is ready to recognize.
    virtual bool isModelLoaded() const = 0;

    // True between a successful start() and the matching stop().
    virtual bool isRunning() const = 0;

public slots:
    // Begin a recognition session.  Requires a loaded model; emits
    // errorOccurred() if called before one is ready, otherwise started().
    virtual void start() = 0;

    // End the recognition session, flushing any buffered audio so a trailing
    // finalTranscript() may still be emitted before stopped().
    virtual void stop() = 0;

    // Feed one buffer/window of PCM (mono / 16 kHz / 16-bit) into the recognizer.
    // Ignored when the engine is not running.
    virtual void feedAudio(const QByteArray& pcm) = 0;

signals:
    // Interim, still-changing hypothesis for the in-progress utterance.  May be
    // emitted repeatedly and superseded before the matching finalTranscript().
    void partialTranscript(const QString& text);

    // Stable, finalized text for a completed utterance/segment.
    void finalTranscript(const QString& text);

    // loadModel() completed successfully; the engine is now ready.
    void modelLoaded();

    // Recognition-session state transitions.
    void started();
    void stopped();

    // A recoverable or fatal problem.  `message` is human-readable so the UI can
    // surface it directly (e.g. model file missing, backend init failed).
    void errorOccurred(const QString& message);
};
