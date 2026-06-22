#include "audiocapture.h"

#include <cmath>

#if TT_HAVE_AUDIO_CAPTURE
#  include <QAudioSource>
#  include <QMediaDevices>
#  include <QIODevice>
#  include <QDebug>
#endif

namespace {
// Bytes occupied by `ms` milliseconds of the STT capture format.
int msToBytes(int ms)
{
    return AudioCapture::kSampleRate * AudioCapture::kChannelCount
         * AudioCapture::kBytesPerSample * ms / 1000;
}
} // namespace

AudioCapture::AudioCapture(QObject* parent)
    : QObject(parent)
{
    m_chunker.configure(msToBytes(kWindowMs), msToBytes(kOverlapMs));
}

AudioCapture::~AudioCapture()
{
    teardown();
}

void AudioCapture::setInputDevice(const QString& id)
{
    if (id == m_requestedId)
        return;
    m_requestedId = id;
    // Apply immediately when live so the meter/STT follow the new mic without a
    // manual restart; otherwise the id is picked up at the next start().
    if (isCapturing()) {
        stop();
        start();
    }
}

QList<AudioInputDevice> AudioCapture::availableDevices()
{
    QList<AudioInputDevice> list;
#if TT_HAVE_AUDIO_CAPTURE
    const QAudioDevice def = QMediaDevices::defaultAudioInput();
    const auto inputs = QMediaDevices::audioInputs();
    list.reserve(inputs.size());
    for (const QAudioDevice& d : inputs) {
        AudioInputDevice e;
        e.id        = QString::fromLatin1(d.id().toHex());
        e.name      = d.description();
        e.isDefault = (d.id() == def.id());
        list.append(e);
    }
#endif
    return list;
}

#if TT_HAVE_AUDIO_CAPTURE

bool AudioCapture::isCapturing() const
{
    return m_source != nullptr;
}

void AudioCapture::resolveFormatAndDevice()
{
    const QAudioDevice def = QMediaDevices::defaultAudioInput();
    m_device = def;
    if (!m_requestedId.isEmpty()) {
        const QByteArray want = QByteArray::fromHex(m_requestedId.toLatin1());
        for (const QAudioDevice& d : QMediaDevices::audioInputs()) {
            if (d.id() == want) { m_device = d; break; }
        }
    }

    m_format = QAudioFormat();
    m_format.setSampleRate(kSampleRate);
    m_format.setChannelCount(kChannelCount);
    m_format.setSampleFormat(QAudioFormat::Int16);

    // QAudioSource does not resample.  If the backend cannot deliver our exact
    // STT format, fall back to the device's preferred format so capture still
    // works; converting that to 16 kHz mono for the STT engine is a later step.
    if (!m_device.isNull() && !m_device.isFormatSupported(m_format)) {
        const QAudioFormat pref = m_device.preferredFormat();
        qWarning("AudioCapture: '%s' does not support 16 kHz mono Int16; "
                 "falling back to preferred format (%d Hz, %d ch). "
                 "Resampling for STT is not yet implemented.",
                 qPrintable(m_device.description()),
                 pref.sampleRate(), pref.channelCount());
        m_format = pref;
    }
}

void AudioCapture::start()
{
    if (m_source)
        return;   // already capturing — start() is idempotent

    resolveFormatAndDevice();
    if (m_device.isNull()) {
        qWarning("AudioCapture: no audio input device available.");
        emit captureError(tr("No microphone found."));
        return;
    }

    m_chunker.reset();
    m_source = new QAudioSource(m_device, m_format, this);
    m_io = m_source->start();   // pull mode: read from the returned QIODevice
    if (!m_io) {
        qWarning("AudioCapture: failed to start capture (state %d).",
                 int(m_source->state()));
        teardown();
        emit captureError(tr("Could not open the selected microphone."));
        return;
    }
    connect(m_io, &QIODevice::readyRead, this, [this]{ onReadyRead(); });
    emit captureStarted();
}

void AudioCapture::stop()
{
    if (!m_source)
        return;
    teardown();
    emit captureStopped();
}

void AudioCapture::teardown()
{
    if (m_io) {
        m_io->disconnect(this);
        m_io = nullptr;          // owned by m_source; do not delete separately
    }
    if (m_source) {
        m_source->stop();
        m_source->disconnect(this);
        // Delete synchronously (not deleteLater): QAudioSource's destructor tears
        // down the backend stream and its audio thread here and now, so rapid
        // start/stop toggling can never accumulate live sources or threads.
        delete m_source;
        m_source = nullptr;
    }
    m_chunker.reset();
}

void AudioCapture::onReadyRead()
{
    if (!m_io)
        return;
    const QByteArray pcm = m_io->readAll();
    if (!pcm.isEmpty())
        processPcm(pcm);
}

void AudioCapture::processPcm(const QByteArray& pcm)
{
    emit bufferReady(pcm);
    emit levelChanged(computeLevel(pcm));

    const QList<QByteArray> windows = m_chunker.append(pcm);
    for (const QByteArray& w : windows)
        emit chunkReady(w);
}

qreal AudioCapture::computeLevel(const QByteArray& pcm) const
{
    // RMS amplitude, normalised to [0,1].  Handles the primary Int16 path and
    // the Float fallback that some backends prefer.
    switch (m_format.sampleFormat()) {
    case QAudioFormat::Int16: {
        const int n = int(pcm.size() / int(sizeof(qint16)));
        if (n <= 0) return 0.0;
        const auto* s = reinterpret_cast<const qint16*>(pcm.constData());
        double sumsq = 0.0;
        for (int i = 0; i < n; ++i) {
            const double v = s[i] / 32768.0;
            sumsq += v * v;
        }
        return qBound(0.0, std::sqrt(sumsq / n), 1.0);
    }
    case QAudioFormat::Float: {
        const int n = int(pcm.size() / int(sizeof(float)));
        if (n <= 0) return 0.0;
        const auto* s = reinterpret_cast<const float*>(pcm.constData());
        double sumsq = 0.0;
        for (int i = 0; i < n; ++i)
            sumsq += double(s[i]) * double(s[i]);
        return qBound(0.0, std::sqrt(sumsq / n), 1.0);
    }
    default:
        return 0.0;
    }
}

#else  // ── Qt5 stub: no QAudioSource/QMediaDevices available ───────────────────

bool AudioCapture::isCapturing() const { return false; }
void AudioCapture::start() {}
void AudioCapture::stop()  {}
void AudioCapture::teardown() {}

#endif // TT_HAVE_AUDIO_CAPTURE
