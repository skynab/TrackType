#pragma once

#include <QObject>
#include <QKeySequence>
#include <QAbstractNativeEventFilter>
#include <QtGlobal>

class X11HotkeyThread;   // Linux backend (defined in globalhotkey.cpp)

// System-wide hotkey: emits pressed()/released() for a key combination no matter
// which window has focus.
//   Windows — RegisterHotKey (press only; released() fires right after pressed(),
//             so push-to-talk degrades to toggle on Windows).
//   macOS   — Carbon RegisterEventHotKey (true press + release).
//   Linux   — X11 XGrabKey on a dedicated connection/thread (press + release;
//             X11 / XWayland only — pure Wayland has no global-hotkey API).
class GlobalHotkey : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
public:
    explicit GlobalHotkey(QObject* parent = nullptr);
    ~GlobalHotkey() override;

    // Register `seq` (its first chord) as the global hotkey; an empty sequence
    // clears it.  Returns true on success.
    bool setShortcut(const QKeySequence& seq);
    QKeySequence shortcut() const { return m_seq; }

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    bool nativeEventFilter(const QByteArray& type, void* message, qintptr* result) override;
#else
    bool nativeEventFilter(const QByteArray& type, void* message, long* result) override;
#endif

signals:
    void pressed();
    void released();

private slots:
    // Invoked (queued) from the Linux X11 grab thread to re-emit on this thread.
    void emitPressed()  { emit pressed(); }
    void emitReleased() { emit released(); }

private:
    bool registerNative(int qtKey, int qtModifiers);
    void unregisterNative();

    QKeySequence m_seq;

#if defined(PLATFORM_WINDOWS)
    int m_id = 1;
    bool m_registered = false;
#elif defined(PLATFORM_LINUX)
    X11HotkeyThread* m_thread = nullptr;
#elif defined(PLATFORM_MACOS)
    void* m_handle = nullptr;
#endif
};
