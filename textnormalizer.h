#pragma once

#include <QString>

// Formats a stream of recognized speech segments for injection:
//   * inserts a single space between consecutive segments (but not before the
//     very first one),
//   * capitalizes the first letter of the document and of any segment that
//     follows sentence-ending punctuation (. ! ?).
//
// Pure logic, no platform/GUI dependencies, so it is unit-testable without a
// real focused window.  When disabled it passes segments through unchanged
// (preserving whatever spacing the recognizer produced).
class TextNormalizer
{
public:
    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const        { return m_enabled; }

    // Forget all state (call when starting a fresh dictation session).
    void reset();

    // Normalize a finalized segment and advance internal state.  Returns the
    // exact text that should be injected (may be empty for blank input).
    QString normalize(const QString& rawSegment);

    // Format a live (in-progress) segment the way normalize() would, WITHOUT
    // advancing state — for previewing partial results.
    QString previewPartial(const QString& rawSegment) const;

private:
    static QString applyFormat(const QString& seg, bool atStart, bool capitalize);

    bool m_enabled = true;
    bool m_atStart = true;   // nothing emitted yet → no leading space
    bool m_capNext = true;   // capitalize the next segment's first letter
};
