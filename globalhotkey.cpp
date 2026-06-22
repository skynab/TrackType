#include "globalhotkey.h"
#include <QGuiApplication>

// Extract the single chord (key + modifiers) from a QKeySequence portably.
static int firstChord(const QKeySequence& seq)
{
    if (seq.isEmpty())
        return 0;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return seq[0].toCombined();
#else
    return seq[0];
#endif
}

GlobalHotkey::GlobalHotkey(QObject* parent) : QObject(parent) {}

GlobalHotkey::~GlobalHotkey() { unregisterNative(); }

bool GlobalHotkey::setShortcut(const QKeySequence& seq)
{
    unregisterNative();
    m_seq = seq;
    const int combined = firstChord(seq);
    if (combined == 0)
        return true;   // cleared
    const int key  = combined & ~Qt::KeyboardModifierMask;
    const int mods = combined & Qt::KeyboardModifierMask;
    return registerNative(key, mods);
}

// ─────────────────────────────────────────────────────────────
//  WINDOWS — RegisterHotKey + WM_HOTKEY
// ─────────────────────────────────────────────────────────────
#if defined(PLATFORM_WINDOWS)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static UINT qtKeyToVk(int key)
{
    if (key >= Qt::Key_A && key <= Qt::Key_Z) return UINT('A' + (key - Qt::Key_A));
    if (key >= Qt::Key_0 && key <= Qt::Key_9) return UINT('0' + (key - Qt::Key_0));
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) return UINT(VK_F1 + (key - Qt::Key_F1));
    switch (key) {
        case Qt::Key_Space:  return VK_SPACE;
        case Qt::Key_Escape: return VK_ESCAPE;
        case Qt::Key_Tab:    return VK_TAB;
        default: return 0;
    }
}

bool GlobalHotkey::registerNative(int key, int mods)
{
    const UINT vk = qtKeyToVk(key);
    if (!vk)
        return false;
    UINT m = MOD_NOREPEAT;
    if (mods & Qt::ControlModifier) m |= MOD_CONTROL;
    if (mods & Qt::AltModifier)     m |= MOD_ALT;
    if (mods & Qt::ShiftModifier)   m |= MOD_SHIFT;
    if (mods & Qt::MetaModifier)    m |= MOD_WIN;
    if (!RegisterHotKey(nullptr, m_id, m, vk))
        return false;
    qApp->installNativeEventFilter(this);
    m_registered = true;
    return true;
}

void GlobalHotkey::unregisterNative()
{
    if (m_registered) {
        UnregisterHotKey(nullptr, m_id);
        qApp->removeNativeEventFilter(this);
        m_registered = false;
    }
}

bool GlobalHotkey::nativeEventFilter(const QByteArray& type, void* message, qintptr*)
{
    if (type == "windows_generic_MSG") {
        MSG* msg = static_cast<MSG*>(message);
        if (msg->message == WM_HOTKEY && int(msg->wParam) == m_id) {
            // RegisterHotKey reports only the press; emit both so toggle works
            // and push-to-talk at least starts (see header note).
            emit pressed();
            emit released();
        }
    }
    return false;
}

// ─────────────────────────────────────────────────────────────
//  LINUX — XGrabKey on a dedicated X connection / thread
// ─────────────────────────────────────────────────────────────
#elif defined(PLATFORM_LINUX)

#include <QThread>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <atomic>

static KeySym qtKeyToKeysym(int key)
{
    if (key >= Qt::Key_A && key <= Qt::Key_Z) return XK_a + (key - Qt::Key_A);
    if (key >= Qt::Key_0 && key <= Qt::Key_9) return XK_0 + (key - Qt::Key_0);
    if (key >= Qt::Key_F1 && key <= Qt::Key_F12) return XK_F1 + (key - Qt::Key_F1);
    switch (key) {
        case Qt::Key_Space:  return XK_space;
        case Qt::Key_Escape: return XK_Escape;
        default: return NoSymbol;
    }
}

static int x11SilentError(Display*, XErrorEvent*) { return 0; }

