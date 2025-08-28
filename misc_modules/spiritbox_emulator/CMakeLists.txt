cmake_minimum_required(VERSION 3.13)
project(spiritbox_emulator)

file(GLOB_RECURSE SRC "src/*.cpp")

add_library(spiritbox_emulator SHARED ${SRC})
target_link_libraries(spiritbox_emulator PRIVATE sdrpp_core)
set_target_properties(spiritbox_emulator PROPERTIES PREFIX "")

# Core include paths
target_include_directories(spiritbox_emulator PRIVATE "src/")
target_include_directories(spiritbox_emulator PRIVATE "${SDRPP_CORE_ROOT}")
target_include_directories(spiritbox_emulator PRIVATE "${CMAKE_SOURCE_DIR}")
target_include_directories(spiritbox_emulator PRIVATE "${CMAKE_SOURCE_DIR}/core/src")

if (MSVC)
    target_compile_options(spiritbox_emulator PRIVATE ${SDRPP_MODULE_COMPILER_FLAGS})
else()
    target_compile_options(spiritbox_emulator PRIVATE ${SDRPP_MODULE_COMPILER_FLAGS})
endif()

# Install directives
if (MSVC)
    install(TARGETS spiritbox_emulator DESTINATION "lib/sdrpp/plugins")
else()
    install(TARGETS spiritbox_emulator DESTINATION "lib/sdrpp/plugins")
endif()