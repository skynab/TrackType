#include "textnormalizer.h"

namespace {
// True if the last non-space character of `s` ends a sentence.
bool endsSentence(const QString& s)
{
    for (int i = s.size() - 1; i >= 0; --i) {
        const QChar c = s.at(i);
        if (!c.isSpace())
            return c == '.' || c == '!' || c == '?';
    }
    return false;
}
} // namespace

void TextNormalizer::reset()
{
    m_atStart = true;
    m_capNext = true;
}

QString TextNormalizer::applyFormat(const QString& seg, bool atStart, bool capitalize)
{
    QString s = seg;
    if (capitalize && !s.isEmpty())
        s[0] = s.at(0).toUpper();
    return atStart ? s : (QStringLiteral(" ") + s);
}

QString TextNormalizer::normalize(const QString& rawSegment)
{
    if (!m_enabled) {
        // Pass-through: keep the recognizer's own text/spacing.
        if (!rawSegment.isEmpty())
            m_atStart = false;
        return rawSegment;
    }

    const QString seg = rawSegment.trimmed();
    if (seg.isEmpty())
        return {};

    const QString out = applyFormat(seg, m_atStart, m_capNext);
    m_atStart = false;
    m_capNext = endsSentence(seg);
    return out;
}

QString TextNormalizer::previewPartial(const QString& rawSegment) const
{
    if (!m_enabled)
        return rawSegment;

    const QString seg = rawSegment.trimmed();
    if (seg.isEmpty())
        return {};
    return applyFormat(seg, m_atStart, m_capNext);
}
