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
