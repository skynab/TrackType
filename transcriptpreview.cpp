#include "transcriptpreview.h"

#include <QLabel>
#include <QVBoxLayout>

static const char* kNormalStyle =
    "QWidget { background: #1A1A1A; border: 1px solid #FFA600; border-radius: 4px; }"
    "QLabel  { color: #FFA600; font-size: 12px; background: transparent; border: none; }";

static const char* kReviewStyle =
    "QWidget { background: #1A1A1A; border: 1px solid #FFFFFF; border-radius: 4px; }"
    "QLabel  { color: #FFFFFF; font-size: 12px; background: transparent; border: none; }"
    "QLabel#hint { color: #AAAAAA; font-size: 10px; }";

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
    layout->setSpacing(2);

    m_label = new QLabel;
    m_label->setWordWrap(true);
    m_label->setTextInteractionFlags(Qt::NoTextInteraction);
    layout->addWidget(m_label);

    m_hintLabel = new QLabel;
    m_hintLabel->setObjectName("hint");
    m_hintLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    m_hintLabel->setVisible(false);
    layout->addWidget(m_hintLabel);

    setStyleSheet(kNormalStyle);
    setMaximumWidth(360);
}

void TranscriptPreview::reposition(const QRect& anchorGlobal)
{
    const int w = qMax(width(), anchorGlobal.width());
    setFixedWidth(qMin(w, maximumWidth()));
    adjustSize();
    move(anchorGlobal.left(), anchorGlobal.bottom() + 4);
}

void TranscriptPreview::showText(const QString& text, const QRect& anchorGlobal)
{
    if (text.isEmpty()) {
        hide();
        return;
    }
    setStyleSheet(kNormalStyle);
    m_hintLabel->setVisible(false);
    m_label->setText(text);
    adjustSize();
    reposition(anchorGlobal);
    if (!isVisible())
        show();
}

void TranscriptPreview::showPendingReview(const QString& text, const QRect& anchorGlobal)
{
    setStyleSheet(kReviewStyle);
    m_label->setText(text);
    m_hintLabel->setText(tr("Hotkey or 'commit' to inject · 'cancel' to discard"));
    m_hintLabel->setVisible(true);
    adjustSize();
    reposition(anchorGlobal);
    if (!isVisible())
        show();
}
