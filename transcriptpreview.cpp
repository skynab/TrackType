#include "transcriptpreview.h"

#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

// Orange-border style used while showing live partials.
static const char* kNormalStyle =
    "QWidget   { background: #1A1A1A; border: 1px solid #FFA600; border-radius: 4px; }"
    "QLineEdit { color: #FFA600; font-size: 12px; background: transparent; border: none; }"
    "QLabel    { color: #FFA600; font-size: 12px; background: transparent; border: none; }";

// White-border style used while holding text for review / editing.
static const char* kReviewStyle =
    "QWidget   { background: #1A1A1A; border: 1px solid #FFFFFF; border-radius: 4px; }"
    "QLineEdit { color: #FFFFFF; font-size: 12px; background: transparent; border: none;"
    "            selection-background-color: #FFFFFF; selection-color: #1A1A1A; }"
    "QLabel    { color: #FFFFFF; font-size: 12px; background: transparent; border: none; }"
    "QLabel#hint { color: #AAAAAA; font-size: 10px; }";

// Base window flags shared by both modes.  Qt::WindowDoesNotAcceptFocus is
// added/removed dynamically via setReviewMode().
static const Qt::WindowFlags kBaseFlags =
    Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint;

TranscriptPreview::TranscriptPreview(QWidget* parent)
    : QWidget(parent, kBaseFlags | Qt::WindowDoesNotAcceptFocus)
{
    // Prevent the popup from grabbing focus when it first appears.
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 5, 8, 5);
    layout->setSpacing(2);

    m_edit = new QLineEdit;
    m_edit->setReadOnly(true);
    m_edit->setFocusPolicy(Qt::NoFocus);
    m_edit->setFrame(false);
    layout->addWidget(m_edit);

    connect(m_edit, &QLineEdit::returnPressed, this, [this]() {
        emit commitRequested();
    });

    m_hintLabel = new QLabel;
    m_hintLabel->setObjectName("hint");
    m_hintLabel->setWordWrap(true);
    m_hintLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    m_hintLabel->setVisible(false);
    layout->addWidget(m_hintLabel);

    setStyleSheet(kNormalStyle);
    setMaximumWidth(360);
}

QString TranscriptPreview::text() const
{
    return m_edit->text().trimmed();
}

void TranscriptPreview::setReviewMode(bool on)
{
    if (m_reviewMode == on)
        return;
    m_reviewMode = on;

    const Qt::WindowFlags newFlags = on ? kBaseFlags : kBaseFlags | Qt::WindowDoesNotAcceptFocus;
    // setWindowFlags() hides the window; the caller's show() restores visibility.
    setWindowFlags(newFlags);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);

    m_edit->setReadOnly(!on);
    m_edit->setFocusPolicy(on ? Qt::ClickFocus : Qt::NoFocus);
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
    setReviewMode(false);   // restores WindowDoesNotAcceptFocus if it was removed
    setStyleSheet(kNormalStyle);
    m_hintLabel->setVisible(false);
    m_edit->setText(text);
    adjustSize();
    reposition(anchorGlobal);
    if (!isVisible())
        show();
}

void TranscriptPreview::showPendingReview(const QString& text, const QRect& anchorGlobal)
{
    setReviewMode(true);    // removes WindowDoesNotAcceptFocus so QLineEdit can receive input
    setStyleSheet(kReviewStyle);
    m_edit->setText(text);
    m_edit->selectAll();    // ready to type a replacement immediately
    m_hintLabel->setText(tr("Click to edit · Enter or hotkey to commit · 'cancel' to discard"));
    m_hintLabel->setVisible(true);
    adjustSize();
    reposition(anchorGlobal);
    show();                 // always show (setWindowFlags hides the window)
}
