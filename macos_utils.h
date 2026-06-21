#pragma once
#include <QtGlobal>

#ifdef Q_OS_MAC
// Sets NSWindowCollectionBehavior so the window stays visible during
// Mission Control / Exposé and follows the user across all Spaces.
// Safe to call on any platform (no-op outside macOS via the ifdef).
void applyMacOSWindowBehavior(quintptr winId);
#endif
