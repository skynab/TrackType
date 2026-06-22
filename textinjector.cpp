#include "textinjector.h"

#include <QString>
#include <QGuiApplication>
#include <QClipboard>
#include <QTimer>

// ─────────────────────────────────────────────────────────────────────────────
//  Common (platform-independent) — mode state + dispatch + clipboard paste.
//  Kept ABOVE the per-platform blocks so the Qt headers are parsed before any
//  X11 headers (whose macros — None/Bool/… — would otherwise clash with Qt).
// ─────────────────────────────────────────────────────────────────────────────
namespace {
TextInjector::Mode g_mode = TextInjector::Mode::Type;
}

void TextInjector::setMode(Mode mode) { g_mode = mode; }
TextInjector::Mode TextInjector::mode() { return g_mode; }

void TextInjector::typeText(const QString& text)
{
    if (text.isEmpty())
        return;

    if (g_mode == Mode::ClipboardPaste) {
        QClipboard* cb = QGuiApplication::clipboard();
        const QString previous = cb->text();
        cb->setText(text);
        sendPasteShortcut();
        // Restore the user's previous clipboard shortly after the paste lands.
        QTimer::singleShot(300, cb, [previous]() {
            QGuiApplication::clipboard()->setText(previous);
        });
        return;
    }

    injectUnicode(text);
}

// ─────────────────────────────────────────────────────────────
//  WINDOWS — SendInput
// ─────────────────────────────────────────────────────────────
#if defined(PLATFORM_WINDOWS)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vector>

void TextInjector::injectUnicode(const QString& text)
{
    const int n = text.size();
    if (n <= 0)
        return;

    // One key-down + key-up per UTF-16 code unit; surrogate pairs are sent as
    // two consecutive KEYEVENTF_UNICODE events, which Windows recombines.
    std::vector<INPUT> events;
    events.reserve(size_t(n) * 2);
    for (int i = 0; i < n; ++i) {
        INPUT down{};
        down.type       = INPUT_KEYBOARD;
        down.ki.wScan   = WORD(text.at(i).unicode());
        down.ki.dwFlags = KEYEVENTF_UNICODE;
        events.push_back(down);

        INPUT up = down;
        up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        events.push_back(up);
    }
    SendInput(UINT(events.size()), events.data(), sizeof(INPUT));
}

void TextInjector::sendBackspaces(int count)
{
    if (count <= 0)
        return;
    std::vector<INPUT> events;
    events.reserve(size_t(count) * 2);
    for (int i = 0; i < count; ++i) {
        INPUT down{};
        down.type   = INPUT_KEYBOARD;
        down.ki.wVk = VK_BACK;
        events.push_back(down);

        INPUT up = down;
        up.ki.dwFlags = KEYEVENTF_KEYUP;
        events.push_back(up);
    }
    SendInput(UINT(events.size()), events.data(), sizeof(INPUT));
}

void TextInjector::sendPasteShortcut()
{
    INPUT in[4] = {};
    in[0].type = INPUT_KEYBOARD; in[0].ki.wVk = VK_CONTROL;
    in[1].type = INPUT_KEYBOARD; in[1].ki.wVk = 'V';
    in[2].type = INPUT_KEYBOARD; in[2].ki.wVk = 'V';        in[2].ki.dwFlags = KEYEVENTF_KEYUP;
    in[3].type = INPUT_KEYBOARD; in[3].ki.wVk = VK_CONTROL; in[3].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(4, in, sizeof(INPUT));
}

// ─────────────────────────────────────────────────────────────
//  macOS — CoreGraphics (implemented in macos_utils.mm)
// ─────────────────────────────────────────────────────────────
#elif defined(PLATFORM_MACOS)

#include "macos_utils.h"

void TextInjector::injectUnicode(const QString& text) { macTypeUnicode(text); }
void TextInjector::sendBackspaces(int count)          { macSendBackspaces(count); }
void TextInjector::sendPasteShortcut()                { macSendPasteShortcut(); }

