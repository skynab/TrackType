# Building TrackType

TrackType is a Qt 6 (C++17) app that vendors **whisper.cpp** for on-device
speech-to-text. It builds CPU-only on Windows, macOS, and Linux.

## Prerequisites

| Platform | Toolchain | Qt | Notes |
|---|---|---|---|
| Windows | MSVC 2019+ | Qt 6.2+ (Multimedia, Network, Svg) | WiX 4 only needed for the installer |
| macOS | Xcode 13+ | Qt 6.2+ | links Carbon (global hotkey) + ApplicationServices |
| Linux | GCC/Clang, CMake 3.16+ | Qt 6.2+ | `libx11-dev`, `libxtst-dev`, `libxi-dev` (X11/XTest injection + hotkey) |

whisper.cpp is vendored under `third_party/whisper/` and built as a static
library — no extra download or system package is required to compile it.

## Build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

The binary is `build/TrackType` (Linux), `build/TrackType.app` (macOS), or
`build/Release/TrackType.exe` (Windows).

## Tests

```sh
cmake -B build-tests -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-tests --config Debug --parallel
ctest --test-dir build-tests --output-on-failure
```

- `TextNormalizer`, `TranscriptProcessor`, `AudioCapture`, `Placeholder` run
  headless everywhere (set `QT_QPA_PLATFORM=offscreen`).
- The `Whisper` end-to-end test (WAV → engine → processor) only runs when a model
  and WAV are provided, so it is skipped unless you point it at them:

  ```sh
  TRACKTYPE_TEST_MODEL=/path/ggml-tiny.en.bin \
  TRACKTYPE_TEST_WAV=/path/jfk.wav \
  ctest --test-dir build-tests -R Whisper --output-on-failure
  ```

## Speech model

On first run the app downloads `ggml-base.en.bin` into the per-user data
directory (`QStandardPaths::AppLocalDataLocation/models/`) with a progress
dialog and SHA-256 verification; if offline it reports the failure and continues.
You can also drop a `ggml-*.bin` there by hand, or pick a different model in
**Settings → Speech model**.

## Microphone & input permissions

| Platform | Microphone | Typing into other apps |
|---|---|---|
| Windows | granted on first capture | none needed |
| macOS | **Microphone** prompt (declared via `NSMicrophoneUsageDescription`) | **Accessibility** must be granted in System Settings → Privacy & Security → Accessibility |
| Linux | none (PulseAudio/PipeWire) | X11/XWayland for XTest; native Wayland → use clipboard-paste mode |

## Packaging

CI (`.github/workflows/build.yml`) produces per-platform artifacts:
`TrackType-Windows-Setup.exe` (WiX bundle), `TrackType-macOS.dmg` (macdeployqt),
`TrackType-Linux.AppImage` and a `tracktype` `.deb`.
