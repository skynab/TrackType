#pragma once

#include <QtGlobal>
#include <QObject>
#include <QByteArray>
#include <QList>
#include <QString>

// QAudioSource / QMediaDevices / QAudioDevice are Qt6-only.  The dictation
// pipeline targets Qt6 (what CI builds), but the project still keeps a Qt5
// fallback in CMake, so guard the real implementation and provide a compile-safe
// stub under Qt5 (empty device list, no-op capture).
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#  define TT_HAVE_AUDIO_CAPTURE 1
#  include <QAudioFormat>
#  include <QAudioDevice>
#else
#  define TT_HAVE_AUDIO_CAPTURE 0
#endif

QT_BEGIN_NAMESPACE
class QAudioSource;
class QIODevice;
QT_END_NAMESPACE

// ─────────────────────────────────────────────────────────────────────────────
//  PcmChunker — fixed-size analysis-window accumulator with overlap.
//
//  The audio backend hands us buffers whose size and timing are dictated by the
//  device, not by what the STT engine wants.  PcmChunker decouples the two: it
//  behaves like a FIFO ring — raw PCM is appended as it arrives and emitted in
//  windowBytes-sized slices that advance by (windowBytes − overlapBytes) each
//  step, so consecutive windows share `overlapBytes` of trailing context.
//
//  Header-only and free of any QtMultimedia dependency so it can be unit-tested
//  on its own.
// ─────────────────────────────────────────────────────────────────────────────
class PcmChunker
{
public:
    // windowBytes: size of each emitted window.
    // overlapBytes: bytes shared with the previous window (clamped to
    //               0 ≤ overlap < window).  Clears any buffered data.
    void configure(int windowBytes, int overlapBytes)
    {
        m_windowBytes = qMax(0, windowBytes);
        const int ov  = qBound(0, overlapBytes, qMax(0, m_windowBytes - 1));
        m_hopBytes    = qMax(1, m_windowBytes - ov);
        m_buf.clear();
    }

    // Append raw PCM and return every window that became complete as a result.
    QList<QByteArray> append(const QByteArray& pcm)
    {
        QList<QByteArray> out;
        if (m_windowBytes <= 0)
            return out;
        m_buf.append(pcm);
        while (m_buf.size() >= m_windowBytes) {
            out.append(m_buf.left(m_windowBytes));
            m_buf.remove(0, m_hopBytes);
        }
        return out;
    }

    void reset()              { m_buf.clear(); }
    int  pending()     const  { return int(m_buf.size()); }
    int  windowBytes() const  { return m_windowBytes; }
    int  hopBytes()    const  { return m_hopBytes; }

private:
    QByteArray m_buf;
    int        m_windowBytes = 0;
    int        m_hopBytes    = 1;
};

// One selectable input (microphone) device, surfaced to the UI.
struct AudioInputDevice
{
    QString id;                 // stable, persistable identifier (hex of QAudioDevice::id())
    QString name;               // human-readable description for the dropdown
    bool    isDefault = false;  // true for the current system default input
};

// ─────────────────────────────────────────────────────────────────────────────
//  AudioCapture — captures mono / 16 kHz / 16-bit little-endian PCM (the format
//  whisper and Vosk expect) from a chosen input device and publishes it via Qt
//  signals: raw buffers, fixed STT windows, and a live input level.
// ─────────────────────────────────────────────────────────────────────────────
class AudioCapture : public QObject
{
    Q_OBJECT
public:
    // STT-facing capture format.
    static constexpr int kSampleRate     = 16000;
    static constexpr int kChannelCount   = 1;
    static constexpr int kBytesPerSample = 2;     // signed 16-bit

    // Analysis-window geometry handed to the STT engine: ~2 s windows with a
    // small overlap so word boundaries falling on a seam are not lost.
    static constexpr int kWindowMs  = 2000;
    static constexpr int kOverlapMs = 200;

    explicit AudioCapture(QObject* parent = nullptr);
    ~AudioCapture() override;

    // Currently-available input devices (the system default is flagged).
    static QList<AudioInputDevice> availableDevices();

    // Select the device by its persisted id.  An empty or unknown id falls back
    // to the system default.  If capture is running it is restarted on the new
    // device; otherwise the choice takes effect at the next start().
    void    setInputDevice(const QString& id);
    QString inputDeviceId() const { return m_requestedId; }

    bool isCapturing() const;

public slots:
    void start();
    void stop();

signals:
    // Raw PCM exactly as delivered by the device callback (mono/16k/16-bit).
    void bufferReady(const QByteArray& pcm);
    // Fixed-size analysis windows (window + small overlap) for the STT engine,
    // decoupled from the device callback cadence.
    void chunkReady(const QByteArray& pcm);
    // Normalised input level in [0,1] (RMS per buffer) for a live meter.
    void levelChanged(qreal level);
    // State-change notifications.
    void captureStarted();
    void captureStopped();
    // Capture could not run: no input device, or the device/format could not be
    // opened.  `message` is human-readable for the UI.
    void captureError(const QString& message);

private:
    void teardown();

    QString    m_requestedId;   // persisted id ("" → system default)
    PcmChunker m_chunker;

#if TT_HAVE_AUDIO_CAPTURE
    void  onReadyRead();
    void  resolveFormatAndDevice();
    void  processPcm(const QByteArray& pcm);
    qreal computeLevel(const QByteArray& pcm) const;

    QAudioFormat  m_format;
    QAudioDevice  m_device;
    QAudioSource* m_source = nullptr;
    QIODevice*    m_io     = nullptr;   // owned by m_source (pull-mode buffer)
#endif
};
