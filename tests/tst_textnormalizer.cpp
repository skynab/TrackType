#include <QtTest>
#include "textnormalizer.h"

// Unit tests for TextNormalizer (spacing + capitalization).  Pure logic — no
// focused window, no platform input injection required.
class TextNormalizerTest : public QObject
{
    Q_OBJECT

private slots:
    void capitalizesFirstSegment();
    void insertsSpaceBetweenSegments();
    void capitalizesAfterSentenceEnd();
    void trimsAndCollapsesWhitespace();
    void disabledIsPassthrough();
    void previewDoesNotMutateState();
    void blankSegmentsIgnored();
};

void TextNormalizerTest::capitalizesFirstSegment()
{
    TextNormalizer n;                 // enabled by default
    QCOMPARE(n.normalize("hello world"), QStringLiteral("Hello world"));
}

void TextNormalizerTest::insertsSpaceBetweenSegments()
{
    TextNormalizer n;
    QCOMPARE(n.normalize("hello"), QStringLiteral("Hello"));   // first: no leading space
    QCOMPARE(n.normalize("world"), QStringLiteral(" world"));  // subsequent: leading space
    QCOMPARE(n.normalize("again"), QStringLiteral(" again"));
}

void TextNormalizerTest::capitalizesAfterSentenceEnd()
{
    TextNormalizer n;
    QCOMPARE(n.normalize("first"),  QStringLiteral("First"));
    QCOMPARE(n.normalize("done."),  QStringLiteral(" done."));   // ends a sentence
    QCOMPARE(n.normalize("next"),   QStringLiteral(" Next"));    // → capitalized
    QCOMPARE(n.normalize("more?"),  QStringLiteral(" more?"));
    QCOMPARE(n.normalize("yes"),    QStringLiteral(" Yes"));     // after '?'
}

void TextNormalizerTest::trimsAndCollapsesWhitespace()
{
    TextNormalizer n;
    QCOMPARE(n.normalize("  spaced  "), QStringLiteral("Spaced"));
    QCOMPARE(n.normalize("  tail "),    QStringLiteral(" tail"));
}

void TextNormalizerTest::disabledIsPassthrough()
{
    TextNormalizer n;
    n.setEnabled(false);
    // Whatever the recognizer produced is returned verbatim.
    QCOMPARE(n.normalize(" and so"), QStringLiteral(" and so"));
    QCOMPARE(n.normalize("more"),    QStringLiteral("more"));
}

void TextNormalizerTest::previewDoesNotMutateState()
{
    TextNormalizer n;
    QCOMPARE(n.normalize("hello"), QStringLiteral("Hello"));

    // Previewing a partial must not advance state...
    QCOMPARE(n.previewPartial("wor"),   QStringLiteral(" wor"));
    QCOMPARE(n.previewPartial("world"), QStringLiteral(" world"));
    // ...so the eventual final still gets exactly one leading space.
    QCOMPARE(n.normalize("world"), QStringLiteral(" world"));
}

void TextNormalizerTest::blankSegmentsIgnored()
{
    TextNormalizer n;
    QCOMPARE(n.normalize("   "), QString());
    // A blank segment must not consume the document-start state.
    QCOMPARE(n.normalize("start"), QStringLiteral("Start"));
}

QTEST_APPLESS_MAIN(TextNormalizerTest)
#include "tst_textnormalizer.moc"
