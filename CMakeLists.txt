cmake_minimum_required(VERSION 3.15)
project(openagc C)

# openagc — clean-rewrite PS5 AGC library
# sceAgc*/sceAgcDriver* drop-in API compatibility with the Sony PS5 SDK.

set(OPENAGC_PLATFORM "auto" CACHE STRING "openagc platform: auto, prospero, or generic")
set_property(CACHE OPENAGC_PLATFORM PROPERTY STRINGS auto prospero generic)

if(OPENAGC_PLATFORM STREQUAL "prospero")
    set(OPENAGC_PROSPERO_PLATFORM ON)
elseif(OPENAGC_PLATFORM STREQUAL "generic")
    set(OPENAGC_PROSPERO_PLATFORM OFF)
else()
    if(PROSPERO OR PLATFORM_PS5 OR CMAKE_SYSTEM_NAME STREQUAL "Prospero")
        set(OPENAGC_PROSPERO_PLATFORM ON)
    else()
        set(OPENAGC_PROSPERO_PLATFORM OFF)
    endif()
endif()

# Core AGC sources
set(OPENAGC_SOURCES
    src/cb.c
    src/cb_builders.c
    src/context_state.c
    src/register_defaults.c
    src/register_defaults_v0.c
    src/register_defaults_v4.c
    src/register_defaults_v5.c
    src/register_defaults_v7.c
    src/register_defaults_v8.c
    src/register_defaults_v9.c
    src/register_defaults_v10.c
    src/register_defaults_v11.c
    src/register_defaults_versions.c
    src/acb.c
    src/dcb.c
    src/texture.c
    src/shader.c
    src/game_compat.c
    src/driver_ops.c
)

# Platform-specific sources
if(OPENAGC_PROSPERO_PLATFORM)
    list(APPEND OPENAGC_SOURCES
        src/driver_prospero.c
    )
else()
    list(APPEND OPENAGC_SOURCES
        src/driver_generic.c
    )
endif()

add_library(openagc STATIC ${OPENAGC_SOURCES})

target_include_directories(openagc PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(openagc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(openagc PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(OPENAGC_PROSPERO_PLATFORM)
    target_compile_definitions(openagc PUBLIC OPENAGC_PROSPERO OPENAGC_REQUIRE_ABI)
    target_link_libraries(openagc PUBLIC kernel SceAgcDriver)
else()
    target_compile_definitions(openagc PUBLIC OPENAGC_GENERIC)
endif()

# === Tests ===
option(OPENAGC_BUILD_TESTS "Build openagc tests" ON)
if(OPENAGC_BUILD_TESTS)
    enable_testing()

    add_executable(openagc_tests
        tests/test_main.c
        tests/test_types.c
        tests/test_acb.c
        tests/test_cb.c
        tests/test_dcb.c
        tests/test_driver.c
        tests/test_texture.c
        tests/test_shader.c
        tests/test_ioctl.c
        tests/test_register_defaults.c
        tests/test_registers.c
    )
    target_link_libraries(openagc_tests PRIVATE openagc)
    target_include_directories(openagc_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    add_test(NAME openagc_all_tests COMMAND openagc_tests)
endif()

# === Install ===
include(GNUInstallDirs)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.h")
install(TARGETS openagc ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
