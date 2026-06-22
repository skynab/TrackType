cmake_minimum_required(VERSION 3.16)

# Reuse the Qt version already detected by the parent.
find_package(${QT} REQUIRED COMPONENTS Core Test Multimedia)

add_executable(tst_placeholder
    tst_placeholder.cpp
)

target_include_directories(tst_placeholder PRIVATE ${CMAKE_SOURCE_DIR})

target_link_libraries(tst_placeholder PRIVATE
    ${QT}::Core
    ${QT}::Test
)

add_test(NAME Placeholder COMMAND tst_placeholder)

# AudioCapture: exercises the PcmChunker accumulator and start/stop teardown.
# Compiles audiocapture.cpp directly (no separate library target exists).
add_executable(tst_audiocapture
    tst_audiocapture.cpp
    ${CMAKE_SOURCE_DIR}/audiocapture.cpp
)

target_include_directories(tst_audiocapture PRIVATE ${CMAKE_SOURCE_DIR})

target_link_libraries(tst_audiocapture PRIVATE
    ${QT}::Core
    ${QT}::Test
    ${QT}::Multimedia
)

add_test(NAME AudioCapture COMMAND tst_audiocapture)

# Whisper end-to-end integration test.  Links the vendored whisper static lib and
# compiles the engine directly.  Runs only when TRACKTYPE_TEST_MODEL points at a
# real model (otherwise it skips), so the binary always builds but inference runs
# only where a model is provided (Linux CI).
add_executable(tst_whisperengine
    tst_whisperengine.cpp
    ${CMAKE_SOURCE_DIR}/whisperengine.cpp
)

target_include_directories(tst_whisperengine PRIVATE ${CMAKE_SOURCE_DIR})

target_compile_definitions(tst_whisperengine PRIVATE
    TT_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

target_link_libraries(tst_whisperengine PRIVATE
    ${QT}::Core
    ${QT}::Test
    ${QT}::Multimedia
    whisper
)

add_test(NAME Whisper COMMAND tst_whisperengine)
