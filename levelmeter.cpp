#include "levelmeter.h"

#include <QPropertyAnimation>
#include <QEasingCurve>
#include <cmath>

LevelMeter::LevelMeter(QWidget* parent)
    : QProgressBar(parent)
{
    setRange(0, 100);
    setValue(0);
    setTextVisible(false);
    setOrientation(Qt::Horizontal);
    setFixedSize(72, 12);
    setToolTip(tr("Microphone input level"));

    // Glide toward each new level rather than snapping with every buffer.
    m_anim = new QPropertyAnimation(this, "value", this);
    m_anim->setDuration(90);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
}

void LevelMeter::setLevel(qreal level)
{
    // Speech RMS is small; a sqrt curve keeps the meter lively in the normal
    // talking range without pinning to 100% on loud peaks.
    const qreal shaped = std::sqrt(qBound<qreal>(0.0, level, 1.0));
    const int   target = qBound(0, int(std::lround(shaped * 100.0)), 100);

    m_anim->stop();
    m_anim->setStartValue(value());
    m_anim->setEndValue(target);
    m_anim->start();
}
