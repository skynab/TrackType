#include "transcriptprocessor.h"

const QString TranscriptProcessor::CursorMark = QStringLiteral("{|}");

TranscriptProcessor::TranscriptProcessor()
    : m_commands(defaultCommands())
{
}

QMap<QString, QString> TranscriptProcessor::defaultCommands()
{
    QMap<QString, QString> m;
    m.insert(QStringLiteral("new line"), QStringLiteral("\n"));
    m.insert(QStringLiteral("period"),   QStringLiteral("."));
    // Brackets with the caret placed between them.
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

    return Result{ m_normalizer.normalize(rawTranscript), 0 };
}

QString TranscriptProcessor::previewPartial(const QString& rawTranscript) const
{
    return m_normalizer.previewPartial(rawTranscript);
}
