cmake_minimum_required(VERSION 3.16)
project(TrackType VERSION 0.9.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ── Qt version detection ──────────────────────────────────────────────────────
# Prefer Qt6; fall back to Qt5 (5.12+) if Qt6 is not available.
find_package(Qt6 QUIET COMPONENTS Core Gui Widgets)
if(Qt6_FOUND)
    set(QT Qt6)
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg Multimedia Network)
    find_package(Qt6 OPTIONAL_COMPONENTS LinguistTools QUIET)
    set(LINGUIST_FOUND ${Qt6LinguistTools_FOUND})
    message(STATUS "TrackType: building with Qt6 ${Qt6_VERSION}")
else()
    find_package(Qt5 5.12 REQUIRED COMPONENTS Core Gui Widgets Svg Multimedia Network)
    set(QT Qt5)
    find_package(Qt5 OPTIONAL_COMPONENTS LinguistTools QUIET)
    set(LINGUIST_FOUND ${Qt5LinguistTools_FOUND})
    message(STATUS "TrackType: building with Qt5 ${Qt5_VERSION}")
endif()

# ── whisper.cpp (vendored, third_party/whisper) ───────────────────────────────
# Built as a static library, CPU-only — all GPU/accelerator backends and the
# upstream examples/tests are disabled before pulling the project in.  Linking
# the `whisper` target transitively provides ggml and the whisper.h include path.
#
# These are forced CACHE values (not plain variables) so they win regardless of
# the policy version (CMP0077) in effect inside the vendored subproject.
set(BUILD_SHARED_LIBS        OFF CACHE BOOL "" FORCE)   # static whisper + ggml
set(WHISPER_BUILD_TESTS      OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES   OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER     OFF CACHE BOOL "" FORCE)
set(GGML_METAL               OFF CACHE BOOL "" FORCE)   # no Apple GPU
set(GGML_METAL_EMBED_LIBRARY OFF CACHE BOOL "" FORCE)
set(GGML_ACCELERATE          OFF CACHE BOOL "" FORCE)
set(GGML_CUDA                OFF CACHE BOOL "" FORCE)
set(GGML_VULKAN              OFF CACHE BOOL "" FORCE)
set(GGML_OPENCL              OFF CACHE BOOL "" FORCE)
set(GGML_HIP                 OFF CACHE BOOL "" FORCE)
set(GGML_SYCL                OFF CACHE BOOL "" FORCE)
set(GGML_KOMPUTE             OFF CACHE BOOL "" FORCE)
set(GGML_RPC                 OFF CACHE BOOL "" FORCE)
set(GGML_BLAS                OFF CACHE BOOL "" FORCE)
set(GGML_OPENMP              OFF CACHE BOOL "" FORCE)   # no OpenMP runtime dep
set(GGML_NATIVE              OFF CACHE BOOL "" FORCE)   # portable (no -march=native)

# Don't run Qt autogen (moc/uic/rcc) over the vendored C/C++ sources.
set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC OFF)
add_subdirectory(third_party/whisper)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(SOURCES
    main.cpp
    mainwindow.cpp
    textinjector.cpp
    textnormalizer.cpp
    transcriptprocessor.cpp
    globalhotkey.cpp
    transcriptpreview.cpp
    settingsdialog.cpp
    audiocapture.cpp
    levelmeter.cpp
    whisperengine.cpp
    modelmanager.cpp
    modeldownloader.cpp
    translations/tsparser.cpp
    resources.qrc
)

set(HEADERS
    mainwindow.h
    textinjector.h
    textnormalizer.h
    transcriptprocessor.h
    globalhotkey.h
    transcriptpreview.h
    settingsdialog.h
    audiocapture.h
    levelmeter.h
    sttengine.h
    whisperengine.h
    modelmanager.h
    modeldownloader.h
)

