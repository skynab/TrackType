#include "aicleanup.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>

AiCleanup::AiCleanup(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

void AiCleanup::clean(const QString& text)
{
    if (m_apiKey.isEmpty()) {
        emit failed(tr("No Claude API key configured"));
        return;
    }

    QString system =
        QStringLiteral("You are correcting raw speech-to-text output. "
                       "Fix transcription errors, proper nouns, and punctuation. "
                       "Output only the corrected text — no explanation, no surrounding quotes.");

    if (!m_vocabulary.isEmpty())
        system += QStringLiteral("\n\nPreferred spellings and proper nouns: ") + m_vocabulary;

    QJsonObject msg;
    msg[QStringLiteral("role")]    = QStringLiteral("user");
    msg[QStringLiteral("content")] = text;

    QJsonObject body;
    body[QStringLiteral("model")]      = QStringLiteral("claude-haiku-4-5-20251001");
    body[QStringLiteral("max_tokens")] = 1024;
    body[QStringLiteral("system")]     = system;
    body[QStringLiteral("messages")]   = QJsonArray{ msg };

    QNetworkRequest req(QUrl(QStringLiteral("https://api.anthropic.com/v1/messages")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("x-api-key",         m_apiKey.toUtf8());
    req.setRawHeader("anthropic-version", "2023-06-01");

    auto* reply = m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit failed(reply->errorString());
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        const QString result = doc.object()
            .value(QStringLiteral("content")).toArray()
            .at(0).toObject()
            .value(QStringLiteral("text")).toString().trimmed();
        if (result.isEmpty())
            emit failed(tr("Empty or unexpected response from Claude API"));
        else
            emit cleaned(result);
    });
}
