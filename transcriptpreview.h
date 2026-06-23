#pragma once

#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
class QLineEdit;
QT_END_NAMESPACE

// Frameless popup shown near the toolbar for live partial previews and (in
// review mode) for editing finalized text before it is injected.
//
// Partial mode — non-activating: the window never steals keyboard focus from
// the dictation target; Qt::WindowDoesNotAcceptFocus is set at the OS level.
//
// Review mode — editable: Qt::WindowDoesNotAcceptFocus is removed so the
// embedded QLineEdit can receive clicks and keyboard input.  The window still
// carries WA_ShowWithoutActivating so it doesn't grab focus by merely appearing.
class TranscriptPreview : public QWidget
{
    Q_OBJECT
public:
    explicit TranscriptPreview(QWidget* parent = nullptr);

    // Show a live partial transcript (non-editable, non-activating).
    void showText(const QString& text, const QRect& anchorGlobal);

    // Show finalized text for review (editable, white border + hint line).
    // The text is selected so the user can immediately type a replacement.
    void showPendingReview(const QString& text, const QRect& anchorGlobal);

    // Current content of the edit widget, trimmed.  Call this in
    // commitPendingReview() to capture any manual edits the user made.
    QString text() const;

signals:
    // Emitted when the user presses Enter while the review edit has focus.
    void commitRequested();

private:
    void setReviewMode(bool on);
    void reposition(const QRect& anchorGlobal);

    QLineEdit* m_edit        = nullptr;
    QLabel*    m_hintLabel   = nullptr;
    bool       m_reviewMode  = false;
};
