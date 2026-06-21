#include <QtTest>

// Placeholder test suite.  The original dwell-clicker tests were removed when
// the dwell-click engine was deleted during the rewrite into a voice-to-text
// dictation tool.  This keeps the CI test job green (ctest needs at least one
// test) until real coverage — audio capture, speech-to-text and text
// injection — is added in later steps.
class PlaceholderTest : public QObject
{
    Q_OBJECT

private slots:
    void sanity() { QVERIFY(true); }
};

QTEST_MAIN(PlaceholderTest)
#include "tst_placeholder.moc"
