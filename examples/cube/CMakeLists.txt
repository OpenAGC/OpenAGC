cmake_minimum_required(VERSION 3.20)

project(openagc_cube LANGUAGES C)

find_package(OpenAGC CONFIG REQUIRED)

add_executable(openagc_cube
    cube.c
    ps5_platform.c)
set_target_properties(openagc_cube PROPERTIES SUFFIX ".elf")
target_compile_features(openagc_cube PRIVATE c_std_99)
target_compile_options(openagc_cube PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(openagc_cube PRIVATE
    OpenAGC::openagc
    SceVideoOut
    kernel)
