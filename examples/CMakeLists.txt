cmake_minimum_required(VERSION 3.15)
project(OpenAGCExamples LANGUAGES C)

find_package(OpenAGC CONFIG REQUIRED)

set(_OPENAGC_EXAMPLE_SHADER_DIR "${CMAKE_CURRENT_LIST_DIR}/shaders")
if(NOT EXISTS "${_OPENAGC_EXAMPLE_SHADER_DIR}")
    set(_OPENAGC_EXAMPLE_SHADER_DIR
        "${CMAKE_CURRENT_LIST_DIR}/../samples/hw_test/shaders")
endif()
set(OPENAGC_EXAMPLE_SHADER_DIR
    "${_OPENAGC_EXAMPLE_SHADER_DIR}" CACHE PATH
    "Directory containing the compiler-generated tutorial shader headers")

add_executable(openagc_first_compute first_compute.c)
target_link_libraries(openagc_first_compute PRIVATE OpenAGC::openagc)
target_include_directories(openagc_first_compute PRIVATE
    "${OPENAGC_EXAMPLE_SHADER_DIR}")

add_executable(openagc_first_triangle first_triangle.c)
target_link_libraries(openagc_first_triangle PRIVATE OpenAGC::openagc)
target_include_directories(openagc_first_triangle PRIVATE
    "${OPENAGC_EXAMPLE_SHADER_DIR}")
