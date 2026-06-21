#pragma once
#include <QPoint>

// NOTE: the platform mouse-injection implementation behind this class was
// stripped during the rewrite into a voice-to-text dictation tool; the methods
// are currently inert stubs (see clickinjector.cpp).  This file is kept as the
// per-platform seam that will be repurposed into a cross-platform text /
// keystroke injector.  The ClickType enum and modifier flags remain in use by
// the (soon to be removed) toolbar click buttons.

// All click types (legacy — pending removal with the toolbar click buttons)
enum class ClickType {
    None,
    NoClick,         // selected but performs no action
    LeftClick,
    LeftDoubleClick,
    LeftDown,       // begin drag
    LeftUp,         // end drag
    RightClick,
    RightDoubleClick,
    RightDown,
    RightUp,
    MiddleClick,
    MiddleDoubleClick,
    ScrollUp,
    ScrollDown,
    ScrollLeft,
    ScrollRight,
};

// Modifier flags (OR-able)
enum ModifierFlag {
    ModNone  = 0,
    ModCtrl  = 1 << 0,
    ModAlt   = 1 << 1,
    ModShift = 1 << 2,
};

class ClickInjector
{
public:
    // Perform a click at the given screen position with optional modifiers.
    // Modifiers is a bitmask of ModifierFlag values.
    static void performClick(ClickType type, QPoint pos, int modifiers = ModNone);

    // Move the system cursor to pos without clicking.
    static void moveCursor(QPoint pos);

    // Returns the global cursor position (currently QCursor::pos() on every
    // platform; still used by the window edge-lock feature).
    static QPoint cursorPos();

    // Retained for the Linux input-access prompt; the stub returns true on every
    // platform now that kernel pointer-motion tracking has been removed.
    static bool hasInputDeviceAccess();

private:
    static void pressModifiers(int modifiers);
    static void releaseModifiers(int modifiers);
};
