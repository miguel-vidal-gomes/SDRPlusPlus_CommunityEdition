cmake_minimum_required(VERSION 3.13)
project(rnnoise)

# Define the rnnoise library
add_library(rnnoise_lib STATIC
    rnnoise_stub.c
)

# Set include directories
target_include_directories(rnnoise_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Set compiler flags
if (MSVC)
    target_compile_options(rnnoise_lib PRIVATE /W3)
else()
    target_compile_options(rnnoise_lib PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# Set output name
set_target_properties(rnnoise_lib PROPERTIES
    OUTPUT_NAME rnnoise
    PREFIX "lib"
)
