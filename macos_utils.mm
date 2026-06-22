#include "macos_utils.h"
#ifdef Q_OS_MAC

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <Carbon/Carbon.h>
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

// ── Global hotkey ───────────────────────────────────────────────────────────
namespace {
struct MacHotkey {
    EventHotKeyRef    ref     = nullptr;
    EventHandlerRef   handler = nullptr;
    MacHotkeyCallback cb      = nullptr;
    void*             ud      = nullptr;
};

OSStatus macHotkeyHandler(EventHandlerCallRef, EventRef event, void* userData)
{
    auto* hk = static_cast<MacHotkey*>(userData);
    if (hk && hk->cb)
        hk->cb(GetEventKind(event) == kEventHotKeyPressed, hk->ud);
    return noErr;
}
} // namespace

void* macRegisterHotkey(unsigned int keyCode, unsigned int modifiers,
                        MacHotkeyCallback cb, void* userData)
{
    static UInt32 nextId = 1;
    auto* hk = new MacHotkey();
    hk->cb = cb;
    hk->ud = userData;

    EventTypeSpec types[2] = {
        { kEventClassKeyboard, kEventHotKeyPressed },
        { kEventClassKeyboard, kEventHotKeyReleased },
    };
    InstallEventHandler(GetApplicationEventTarget(),
                        NewEventHandlerUPP(macHotkeyHandler),
                        2, types, hk, &hk->handler);

    EventHotKeyID hkid;
    hkid.signature = 'ttHK';
    hkid.id        = nextId++;
    const OSStatus s = RegisterEventHotKey(keyCode, modifiers, hkid,
                                           GetApplicationEventTarget(), 0, &hk->ref);
    if (s != noErr) {
        if (hk->handler) RemoveEventHandler(hk->handler);
        delete hk;
        return nullptr;
    }
    return hk;
}

void macUnregisterHotkey(void* handle)
{
    auto* hk = static_cast<MacHotkey*>(handle);
    if (!hk) return;
    if (hk->ref)     UnregisterEventHotKey(hk->ref);
    if (hk->handler) RemoveEventHandler(hk->handler);
    delete hk;
}

unsigned int macCmdKey()     { return cmdKey; }
unsigned int macOptionKey()  { return optionKey; }
unsigned int macControlKey() { return controlKey; }
unsigned int macShiftKey()   { return shiftKey; }

#endif // Q_OS_MAC
