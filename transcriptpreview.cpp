#include "transcriptpreview.h"

#include <QLabel>
#include <QVBoxLayout>

TranscriptPreview::TranscriptPreview(QWidget* parent)
    : QWidget(parent,
              Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint
              | Qt::WindowDoesNotAcceptFocus)
{
    // Never activate/steal focus from the app being dictated into, and stay
    // visible while another app is frontmost (the dictation target has focus).
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    setFocusPolicy(Qt::NoFocus);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 5, 8, 5);

    m_label = new QLabel;
    m_label->setWordWrap(true);
    m_label->setTextInteractionFlags(Qt::NoTextInteraction);
    layout->addWidget(m_label);

    // Reuse the app's dark/orange palette (matches the toolbar tooltip style).
    setStyleSheet(
        "QWidget { background: #1A1A1A; border: 1px solid #FFA600; border-radius: 4px; }"
        "QLabel  { color: #FFA600; font-size: 12px; background: transparent; border: none; }"
    );
    setMaximumWidth(360);
}

void TranscriptPreview::showText(const QString& text, const QRect& anchorGlobal)
{
    if (text.isEmpty()) {
        hide();
        return;
    }
    m_label->setText(text);
    adjustSize();

    // Place just below the toolbar, left-aligned with it, clamped to its width.
    const int w = qMax(width(), anchorGlobal.width());
    setFixedWidth(qMin(w, maximumWidth()));
    adjustSize();
    move(anchorGlobal.left(), anchorGlobal.bottom() + 4);

    if (!isVisible())
        show();
}
