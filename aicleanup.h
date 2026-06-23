#pragma once
#include <QObject>
#include <QNetworkAccessManager>

// Sends recognized text to the Claude API for post-processing cleanup.
// All API calls are asynchronous: clean() returns immediately and emits
// cleaned() or failed() when the response arrives.
class AiCleanup : public QObject
{
    Q_OBJECT
public:
    explicit AiCleanup(QObject* parent = nullptr);

    void setApiKey(const QString& key)       { m_apiKey    = key; }
    void setVocabulary(const QString& vocab) { m_vocabulary = vocab; }

    // POSTs text to the Claude API.  Emits cleaned() on success or failed()
    // on network/API error.  Only one call should be in flight at a time —
    // the caller is expected to guard against concurrent requests.
    void clean(const QString& text);

signals:
    void cleaned(const QString& result);
    void failed(const QString& error);

private:
    QNetworkAccessManager* m_nam       = nullptr;
    QString                m_apiKey;
    QString                m_vocabulary;
};
