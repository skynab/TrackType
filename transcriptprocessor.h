#pragma once

#include <QString>
#include <QMap>
#include "textnormalizer.h"

// The single transform stage between recognition and injection:
//
//     SttEngine::finalTranscript → TranscriptProcessor → TextInjector
//
// processFinal() takes a finalized transcript and returns what to inject.  It is
// the seam where spoken commands are applied: a data-driven command map (spoken
// phrase → output text) is checked first; anything that isn't a command is
// treated as dictation (spacing/capitalization normalized).
//
// The command map is a plain QMap so it is trivial to extend (and is made
// user-editable via the settings "Voice commands" table).  A command's output
// may contain the cursor marker "{|}" to position the caret after injection
// (e.g. brackets → "[]" with the caret between the two).
class TranscriptProcessor
{
public:
    struct Result {
        QString text;            // text to inject ("" = inject nothing)
        int     cursorBack = 0;  // Left-arrow presses after injecting (caret placement)
    };

    TranscriptProcessor();

    void setAutoFormat(bool enabled) { m_normalizer.setEnabled(enabled); }
    void reset() { m_normalizer.reset(); }

    // Built-in proof-of-concept commands (spoken phrase → output).
    static QMap<QString, QString> defaultCommands();

    void setCommands(const QMap<QString, QString>& commands) { m_commands = commands; }
    QMap<QString, QString> commands() const { return m_commands; }

    // Cursor-position marker that may appear in a command's output.
    static const QString CursorMark;   // "{|}"

    // Transform a finalized transcript: a command if it matches, else dictation.
    Result processFinal(const QString& rawTranscript);

    // Live partial preview (dictation formatting only; commands resolve on final).
    QString previewPartial(const QString& rawTranscript) const;

private:
    // Normalize a transcript to a command-lookup key (trim, lowercase, drop
    // trailing punctuation the recognizer may append).
    static QString commandKey(const QString& raw);

    TextNormalizer        m_normalizer;
    QMap<QString, QString> m_commands;
};