// ─────────────────────────────────────────────────────────────
//  Linux — X11 XTest, with a best-effort uinput fallback
// ─────────────────────────────────────────────────────────────
#elif defined(PLATFORM_LINUX)

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>

#include <linux/uinput.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <cstring>

namespace {

// ── X11 helpers ──────────────────────────────────────────────
KeySym keysymForCodepoint(uint cp)
{
    switch (cp) {
        case '\n': case '\r': return XK_Return;
        case '\t':            return XK_Tab;
        case '\b':            return XK_BackSpace;
        default: break;
    }
    if (cp < 0x100)
        return KeySym(cp);
    return KeySym(0x01000000UL | cp);   // X11 Unicode keysym range
}

// Type via XTest by temporarily remapping a spare keycode to each keysym (so any
// character can be produced at modifier level 0).  Returns false if no X display.
bool x11Type(const QString& text)
{
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy)
        return false;

    int minKc = 0, maxKc = 0;
    XDisplayKeycodes(dpy, &minKc, &maxKc);
    int per = 0;
    KeySym* map = XGetKeyboardMapping(dpy, KeyCode(minKc), maxKc - minKc + 1, &per);

    int spare = 0;
    if (map && per > 0) {
        for (int kc = minKc; kc <= maxKc; ++kc) {
            bool empty = true;
            for (int j = 0; j < per; ++j)
                if (map[(kc - minKc) * per + j] != NoSymbol) { empty = false; break; }
            if (empty) { spare = kc; break; }
        }
    }
    if (map)
        XFree(map);
    if (spare == 0) { XCloseDisplay(dpy); return false; }

    const auto ucs4 = text.toUcs4();
    for (uint cp : ucs4) {
        KeySym ks = keysymForCodepoint(cp);
        XChangeKeyboardMapping(dpy, spare, 1, &ks, 1);
        XSync(dpy, False);
        XTestFakeKeyEvent(dpy, KeyCode(spare), True,  CurrentTime);
        XTestFakeKeyEvent(dpy, KeyCode(spare), False, CurrentTime);
        XFlush(dpy);
    }
    KeySym none = NoSymbol;
    XChangeKeyboardMapping(dpy, spare, 1, &none, 1);
    XSync(dpy, False);
    XCloseDisplay(dpy);
    return true;
}

bool x11FakeChord(KeySym mod, KeySym key, int repeat)
{
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy)
        return false;
    const KeyCode modKc = mod ? XKeysymToKeycode(dpy, mod) : 0;
    const KeyCode keyKc = XKeysymToKeycode(dpy, key);
    for (int i = 0; i < repeat; ++i) {
        if (modKc) XTestFakeKeyEvent(dpy, modKc, True, CurrentTime);
        XTestFakeKeyEvent(dpy, keyKc, True,  CurrentTime);
        XTestFakeKeyEvent(dpy, keyKc, False, CurrentTime);
        if (modKc) XTestFakeKeyEvent(dpy, modKc, False, CurrentTime);
    }
    XFlush(dpy);
    XCloseDisplay(dpy);
    return true;
}

// ── uinput fallback (ASCII / US layout, best-effort) ─────────
// Used only when there is no X display (e.g. pure Wayland without XWayland).
// uinput emits key *codes*, so it cannot produce arbitrary Unicode — only the
// ASCII subset below.  For full Unicode on Wayland, use clipboard-paste mode.
bool asciiToKey(QChar qc, int& code, bool& shift)
{
    static const int kLetter[26] = {
        KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J,
        KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T,
        KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z
    };
    const ushort u = qc.unicode();
    shift = false;
    if (u >= 'a' && u <= 'z') { code = kLetter[u - 'a']; return true; }
    if (u >= 'A' && u <= 'Z') { shift = true; code = kLetter[u - 'A']; return true; }
    if (u >= '1' && u <= '9') { code = KEY_1 + (u - '1'); return true; }
    if (u == '0')             { code = KEY_0; return true; }
    switch (u) {
        case ' ':  code = KEY_SPACE; return true;
        case '.':  code = KEY_DOT;   return true;
        case ',':  code = KEY_COMMA; return true;
        case '\n': case '\r': code = KEY_ENTER; return true;
        case '\t': code = KEY_TAB;   return true;
        default:   return false;     // unmapped — skip
    }
}

