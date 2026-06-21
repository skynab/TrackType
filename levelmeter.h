#pragma once

#include <QProgressBar>

QT_BEGIN_NAMESPACE
class QPropertyAnimation;
QT_END_NAMESPACE

// Small horizontal microphone-level meter for the toolbar.  It subclasses
// QProgressBar so it inherits the app-wide orange progress-bar style (the look
// reused from the old dwell countdown — see BASE_STYLE in mainwindow.cpp) and
// animates smoothly toward each new input level so the bar glides instead of
// flickering with every audio buffer.
class LevelMeter : public QProgressBar
{
    Q_OBJECT
public:
    explicit LevelMeter(QWidget* parent = nullptr);

public slots:
    // Normalised 0..1 amplitude (e.g. RMS) from AudioCapture::levelChanged.
    void setLevel(qreal level);

private:
    QPropertyAnimation* m_anim = nullptr;
};