if(WIN32)
    if(NOT DEFINED BUILD_NUMBER)
        set(BUILD_NUMBER 0)
    endif()
    configure_file(app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/app.rc" @ONLY)
    add_executable(TrackType WIN32 ${SOURCES} ${HEADERS} "${CMAKE_CURRENT_BINARY_DIR}/app.rc")
elseif(APPLE)
    set(MACOSX_ICON icons/TrackType.icns)
    set_source_files_properties(${MACOSX_ICON} PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)
    add_executable(TrackType MACOSX_BUNDLE ${SOURCES} ${HEADERS} ${MACOSX_ICON})
    set_target_properties(TrackType PROPERTIES
        MACOSX_BUNDLE_ICON_FILE TrackType.icns
        MACOSX_BUNDLE_GUI_IDENTIFIER com.optitrack.tracktype
        MACOSX_BUNDLE_BUNDLE_NAME TrackType
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        # Custom Info.plist: declares NSMicrophoneUsageDescription for dictation.
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/installer/Info.plist.in)
else()
    add_executable(TrackType ${SOURCES} ${HEADERS})
endif()

target_link_libraries(TrackType PRIVATE
    ${QT}::Core
    ${QT}::Gui
    ${QT}::Widgets
    ${QT}::Svg
    ${QT}::Network
    whisper
)

if(DEFINED BUILD_NUMBER)
    target_compile_definitions(TrackType PRIVATE BUILD_NUMBER=${BUILD_NUMBER})
endif()

# Qt Multimedia is a hard dependency: audio support is always compiled in.
target_link_libraries(TrackType PRIVATE ${QT}::Multimedia)
target_compile_definitions(TrackType PRIVATE HAVE_MULTIMEDIA)
message(STATUS "TrackType: audio support enabled (Qt Multimedia required)")

if(WIN32)
    target_link_libraries(TrackType PRIVATE user32)
    target_compile_definitions(TrackType PRIVATE PLATFORM_WINDOWS)
elseif(APPLE)
    find_library(APP_SERVICES ApplicationServices REQUIRED)
    find_library(APPKIT AppKit REQUIRED)
    find_library(CARBON Carbon REQUIRED)   # RegisterEventHotKey (global hotkey)
    target_sources(TrackType PRIVATE macos_utils.mm)
    target_link_libraries(TrackType PRIVATE ${APP_SERVICES} ${APPKIT} ${CARBON})
    target_compile_definitions(TrackType PRIVATE PLATFORM_MACOS)
elseif(UNIX)
    find_package(X11 REQUIRED)
    if(NOT X11_Xtst_FOUND)
        message(FATAL_ERROR "libXtst not found. Install libxtst-dev (Debian/Ubuntu) or libXtst-devel (Fedora).")
    endif()
    target_link_libraries(TrackType PRIVATE X11::X11 X11::Xtst)
    if(X11_Xi_FOUND)
        target_link_libraries(TrackType PRIVATE X11::Xi)
        target_compile_definitions(TrackType PRIVATE HAVE_XI2)
        message(STATUS "TrackType: XInput2 raw motion tracking enabled (Wayland dwell fix)")
    else()
        message(STATUS "TrackType: XInput2 not found — install libxi-dev for accurate dwell tracking on Wayland")
    endif()
    target_compile_definitions(TrackType PRIVATE PLATFORM_LINUX)
endif()

install(TARGETS TrackType DESTINATION bin)

# ── Linux: desktop integration + input-device access rule ─────────────────────
if(UNIX AND NOT APPLE)
    # udev rule granting the active session read access to pointer devices so the
    # dwell timer can track the cursor over every window on Wayland.  Shipped in
    # the .deb (the postinst reloads udev); harmless when bundled in the AppImage.
    install(FILES installer/71-tracktype-input.rules
            DESTINATION lib/udev/rules.d)
    install(FILES installer/TrackType.desktop
            DESTINATION share/applications)
    install(FILES icons/app.svg
            DESTINATION share/icons/hicolor/scalable/apps
            RENAME TrackType.svg)

    # The binary installs as "TrackType" (matching the macOS/Windows target and
    # the desktop launcher's Exec=/StartupWMClass=), but the Debian package and
    # the command users type are lowercase "tracktype".  Ship a relative
    # lowercase symlink so `tracktype` works from a terminal too; the .desktop
    # launcher keeps using the capitalised binary.
    install(CODE "
        execute_process(COMMAND \"${CMAKE_COMMAND}\" -E create_symlink
            TrackType \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin/tracktype\")
    ")
endif()

option(BUILD_TESTS "Build unit tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ── Translations ──────────────────────────────────────────────────────────────
set(TS_FILES
    translations/tracktype_cs.ts
    translations/tracktype_fr.ts
    translations/tracktype_es.ts
    translations/tracktype_zh_CN.ts
    translations/tracktype_ja.ts
    translations/tracktype_ko.ts
)

# .ts files are embedded in resources.qrc and parsed at runtime by TsTranslator
# — no lrelease required for the build.
# If LinguistTools ARE available, also compile .qm files for faster startup.
if(LINGUIST_FOUND)
    if(Qt6_FOUND)
        qt_add_lrelease(TrackType
            TS_FILES ${TS_FILES}
            QM_FILES_OUTPUT_VARIABLE QM_FILES
        )
    else()
        qt5_add_translation(QM_FILES ${TS_FILES})
        add_custom_target(TrackType_translations DEPENDS ${QM_FILES})
        add_dependencies(TrackType TrackType_translations)
    endif()

    add_custom_command(TARGET TrackType POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:TrackType>/translations"
        VERBATIM
    )
    foreach(QM_FILE IN LISTS QM_FILES)
        add_custom_command(TARGET TrackType POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${QM_FILE}" "$<TARGET_FILE_DIR:TrackType>/translations/"
            VERBATIM
        )
    endforeach()
    install(FILES ${QM_FILES} DESTINATION bin/translations)
endif()

# ── Packaging: Debian/Ubuntu .deb ─────────────────────────────────────────────
# The .deb installs to /usr and ships the udev rule, so input-device access is
# set up automatically at install time (the postinst reloads udev) and the
# on-screen keyboard is pulled in via Recommends — no terminal steps needed.
# The AppImage (built separately in CI) remains the portable cross-distro option.
if(UNIX AND NOT APPLE)
    set(CPACK_PACKAGE_NAME "tracktype")
    set(CPACK_PACKAGE_VENDOR "TrackType")
    set(CPACK_PACKAGE_CONTACT "TrackType <noreply@tracktype.app>")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Voice-to-text dictation tool")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    set(CPACK_GENERATOR "DEB")

    # Reload udev after install so the input-access rule applies immediately.
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
        "${CMAKE_CURRENT_SOURCE_DIR}/installer/postinst")

    # Auto-derive linked-library dependencies from the binary; recommend the
    # on-screen keyboard and polkit (pkexec) which are used at runtime but not
    # captured by shared-library scanning.
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "onboard, policykit-1")
    set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")

    include(CPack)
endif()
