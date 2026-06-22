#include <QtTest>
#include "transcriptprocessor.h"

// Deterministic tests for the command map + dictation passthrough.  No window,
// no recognizer — pure text transform, so it runs headless on every platform.
class TranscriptProcessorTest : public QObject
{
    Q_OBJECT

private slots:
    void builtinCommands();
    void commandIsCaseAndPunctuationInsensitive();
    void bracketsPlaceCursor();
    void dictationPassesThrough();
    void customCommandsOverride();
};

void TranscriptProcessorTest::builtinCommands()
{
    TranscriptProcessor p;
    QCOMPARE(p.processFinal("new line").text, QStringLiteral("\n"));
    QCOMPARE(p.processFinal("period").text,   QStringLiteral("."));
}

void TranscriptProcessorTest::commandIsCaseAndPunctuationInsensitive()
{
    TranscriptProcessor p;
    // The recognizer often returns capitalized text with trailing punctuation.
    QCOMPARE(p.processFinal("New line.").text, QStringLiteral("\n"));
    QCOMPARE(p.processFinal("  PERIOD ").text, QStringLiteral("."));
}

void TranscriptProcessorTest::bracketsPlaceCursor()
{
    TranscriptProcessor p;
    const TranscriptProcessor::Result r = p.processFinal("brackets");
    QCOMPARE(r.text, QStringLiteral("[]"));
    QCOMPARE(r.cursorBack, 1);   // caret moves left once → between the brackets
}

void TranscriptProcessorTest::dictationPassesThrough()
{
    TranscriptProcessor p;   // auto-format on by default
    // Not a command → normalized dictation.
    QCOMPARE(p.processFinal("hello world").text, QStringLiteral("Hello world"));
    QCOMPARE(p.processFinal("again").text,       QStringLiteral(" again"));
}

void TranscriptProcessorTest::customCommandsOverride()
{
    TranscriptProcessor p;
    QMap<QString, QString> cmds;
    cmds.insert("smiley", ":)");
    p.setCommands(cmds);
    QCOMPARE(p.processFinal("smiley").text, QStringLiteral(":)"));
    // "period" is no longer a command now → treated as dictation.
    QCOMPARE(p.processFinal("period").text, QStringLiteral("Period"));
}

QTEST_APPLESS_MAIN(TranscriptProcessorTest)
#include "tst_transcriptprocessor.moc"
