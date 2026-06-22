#pragma once

#include <QString>

// Cross-platform Unicode text injector.  Types text into whatever window
// currently holds keyboard focus — the output stage of the dictation pipeline
// (SttEngine transcripts → TextInjector::typeText).
//
// Two injection modes:
//   Type           — synthesize per-character keystrokes (layout-independent
//                     Unicode): Windows SendInput KEYEVENTF_UNICODE, macOS
//                     CGEventKeyboardSetUnicodeString, Linux X11 XTest (uinput
//                     ASCII fallback when there is no X display).
//   ClipboardPaste — put the text on the clipboard and synthesize Ctrl/Cmd+V.
//                    For apps/platforms where per-character injection is
//                    unreliable (e.g. native Wayland, some Electron apps).
class TextInjector
{
public:
    enum class Mode { Type, ClipboardPaste };

    static void setMode(Mode mode);
    static Mode mode();

    // Inject `text` into the focused window using the current mode.  Accepts
    // arbitrary Unicode (including non-BMP).  No-op on unsupported platforms.
    static void typeText(const QString& text);

    // Synthesize `count` Backspace key presses (used to retract live partials).
    static void sendBackspaces(int count);

    // Whether synthetic input injection is available for this session.  Retained
    // for the Linux input-access prompt; currently a stub that returns true.
    static bool hasInputDeviceAccess();

private:
    static void injectUnicode(const QString& text);  // per-character (Type mode)
    static void sendPasteShortcut();                 // Ctrl/Cmd+V (paste mode)
};
