cmake_minimum_required(VERSION 3.13)
project(scanner)

# Option to enable verbose scanner debug logging (OFF by default)
# Set to ON only for development/debugging
option(SCANNER_DEBUG_LOGS "Enable verbose scanner debug logging" OFF)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

# We reuse the FFTW library that the core already links against
target_include_directories(scanner PRIVATE "src/" "../../decoder_modules/radio/src" "../../" "../../core/src")
target_link_libraries(scanner PRIVATE fftw3f)

# Add debug logs define if enabled
if(SCANNER_DEBUG_LOGS)
    target_compile_definitions(scanner PRIVATE SCANNER_DEBUG_LOGS)
endif()