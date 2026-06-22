#include "macos_utils.h"
#ifdef Q_OS_MAC

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#include <string>

void applyMacOSWindowBehavior(quintptr winId)
{
    // winId() on macOS is an NSView*
    NSView*   view   = reinterpret_cast<NSView*>(winId);
    NSWindow* window = [view window];
    if (!window) return;

    // Stationary  — stays visible and doesn't zoom out in Mission Control / Exposé
    // CanJoinAllSpaces — appears on every virtual desktop / Space
    window.collectionBehavior =
        NSWindowCollectionBehaviorStationary |
        NSWindowCollectionBehaviorCanJoinAllSpaces;
}

// ── Text injection ──────────────────────────────────────────────────────────
// kVK_ANSI_V == 9, kVK_Delete (Backspace) == 51 (stable Carbon virtual keycodes).
static const CGKeyCode kVkV         = 9;
static const CGKeyCode kVkBackspace = 51;

void macTypeUnicode(const QString& text)
{
    if (text.isEmpty()) return;

    const std::u16string s = text.toStdU16String();
    const UniChar* chars   = reinterpret_cast<const UniChar*>(s.data());
    const UniCharCount len = static_cast<UniCharCount>(s.size());

    // A key event carrying the Unicode string inserts the whole string,
    // independent of the keyboard layout.
    CGEventRef down = CGEventCreateKeyboardEvent(nullptr, 0, true);
    CGEventRef up   = CGEventCreateKeyboardEvent(nullptr, 0, false);
    if (down && up) {
        CGEventKeyboardSetUnicodeString(down, len, chars);
        CGEventKeyboardSetUnicodeString(up,   len, chars);
        CGEventPost(kCGHIDEventTap, down);
        CGEventPost(kCGHIDEventTap, up);
    }
    if (down) CFRelease(down);
    if (up)   CFRelease(up);
}

void macSendBackspaces(int count)
{
    for (int i = 0; i < count; ++i) {
        CGEventRef down = CGEventCreateKeyboardEvent(nullptr, kVkBackspace, true);
        CGEventRef up   = CGEventCreateKeyboardEvent(nullptr, kVkBackspace, false);
        if (down) { CGEventPost(kCGHIDEventTap, down); CFRelease(down); }
        if (up)   { CGEventPost(kCGHIDEventTap, up);   CFRelease(up); }
    }
}

void macSendPasteShortcut()
{
    CGEventRef down = CGEventCreateKeyboardEvent(nullptr, kVkV, true);
    CGEventRef up   = CGEventCreateKeyboardEvent(nullptr, kVkV, false);
    if (down) {
        CGEventSetFlags(down, kCGEventFlagMaskCommand);
        CGEventPost(kCGHIDEventTap, down);
        CFRelease(down);
    }
    if (up) {
        CGEventSetFlags(up, kCGEventFlagMaskCommand);
        CGEventPost(kCGHIDEventTap, up);
        CFRelease(up);
    }
}

#endif // Q_OS_MAC
