cmake_minimum_required(VERSION 3.16)

# Reuse the Qt version already detected by the parent.
find_package(${QT} REQUIRED COMPONENTS Core Test)

add_executable(tst_placeholder
    tst_placeholder.cpp
)

target_include_directories(tst_placeholder PRIVATE ${CMAKE_SOURCE_DIR})

target_link_libraries(tst_placeholder PRIVATE
    ${QT}::Core
    ${QT}::Test
)

add_test(NAME Placeholder COMMAND tst_placeholder)
