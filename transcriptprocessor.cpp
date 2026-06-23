#include "transcriptprocessor.h"
#include <QRegularExpression>

const QString TranscriptProcessor::CursorMark = QStringLiteral("{|}");

TranscriptProcessor::TranscriptProcessor()
    : m_commands(defaultCommands())
{
}

QMap<QString, QString> TranscriptProcessor::defaultCommands()
{
    QMap<QString, QString> m;

    // Punctuation
    m.insert(QStringLiteral("period"),             QStringLiteral("."));
    m.insert(QStringLiteral("comma"),              QStringLiteral(","));
    m.insert(QStringLiteral("question mark"),      QStringLiteral("?"));
    m.insert(QStringLiteral("exclamation mark"),   QStringLiteral("!"));
    m.insert(QStringLiteral("exclamation point"),  QStringLiteral("!"));
    m.insert(QStringLiteral("colon"),              QStringLiteral(":"));
    m.insert(QStringLiteral("semicolon"),          QStringLiteral(";"));
    m.insert(QStringLiteral("open paren"),         QStringLiteral("("));
    m.insert(QStringLiteral("close paren"),        QStringLiteral(")"));
    m.insert(QStringLiteral("quote"),              QStringLiteral("“"));
    m.insert(QStringLiteral("end quote"),          QStringLiteral("”"));

    // Whitespace / control
    m.insert(QStringLiteral("new line"),           QStringLiteral("\n"));
    m.insert(QStringLiteral("enter"),              QStringLiteral("\n"));
    m.insert(QStringLiteral("tab"),                QStringLiteral("\t"));

    // Paired brackets with the caret placed between them.
    m.insert(QStringLiteral("brackets"), QStringLiteral("[") + CursorMark + QStringLiteral("]"));

    return m;
}

QString TranscriptProcessor::commandKey(const QString& raw)
{
    QString k = raw.trimmed().toLower();
    while (!k.isEmpty()) {
        const QChar c = k.at(k.size() - 1);
        if (c == '.' || c == ',' || c == '!' || c == '?')
            k.chop(1);
        else
            break;
    }
    return k.trimmed();
}

TranscriptProcessor::Result TranscriptProcessor::processFinal(const QString& rawTranscript)
{
    const QString key = commandKey(rawTranscript);

    // Reserved undo commands — checked before the user table so they cannot be
    // shadowed by a custom voice command with the same phrase.
    static const QString kScratchAliases[] = {
        QStringLiteral("scratch that"),
        QStringLiteral("delete that"),
        QStringLiteral("undo that"),
    };
    for (const QString& alias : kScratchAliases) {
        if (key == alias) {
            m_normalizer.reset();
            Result r;
            r.scratchThat = true;
            return r;
        }
    }

    // Reserved review-mode commands.
    static const QString kCommitAliases[] = {
        QStringLiteral("commit"),
        QStringLiteral("confirm"),
        QStringLiteral("accept"),
    };
    for (const QString& alias : kCommitAliases) {
        if (key == alias) {
            Result r;
            r.commitReview = true;
            return r;
        }
    }

    static const QString kCancelAliases[] = {
        QStringLiteral("cancel"),
        QStringLiteral("discard"),
        QStringLiteral("never mind"),
        QStringLiteral("nevermind"),
    };
    for (const QString& alias : kCancelAliases) {
        if (key == alias) {
            Result r;
            r.cancelReview = true;
            return r;
        }
    }

    const auto it = m_commands.constFind(key);
    if (it != m_commands.constEnd()) {
        QString out = it.value();
        Result r;
        const int mark = out.indexOf(CursorMark);
        if (mark >= 0) {
            out.remove(mark, CursorMark.size());
            r.cursorBack = out.size() - mark;   // characters after the caret position
        }
        r.text = out;
        // A command breaks the dictation flow; the next dictation segment starts
        // fresh (capitalize, no leading space).
        m_normalizer.reset();
        return r;
    }

    // Apply substitutions: case-insensitive whole-word replace, in definition order.
    QString cooked = rawTranscript;
    for (auto it = m_substitutions.constBegin(); it != m_substitutions.constEnd(); ++it) {
        const QRegularExpression re(
            QStringLiteral("\\b") + QRegularExpression::escape(it.key()) + QStringLiteral("\\b"),
            QRegularExpression::CaseInsensitiveOption);
        cooked.replace(re, it.value());
    }
    return Result{ m_normalizer.normalize(cooked), 0 };
}

QString TranscriptProcessor::previewPartial(const QString& rawTranscript) const
{
    return m_normalizer.previewPartial(rawTranscript);
}
