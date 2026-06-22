#pragma once

#include <QObject>
#include <QString>
#include <QCryptographicHash>

QT_BEGIN_NAMESPACE
class QNetworkAccessManager;
class QNetworkReply;
class QFile;
QT_END_NAMESPACE

// Downloads a single file via QNetworkAccessManager: streams to a ".part" temp
// file, reports progress, verifies a SHA-256 checksum, and renames into place on
// success.  Network failures (offline, DNS, refused, HTTP error) are reported
// through finished(false, …) rather than crashing.
class ModelDownloader : public QObject
{
    Q_OBJECT
public:
    explicit ModelDownloader(QObject* parent = nullptr);
    ~ModelDownloader() override;

    // Download `url` to `destPath`.  Written atomically: streamed to
    // destPath + ".part", then renamed on success.  `expectedSha256` (lowercase
    // hex) is verified before the rename; pass empty to skip verification.
    // Only one download at a time per instance.
    void start(const QString& url, const QString& destPath,
               const QString& expectedSha256 = QString());

    void cancel();

signals:
    // total is -1 when the server does not advertise a content length.
    void progress(qint64 received, qint64 total);
    // ok == true → file is in place and verified; otherwise error is non-empty.
    void finished(bool ok, const QString& error);

private slots:
    void onReadyRead();
    void onReplyFinished();

private:
    void fail(const QString& msg);
    void cleanup();

    QNetworkAccessManager* m_nam   = nullptr;
    QNetworkReply*         m_reply = nullptr;
    QFile*                 m_file  = nullptr;
    QString                m_destPath;
    QString                m_partPath;
    QString                m_expectedSha;
    QCryptographicHash     m_hash{QCryptographicHash::Sha256};
    bool                   m_canceled = false;
};
