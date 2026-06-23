#include "windowwatcher.h"
#include <QTimer>

#if defined(PLATFORM_WINDOWS)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <string>
#elif defined(PLATFORM_MACOS)
#  include "macos_utils.h"
#elif defined(PLATFORM_LINUX)
#  include <X11/Xlib.h>
#  include <X11/Xatom.h>
#endif

WindowWatcher::WindowWatcher(QObject* parent)
    : QObject(parent)
{
#ifdef PLATFORM_LINUX
    m_display = XOpenDisplay(nullptr);   // nullptr on Wayland — graceful degradation
#endif
}

WindowWatcher::~WindowWatcher()
{
#ifdef PLATFORM_LINUX
    if (m_display)
        XCloseDisplay(reinterpret_cast<Display*>(m_display));
#endif
}

void WindowWatcher::start(int intervalMs)
{
    if (!m_timer) {
        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, &WindowWatcher::poll);
    }
    m_timer->start(intervalMs);
}

void WindowWatcher::stop()
{
    if (m_timer)
        m_timer->stop();
}

void WindowWatcher::poll()
{
    const QString title = fetchTitle();
    if (title != m_lastTitle) {
        m_lastTitle = title;
        emit windowTitleChanged(title);
    }
}

QString WindowWatcher::fetchTitle()
{
#if defined(PLATFORM_WINDOWS)

    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return {};
    const int len = GetWindowTextLengthW(hwnd);
    if (len <= 0) return {};
    std::wstring buf(static_cast<size_t>(len) + 1, L'\0');
    GetWindowTextW(hwnd, buf.data(), len + 1);
    return QString::fromStdWString(buf.substr(0, static_cast<size_t>(len)));

#elif defined(PLATFORM_MACOS)

    return macFrontWindowTitle();

#elif defined(PLATFORM_LINUX)

    Display* dpy = reinterpret_cast<Display*>(m_display);
    if (!dpy) return {};

    const Window root        = DefaultRootWindow(dpy);
    const Atom netActiveWin  = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
    const Atom netWmName     = XInternAtom(dpy, "_NET_WM_NAME",       False);
    const Atom utf8String    = XInternAtom(dpy, "UTF8_STRING",         False);

    Atom         actualType;
    int          actualFormat;
    unsigned long nitems, bytesAfter;
    unsigned char* prop = nullptr;

    // Read the active window ID from the root window property
    if (XGetWindowProperty(dpy, root, netActiveWin, 0, 1, False,
                           XA_WINDOW, &actualType, &actualFormat,
                           &nitems, &bytesAfter, &prop) != Success || !prop)
        return {};

    const Window activeWin = *reinterpret_cast<Window*>(prop);
    XFree(prop);
    prop = nullptr;

    if (activeWin == None) return {};

    // Prefer _NET_WM_NAME (UTF-8)
    if (XGetWindowProperty(dpy, activeWin, netWmName, 0, 4096, False,
                           utf8String, &actualType, &actualFormat,
                           &nitems, &bytesAfter, &prop) == Success && prop) {
        QString title = QString::fromUtf8(reinterpret_cast<const char*>(prop));
        XFree(prop);
        return title;
    }

    // Fall back to WM_NAME (Latin-1)
    char* name = nullptr;
    if (XFetchName(dpy, activeWin, &name) && name) {
        QString title = QString::fromLocal8Bit(name);
        XFree(name);
        return title;
    }

    return {};

#else
    return {};
#endif
}
