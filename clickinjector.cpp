#include "clickinjector.h"
#include <QCursor>

// The platform mouse-event injection that used to live here — Windows SendInput,
// macOS CoreGraphics CGEvent, and the Linux uinput / XTest / evdev machinery —
// was removed during the rewrite from a dwell-clicker into a voice-to-text
// dictation tool.
//
// The per-platform #ifdef scaffolding below is kept deliberately: it is the
// seam that will be repurposed into a cross-platform text / keystroke injector
// that types recognised speech into the currently focused window (each backend
// becomes a typeText() implementation — Windows SendInput with
// KEYEVENTF_UNICODE, macOS CGEventKeyboardSetUnicodeString in a .mm file, Linux
// XTest / uinput).
//
// Until then the click/move/modifier methods are inert no-ops.  cursorPos() and
// hasInputDeviceAccess() keep simple working implementations because the window
// edge-lock and the Linux permission prompt still reference them.

// ─────────────────────────────────────────────────────────────
//  WINDOWS
// ─────────────────────────────────────────────────────────────
#if defined(PLATFORM_WINDOWS)

void   ClickInjector::pressModifiers(int)   {}
void   ClickInjector::releaseModifiers(int) {}
void   ClickInjector::moveCursor(QPoint)    {}
QPoint ClickInjector::cursorPos()           { return QCursor::pos(); }
bool   ClickInjector::hasInputDeviceAccess(){ return true; }
void   ClickInjector::performClick(ClickType, QPoint, int) {}

// ─────────────────────────────────────────────────────────────
//  macOS   (CoreGraphics injection — to be repurposed into typeText)
// ─────────────────────────────────────────────────────────────
#elif defined(PLATFORM_MACOS)

void   ClickInjector::pressModifiers(int)   {}
void   ClickInjector::releaseModifiers(int) {}
void   ClickInjector::moveCursor(QPoint)    {}
QPoint ClickInjector::cursorPos()           { return QCursor::pos(); }
bool   ClickInjector::hasInputDeviceAccess(){ return true; }
void   ClickInjector::performClick(ClickType, QPoint, int) {}

// ─────────────────────────────────────────────────────────────
//  Linux   (X11 / Wayland injection — to be repurposed into typeText)
// ─────────────────────────────────────────────────────────────
#elif defined(PLATFORM_LINUX)

void   ClickInjector::pressModifiers(int)   {}
void   ClickInjector::releaseModifiers(int) {}
void   ClickInjector::moveCursor(QPoint)    {}
QPoint ClickInjector::cursorPos()           { return QCursor::pos(); }
bool   ClickInjector::hasInputDeviceAccess(){ return true; }
void   ClickInjector::performClick(ClickType, QPoint, int) {}

// ─────────────────────────────────────────────────────────────
//  Fallback (unsupported platform — no-op)
// ─────────────────────────────────────────────────────────────
#else

void   ClickInjector::pressModifiers(int)   {}
void   ClickInjector::releaseModifiers(int) {}
void   ClickInjector::moveCursor(QPoint)    {}
QPoint ClickInjector::cursorPos()           { return QCursor::pos(); }
bool   ClickInjector::hasInputDeviceAccess(){ return true; }
void   ClickInjector::performClick(ClickType, QPoint, int) {}

#endif
