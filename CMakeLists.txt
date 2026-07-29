cmake_minimum_required(VERSION 3.15)
project(OpenAGC VERSION 0.2.0 LANGUAGES C)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

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
    src/capabilities.c
    src/memory.c
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
    src/graphics.c
    src/game_compat.c
    src/driver_ops.c
    src/driver_registry.c
    src/driver_sony_exports.c
    src/agc_workload_packet.c
)

# Platform-specific sources
if(OPENAGC_PROSPERO_PLATFORM)
    list(APPEND OPENAGC_SOURCES
        src/driver_prospero.c
        src/videoout_prospero.c
    )
else()
    list(APPEND OPENAGC_SOURCES
        src/driver_generic.c
        src/videoout_generic.c
    )
endif()

add_library(openagc STATIC ${OPENAGC_SOURCES})
add_library(OpenAGC::openagc ALIAS openagc)
set_target_properties(openagc PROPERTIES EXPORT_NAME openagc)

target_include_directories(openagc PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_include_directories(openagc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(openagc PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(OPENAGC_PROSPERO_PLATFORM)
    target_compile_definitions(openagc PUBLIC OPENAGC_PROSPERO OPENAGC_REQUIRE_ABI)
    target_link_libraries(openagc PUBLIC kernel SceVideoOut)
else()
    target_compile_definitions(openagc PUBLIC OPENAGC_GENERIC)
endif()

# === Tests ===
option(OPENAGC_BUILD_TESTS "Build openagc tests" ON)
if(OPENAGC_BUILD_TESTS)
    enable_testing()

    add_executable(openagc_tests
        tests/test_main.c
        tests/test_capabilities.c
        tests/test_memory.c
        tests/test_types.c
        tests/test_acb.c
        tests/test_cb.c
        tests/test_dcb.c
        tests/test_driver.c
        tests/test_texture.c
        tests/test_shader.c
        tests/test_graphics.c
        samples/triangle/triangle.c
        tests/test_ioctl.c
        tests/test_register_defaults.c
        tests/test_registers.c
        tests/test_sony_exports.c
        tests/test_driver_registry.c
        tests/test_workload_packet.c
        tests/test_videoout.c
    )
    target_link_libraries(openagc_tests PRIVATE openagc)
    target_include_directories(openagc_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    add_test(NAME openagc_all_tests COMMAND openagc_tests)
endif()

# === Host shader compiler packaging ===
set(OPENAGC_PSBC_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../openagc-psbc"
    CACHE PATH "Path to the openagc-psbc source tree")
set(_OPENAGC_PSBC_DEFAULT "")
if(EXISTS "${OPENAGC_PSBC_SOURCE_DIR}/psbc")
    set(_OPENAGC_PSBC_DEFAULT "${OPENAGC_PSBC_SOURCE_DIR}/psbc")
endif()
set(OPENAGC_PSBC_EXECUTABLE "${_OPENAGC_PSBC_DEFAULT}" CACHE FILEPATH
    "Host openagc-psbc executable to install")
set(_OPENAGC_PACKAGE_PSBC_DEFAULT OFF)
if(OPENAGC_PSBC_EXECUTABLE OR EXISTS "${OPENAGC_PSBC_SOURCE_DIR}/Makefile")
    set(_OPENAGC_PACKAGE_PSBC_DEFAULT ON)
endif()
option(OPENAGC_PACKAGE_PSBC
    "Install the host openagc-psbc compiler with OpenAGC"
    ${_OPENAGC_PACKAGE_PSBC_DEFAULT})
option(OPENAGC_BUILD_PSBC
    "Build openagc-psbc through its Makefile before installation" OFF)

if(OPENAGC_PACKAGE_PSBC)
    if(OPENAGC_BUILD_PSBC)
        find_program(OPENAGC_MAKE_EXECUTABLE NAMES gmake make REQUIRED)
        if(NOT EXISTS "${OPENAGC_PSBC_SOURCE_DIR}/Makefile")
            message(FATAL_ERROR
                "OPENAGC_BUILD_PSBC requires OPENAGC_PSBC_SOURCE_DIR with a Makefile")
        endif()
        set(OPENAGC_PSBC_EXECUTABLE "${OPENAGC_PSBC_SOURCE_DIR}/psbc")
        add_custom_target(openagc_psbc_host ALL
            COMMAND "${OPENAGC_MAKE_EXECUTABLE}" -C
                "${OPENAGC_PSBC_SOURCE_DIR}" psbc
            BYPRODUCTS "${OPENAGC_PSBC_EXECUTABLE}"
            COMMENT "Building host openagc-psbc"
            VERBATIM)
    elseif(NOT EXISTS "${OPENAGC_PSBC_EXECUTABLE}")
        message(FATAL_ERROR
            "OPENAGC_PACKAGE_PSBC requires an existing OPENAGC_PSBC_EXECUTABLE or OPENAGC_BUILD_PSBC=ON")
    endif()
    add_executable(OpenAGC::psbc IMPORTED GLOBAL)
    set_target_properties(OpenAGC::psbc PROPERTIES
        IMPORTED_LOCATION "${OPENAGC_PSBC_EXECUTABLE}")
endif()

# === Install and CMake package ===
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.h")
install(TARGETS openagc EXPORT OpenAGCTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
if(OPENAGC_PACKAGE_PSBC)
    install(PROGRAMS "${OPENAGC_PSBC_EXECUTABLE}"
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        RENAME openagc-psbc)
endif()
install(FILES LICENSE README.md
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/OpenAGC)

set(OPENAGC_CMAKE_INSTALL_DIR
    "${CMAKE_INSTALL_LIBDIR}/cmake/OpenAGC")
configure_package_config_file(
    cmake/OpenAGCConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/OpenAGCConfig.cmake"
    INSTALL_DESTINATION "${OPENAGC_CMAKE_INSTALL_DIR}"
    PATH_VARS CMAKE_INSTALL_BINDIR CMAKE_INSTALL_INCLUDEDIR)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/OpenAGCConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion)
export(EXPORT OpenAGCTargets
    FILE "${CMAKE_CURRENT_BINARY_DIR}/OpenAGCTargets.cmake"
    NAMESPACE OpenAGC::)
install(EXPORT OpenAGCTargets
    FILE OpenAGCTargets.cmake
    NAMESPACE OpenAGC::
    DESTINATION "${OPENAGC_CMAKE_INSTALL_DIR}")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/OpenAGCConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/OpenAGCConfigVersion.cmake"
    DESTINATION "${OPENAGC_CMAKE_INSTALL_DIR}")

set(CPACK_PACKAGE_NAME OpenAGC)
set(CPACK_PACKAGE_VENDOR "OpenAGC")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME
    "openagc-${PROJECT_VERSION}-${OPENAGC_PLATFORM}")
set(CPACK_GENERATOR TGZ)
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
include(CPack)

# Public-API examples are deliberately separate from the large PS5 hardware
# qualification programs under samples/hw_test.
option(OPENAGC_BUILD_EXAMPLES "Build OpenAGC public API examples" OFF)
if(OPENAGC_BUILD_EXAMPLES)
    add_library(openagc_triangle_example STATIC
        samples/triangle/triangle.c)
    target_include_directories(openagc_triangle_example PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/samples/triangle)
    target_link_libraries(openagc_triangle_example PUBLIC openagc)
endif()
