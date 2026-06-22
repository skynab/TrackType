#pragma once
#include <QtGlobal>

#ifdef Q_OS_MAC
#include <QString>

// Sets NSWindowCollectionBehavior so the window stays visible during
// Mission Control / Exposé and follows the user across all Spaces.
// Safe to call on any platform (no-op outside macOS via the ifdef).
void applyMacOSWindowBehavior(quintptr winId);

// ── Text injection (CoreGraphics) ──────────────────────────────────────────
// These drive TextInjector's macOS backend.  All require the app to be granted
// Accessibility permission (System Settings → Privacy & Security → Accessibility).

// Type Unicode `text` into the focused app via CGEventKeyboardSetUnicodeString.
void macTypeUnicode(const QString& text);

// Synthesize `count` Backspace key presses (used to retract live partials).
void macSendBackspaces(int count);

// Synthesize `count` Left-arrow key presses (caret placement).
void macSendLeftArrows(int count);

// Synthesize Cmd+V (the clipboard-paste fallback shortcut).
void macSendPasteShortcut();

// ── Global hotkey (Carbon RegisterEventHotKey) ─────────────────────────────
// Isolated here so Carbon headers stay out of the cross-platform GlobalHotkey.
typedef void (*MacHotkeyCallback)(bool pressed, void* userData);

// Register a global hotkey (mac virtual keyCode + Carbon modifier mask).  Returns
// an opaque handle, or nullptr on failure.  `cb` is invoked on the main thread.
void* macRegisterHotkey(unsigned int keyCode, unsigned int modifiers,
                        MacHotkeyCallback cb, void* userData);
void  macUnregisterHotkey(void* handle);

// Carbon modifier-mask constants (so callers needn't include Carbon).
unsigned int macCmdKey();
unsigned int macOptionKey();
unsigned int macControlKey();
unsigned int macShiftKey();
#endif
