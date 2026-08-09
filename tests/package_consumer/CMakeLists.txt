cmake_minimum_required(VERSION 3.15)
project(OpenAGCPackageConsumer C)

find_package(OpenAGC 0.1 CONFIG REQUIRED)

if(NOT TARGET OpenAGC::openagc)
    message(FATAL_ERROR "OpenAGC::openagc is missing")
endif()
if(NOT TARGET OpenAGC::psbc)
    message(FATAL_ERROR "OpenAGC::psbc is missing")
endif()
if(NOT EXISTS "${OpenAGC_PSBC_EXECUTABLE}")
    message(FATAL_ERROR "Installed openagc-psbc is missing")
endif()

add_executable(openagc_package_consumer main.c)
target_link_libraries(openagc_package_consumer PRIVATE OpenAGC::openagc)

set(OPENAGC_PACKAGE_TEST_SPIRV "" CACHE FILEPATH
    "Optional SPIR-V fixture used to exercise the installed shader compiler")
if(OPENAGC_PACKAGE_TEST_SPIRV)
    openagc_compile_shader(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/package_test.sb"
        SOURCE "${OPENAGC_PACKAGE_TEST_SPIRV}"
        STAGE compute
        RESULT OPENAGC_PACKAGE_TEST_SHADER)
    add_custom_target(openagc_package_shader ALL
        DEPENDS "${OPENAGC_PACKAGE_TEST_SHADER}")
endif()
