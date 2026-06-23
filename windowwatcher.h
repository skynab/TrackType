#pragma once
#include <QObject>
#include <QString>

class QTimer;

// Polls the OS for the foreground window title at a fixed interval and emits
// windowTitleChanged() when it changes.  The poll is lightweight (a single
// platform API call per tick) and designed to be run on the main thread.
//
// On Wayland/non-X11 Linux the title is always empty (graceful degradation).
class WindowWatcher : public QObject
{
    Q_OBJECT
public:
    explicit WindowWatcher(QObject* parent = nullptr);
    ~WindowWatcher() override;

    void start(int intervalMs = 400);
    void stop();

    QString currentTitle() const { return m_lastTitle; }

signals:
    void windowTitleChanged(const QString& title);

private slots:
    void poll();

private:
    QString fetchTitle();

    QTimer* m_timer     = nullptr;
    QString m_lastTitle;

#ifdef PLATFORM_LINUX
    void* m_display = nullptr;   // Display*; kept open between polls
#endif
};
