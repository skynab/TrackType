#include "modeldownloader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

ModelDownloader::ModelDownloader(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

ModelDownloader::~ModelDownloader()
{
    cleanup();
}

void ModelDownloader::start(const QString& url, const QString& destPath,
                            const QString& expectedSha256)
{
    if (m_reply) {
        emit finished(false, tr("A download is already in progress."));
        return;
    }

    m_canceled    = false;
    m_destPath    = destPath;
    m_partPath    = destPath + ".part";
    m_expectedSha = expectedSha256.toLower();
    m_hash.reset();

    // Ensure the target directory exists.
    QDir().mkpath(QFileInfo(destPath).absolutePath());

    // Fresh temp file (discard any stale partial download).
    m_file = new QFile(m_partPath, this);
    if (!m_file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString err = m_file->errorString();
        cleanup();
        emit finished(false, tr("Cannot write to %1: %2").arg(m_partPath, err));
        return;
    }

    QNetworkRequest req{QUrl(url)};
    // HuggingFace 'resolve' URLs redirect to a CDN — follow redirects safely.
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, "TrackType");

    m_reply = m_nam->get(req);
    connect(m_reply, &QNetworkReply::readyRead, this, &ModelDownloader::onReadyRead);
    connect(m_reply, &QNetworkReply::finished,  this, &ModelDownloader::onReplyFinished);
    connect(m_reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total){ emit progress(received, total); });
}

void ModelDownloader::cancel()
{
    if (!m_reply)
        return;
    m_canceled = true;
    m_reply->abort();   // triggers onReplyFinished() with an OperationCanceledError
}

void ModelDownloader::onReadyRead()
{
    if (!m_reply || !m_file)
        return;
    const QByteArray chunk = m_reply->readAll();
    if (chunk.isEmpty())
        return;
    if (m_file->write(chunk) != chunk.size()) {
        // fail() disconnects the reply, so its pending finished() is ignored —
        // this guarantees a single finished(false) emission.
        fail(tr("Write error: %1").arg(m_file->errorString()));
        return;
    }
    m_hash.addData(chunk);
}

void ModelDownloader::onReplyFinished()
{
    if (!m_reply)
        return;

    // Drain anything still buffered before inspecting the result.  A write error
    // during the drain calls fail() and clears m_reply — bail out if so.
    onReadyRead();
    if (!m_reply)
        return;

    if (m_canceled) {
        fail(tr("Download canceled."));
        return;
    }
    if (m_reply->error() != QNetworkReply::NoError) {
        fail(m_reply->errorString());   // offline / DNS / refused / HTTP error
        return;
    }

    if (m_file) {
        m_file->flush();
        m_file->close();
    }

    // Verify checksum before publishing the file.
    if (!m_expectedSha.isEmpty()) {
        const QString got = QString::fromLatin1(m_hash.result().toHex());
        if (got != m_expectedSha) {
            fail(tr("Checksum mismatch — the downloaded model is corrupt."));
            return;
        }
    }

    // Atomically move into place (replace any existing file).
    QFile::remove(m_destPath);
    if (!QFile::rename(m_partPath, m_destPath)) {
        fail(tr("Could not finalize the downloaded file."));
        return;
    }

    cleanup();
    emit finished(true, QString());
}

void ModelDownloader::fail(const QString& msg)
{
    cleanup();
    QFile::remove(m_partPath);   // never leave a partial/corrupt file behind
    emit finished(false, msg);
}

void ModelDownloader::cleanup()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    if (m_file) {
        if (m_file->isOpen())
            m_file->close();
        m_file->deleteLater();
        m_file = nullptr;
    }
}
