# What CMake to start at
cmake_minimum_required(VERSION 3.12)

# Include build functions from Pico SDK
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

# Build as release but with debug symbols
set(CMAKE_BUILD_TYPE RelWithDebInfo)

# Set the name and version of the project
project(playground C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initalise the SDK
pico_sdk_init()

# Link the Project to a source file (step 4.6)
add_executable(playground
    source.c
)

# Add the standard library to the build
# target_link_libraries(playground pico_stdlib)
target_compile_options(playground PUBLIC -Wall -Wextra -Wno-unused-function -Wno-unused-parameter)

pico_set_program_name(playground "playground")
pico_set_program_version(playground "1.0")

# Link the Project to an extra library (pico_stdlib)
target_link_libraries(playground
    pico_stdlib
    hardware_pio
    hardware_pwm
)

# Build pio
pico_generate_pio_header(playground ${CMAKE_CURRENT_LIST_DIR}/nec_receive_library/nec_receive.pio)

# Enable USB, and DISABLE UART output
pico_enable_stdio_usb(playground 1)
pico_enable_stdio_uart(playground 0)

# create map/bin/hex/uf2 file etc.
# Enable extra outputs (SWD?)
pico_add_extra_outputs(playground)
