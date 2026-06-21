#include "macos_utils.h"
#ifdef Q_OS_MAC

#import <AppKit/AppKit.h>

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

#endif // Q_OS_MAC