struct Uinput {
    int fd = -1;

    bool open()
    {
        fd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK);
        if (fd < 0)
            return false;
        ioctl(fd, UI_SET_EVBIT, EV_KEY);
        ioctl(fd, UI_SET_EVBIT, EV_SYN);
        for (int k = 1; k < 128; ++k)        // enable the whole low keycode range
            ioctl(fd, UI_SET_KEYBIT, k);

        struct uinput_setup us;
        std::memset(&us, 0, sizeof(us));
        us.id.bustype = BUS_USB;
        us.id.vendor  = 0x1234;
        us.id.product = 0x5678;
        std::strncpy(us.name, "TrackType virtual keyboard", sizeof(us.name) - 1);
        if (ioctl(fd, UI_DEV_SETUP, &us) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) {
            ::close(fd);
            fd = -1;
            return false;
        }
        usleep(50000);   // give udev time to create the device node
        return true;
    }

    // NB: not named emit() — that is a Qt keyword macro (expands to nothing).
    void writeEvent(int type, int code, int val)
    {
        struct input_event ev;
        std::memset(&ev, 0, sizeof(ev));
        ev.type = type; ev.code = code; ev.value = val;
        const ssize_t r = ::write(fd, &ev, sizeof(ev));
        (void)r;
    }
    void syn() { writeEvent(EV_SYN, SYN_REPORT, 0); }

    void tap(int code, bool shift)
    {
        if (shift) { writeEvent(EV_KEY, KEY_LEFTSHIFT, 1); syn(); }
        writeEvent(EV_KEY, code, 1); syn();
        writeEvent(EV_KEY, code, 0); syn();
        if (shift) { writeEvent(EV_KEY, KEY_LEFTSHIFT, 0); syn(); }
    }
    void tapWithCtrl(int code)
    {
        writeEvent(EV_KEY, KEY_LEFTCTRL, 1); syn();
        writeEvent(EV_KEY, code, 1); syn();
        writeEvent(EV_KEY, code, 0); syn();
        writeEvent(EV_KEY, KEY_LEFTCTRL, 0); syn();
    }
    void close()
    {
        if (fd >= 0) { ioctl(fd, UI_DEV_DESTROY); ::close(fd); fd = -1; }
    }
};

} // namespace

void TextInjector::injectUnicode(const QString& text)
{
    if (x11Type(text))
        return;   // X11 / XWayland path (full Unicode)

    // Fallback: uinput can only synthesize the ASCII subset (layout-dependent).
    Uinput kb;
    if (!kb.open())
        return;
    for (const QChar qc : text) {
        int code; bool shift;
        if (asciiToKey(qc, code, shift))
            kb.tap(code, shift);
    }
    kb.close();
}

void TextInjector::sendBackspaces(int count)
{
    if (count <= 0)
        return;
    if (x11FakeChord(0, XK_BackSpace, count))
        return;
    Uinput kb;
    if (!kb.open())
        return;
    for (int i = 0; i < count; ++i)
        kb.tap(KEY_BACKSPACE, false);
    kb.close();
}

void TextInjector::sendPasteShortcut()
{
    if (x11FakeChord(XK_Control_L, XK_v, 1))
        return;
    Uinput kb;
    if (!kb.open())
        return;
    kb.tapWithCtrl(KEY_V);
    kb.close();
}

// ─────────────────────────────────────────────────────────────
//  Fallback (unsupported platform)
// ─────────────────────────────────────────────────────────────
#else

void TextInjector::injectUnicode(const QString&) {}
void TextInjector::sendBackspaces(int)           {}
void TextInjector::sendPasteShortcut()           {}

#endif

// Common across platforms.
bool TextInjector::hasInputDeviceAccess() { return true; }