// Plain QThread (no Q_OBJECT / signals) to avoid a moc unit inside a
// platform-conditional .cpp.  It notifies the owner via queued invokeMethod.
class X11HotkeyThread : public QThread
{
public:
    GlobalHotkey* owner   = nullptr;
    KeySym        keysym  = NoSymbol;
    unsigned int  modmask = 0;
    std::atomic<bool> stop{false};

protected:
    void run() override
    {
        Display* dpy = XOpenDisplay(nullptr);
        if (!dpy)
            return;
        Window root = DefaultRootWindow(dpy);
        const KeyCode kc = XKeysymToKeycode(dpy, keysym);
        if (kc == 0) { XCloseDisplay(dpy); return; }

        // Grab with the common lock-modifier combinations so Caps/Num Lock don't
        // defeat the hotkey.
        const unsigned int locks[] = {0, LockMask, Mod2Mask, LockMask | Mod2Mask};
        XErrorHandler old = XSetErrorHandler(x11SilentError);
        for (unsigned int l : locks)
            XGrabKey(dpy, kc, modmask | l, root, False, GrabModeAsync, GrabModeAsync);
        XSync(dpy, False);
        XSetErrorHandler(old);

        while (!stop.load()) {
            while (XPending(dpy)) {
                XEvent ev;
                XNextEvent(dpy, &ev);
                if (ev.xkey.keycode != kc)
                    continue;
                if (ev.type == KeyPress) {
                    QMetaObject::invokeMethod(owner, "emitPressed", Qt::QueuedConnection);
                } else if (ev.type == KeyRelease) {
                    // Filter auto-repeat: a release immediately followed by a
                    // press of the same key at the same time is a repeat.
                    bool repeat = false;
                    if (XPending(dpy)) {
                        XEvent next;
                        XPeekEvent(dpy, &next);
                        if (next.type == KeyPress && next.xkey.keycode == kc
                            && next.xkey.time == ev.xkey.time) {
                            XNextEvent(dpy, &next);   // consume the repeat press
                            repeat = true;
                        }
                    }
                    if (!repeat)
                        QMetaObject::invokeMethod(owner, "emitReleased", Qt::QueuedConnection);
                }
            }
            msleep(15);
        }

        for (unsigned int l : locks)
            XUngrabKey(dpy, kc, modmask | l, root);
        XCloseDisplay(dpy);
    }
};

bool GlobalHotkey::registerNative(int key, int mods)
{
    const KeySym ks = qtKeyToKeysym(key);
    if (ks == NoSymbol)
        return false;
    unsigned int m = 0;
    if (mods & Qt::ControlModifier) m |= ControlMask;
    if (mods & Qt::AltModifier)     m |= Mod1Mask;
    if (mods & Qt::ShiftModifier)   m |= ShiftMask;
    if (mods & Qt::MetaModifier)    m |= Mod4Mask;

    m_thread = new X11HotkeyThread;
    m_thread->owner   = this;
    m_thread->keysym  = ks;
    m_thread->modmask = m;
    m_thread->start();
    return true;
}

void GlobalHotkey::unregisterNative()
{
    if (m_thread) {
        m_thread->stop.store(true);
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
    }
}

bool GlobalHotkey::nativeEventFilter(const QByteArray&, void*, qintptr*) { return false; }

// ─────────────────────────────────────────────────────────────
//  macOS — Carbon RegisterEventHotKey (in macos_utils.mm)
// ─────────────────────────────────────────────────────────────
#elif defined(PLATFORM_MACOS)

#include "macos_utils.h"

// Trampoline: Carbon calls this (on the main run loop) with our GlobalHotkey*.
static void macHotkeyTrampoline(bool pressed, void* userData)
{
    auto* self = static_cast<GlobalHotkey*>(userData);
    if (pressed) emit self->pressed();
    else         emit self->released();
}

static uint32_t qtKeyToMacVk(int key)
{
    // Positional virtual keycodes (Carbon kVK_ANSI_*).
    static const int letters[26] = {
        0,11,8,2,14,3,5,4,34,38,40,37,46,45,31,35,12,15,1,17,32,9,13,7,16,6 };
    if (key >= Qt::Key_A && key <= Qt::Key_Z) return uint32_t(letters[key - Qt::Key_A]);
    static const int digits[10] = {29,18,19,20,21,23,22,26,28,25};
    if (key >= Qt::Key_0 && key <= Qt::Key_9) return uint32_t(digits[key - Qt::Key_0]);
    if (key == Qt::Key_Space) return 49;
    return 0xFFFFFFFFu;   // unmapped
}

bool GlobalHotkey::registerNative(int key, int mods)
{
    const uint32_t vk = qtKeyToMacVk(key);
    if (vk == 0xFFFFFFFFu)
        return false;
    uint32_t m = 0;
    if (mods & Qt::ControlModifier) m |= macControlKey();
    if (mods & Qt::AltModifier)     m |= macOptionKey();
    if (mods & Qt::ShiftModifier)   m |= macShiftKey();
    if (mods & Qt::MetaModifier)    m |= macCmdKey();
    m_handle = macRegisterHotkey(vk, m, &macHotkeyTrampoline, this);
    return m_handle != nullptr;
}

void GlobalHotkey::unregisterNative()
{
    if (m_handle) { macUnregisterHotkey(m_handle); m_handle = nullptr; }
}

bool GlobalHotkey::nativeEventFilter(const QByteArray&, void*, qintptr*) { return false; }

// ─────────────────────────────────────────────────────────────
//  Fallback (unsupported platform)
// ─────────────────────────────────────────────────────────────
#else

bool GlobalHotkey::registerNative(int, int) { return false; }
void GlobalHotkey::unregisterNative() {}
bool GlobalHotkey::nativeEventFilter(const QByteArray&, void*, qintptr*) { return false; }

#endif
