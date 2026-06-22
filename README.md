# TrackType

A cross-platform **voice-to-text dictation tool** for Windows, macOS, and Linux,
built in C++17 + Qt6. Speak into your microphone and TrackType types what you say
into whatever application currently has keyboard focus — your editor, browser,
chat window, or anywhere else you can type.

> **Status:** TrackType is being rebuilt from the cross-platform desktop
> skeleton of a former dwell-clicker (TrackClick). The shared foundation —
> frameless always-on-top toolbar, system tray, persistent settings, live
> multilingual UI, and the Windows/macOS/Linux build & packaging pipeline — is in
> place. The dictation engine (microphone capture → speech recognition → text
> injection) is being added incrementally. The build/requirements/structure
> sections below track the current code.

## Planned features

- **Speak to type** — dictate into any focused application
- **Floating toolbar** — frameless, always-on-top, draggable
- **Push-to-talk / toggle** — start and stop dictation by button or global hotkey
- **Live transcript preview** — see partial results before they're committed
- **Special words → characters** — say "brackets" to insert `[ ]`, "new line"
  for a line break, etc. (user-editable command map)
- **Multilingual UI** — English, Čeština, Français, Español, 中文简体, 日本語,
  한국어, and more; language switches live without restart
- **System tray** — minimize to tray; double-click to restore
- **Persistent settings** — window position and all preferences saved across sessions

## Requirements

| Platform | Requirement |
|---|---|
| All | Qt 6.2+ (Qt 5.12+ also supported), CMake 3.16+, C++17 compiler |
| Windows | MSVC 2019+ or MinGW-w64 |
| macOS | Xcode 13+; grant **Accessibility** permission so TrackType can type into other apps (and **Microphone** permission for dictation) |
| Linux | `libx11-dev` + `libxtst-dev` (Debian/Ubuntu) or `libX11-devel` + `libXtst-devel` (Fedora/RHEL); X11/XWayland required |

### Text injection & permissions

Recognized speech is typed into whatever window currently has focus
(`TextInjector`). Backends and their requirements:

| Platform | How text is injected | Requirement / limitation |
|---|---|---|
| Windows | `SendInput` with `KEYEVENTF_UNICODE` — layout-independent Unicode | none |
| macOS | `CGEventKeyboardSetUnicodeString` + `CGEventPost` | **Accessibility permission is required** (System Settings → Privacy & Security → Accessibility). Without it, keystrokes are silently dropped. |
| Linux (X11 / XWayland) | XTest, remapping a spare keycode per character for full Unicode | works for X11 apps and XWayland clients |
| Linux (native Wayland) | uinput fallback (ASCII only) — Wayland blocks synthetic XTest input to native clients | for reliable Unicode on Wayland, use **clipboard-paste** mode |

**Clipboard-paste mode** (Settings → *Type text by: Clipboard paste*) puts each
result on the clipboard and synthesizes Ctrl/Cmd+V. Use it where per-character
injection is unreliable (native Wayland, some Electron/Java apps). The previous
clipboard contents are restored shortly after each paste.

## Build

```bash
git clone <this repo>
cd TrackType

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

# Run
./build/TrackType              # Linux/macOS
.\build\Release\TrackType.exe  # Windows
```

To specify a custom Qt installation:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x.x/gcc_64
```

### Running the tests

```bash
cmake -B build-tests -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-tests --parallel
ctest --test-dir build-tests --output-on-failure
```

## Translations

All translation `.ts` files are embedded directly in the binary via
`resources.qrc` and parsed at runtime — **no external tools are required** for any
language to work. If Qt LinguistTools (`lrelease`) happen to be installed, CMake
also compiles the `.ts` files to binary `.qm` format, which loads slightly faster.

Translation files are searched in this order at startup and on every language
switch (first match wins):

| Priority | Location | Notes |
|---|---|---|
| 1 | `<user-data>/translations/` | Drop a file here to override the bundled copy |
| 2 | `<binary>/translations/` | Shipped alongside the executable |
| 3 | `<binary>/../translations/` | macOS app bundle |
| 4 | Embedded in binary | Built-in fallback, always present |

**User-data paths by platform:**

| Platform | Path |
|---|---|
| macOS | `~/Library/Application Support/TrackType/translations/` |
| Windows | `%LOCALAPPDATA%\TrackType\TrackType\translations\` |
| Linux | `~/.local/share/TrackType/translations/` |

Place a file named `tracktype_<code>.ts` (e.g. `tracktype_fr.ts`) in that
directory; the app picks it up the next time that language is selected — no
restart or rebuild needed. A compiled `.qm` file of the same stem also works and
loads slightly faster.

### Adding a new language

1. Create `translations/tracktype_<code>.ts` following the format of an existing file.
2. Add it to `resources.qrc` (under the `/translations` prefix) so it is embedded in the binary.
3. Add a `m_cmbLanguage->addItem(...)` entry in `SettingsDialog::buildUi()` (`settingsdialog.cpp`).
4. *(Optional)* Add it to the `TS_FILES` list in `CMakeLists.txt` so CMake compiles a `.qm` file when LinguistTools are available.

## Project Structure

```
TrackType/
├── CMakeLists.txt        — Build system
├── resources.qrc         — Embedded icons, sounds, and translation files
├── icons/                — SVG toolbar icons
├── sounds/               — Audio cues
├── translations/         — Qt Linguist .ts sources + runtime .ts parser
├── installer/            — Windows (WiX), Linux (.desktop, udev, deb) packaging
├── main.cpp              — Entry point
├── mainwindow.h/cpp      — Floating toolbar UI
├── textinjector.h/cpp    — Cross-platform Unicode text injector (types into the
│                            focused window: Win SendInput / macOS CGEvent / X11 XTest)
├── macos_utils.h/.mm     — macOS window-behaviour helper
├── settingsdialog.h/cpp  — Configuration dialog & AppSettings struct
└── tests/                — Unit tests (ctest)
```

## License

See [LICENSE](LICENSE).
