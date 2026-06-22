#pragma once

#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
QT_END_NAMESPACE

// A small frameless popup shown near the toolbar that previews the live partial
// transcript before it is committed.  Crucially it never takes keyboard focus
// (otherwise it would steal focus from the window being dictated into).
class TranscriptPreview : public QWidget
{
    Q_OBJECT
public:
    explicit TranscriptPreview(QWidget* parent = nullptr);

    // Update the previewed text and show the popup, positioned just below the
    // given (global) anchor rectangle — typically the main toolbar's geometry.
    void showText(const QString& text, const QRect& anchorGlobal);

private:
    QLabel* m_label = nullptr;
};
