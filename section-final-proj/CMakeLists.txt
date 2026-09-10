cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME section-final-proj)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
# Languages pinned to C/ASM: this project has no C++ sources, and the bare
# project() call defaults to C+CXX, which fails unless arm-none-eabi-g++ is
# installed (Fedora ships it separately in arm-none-eabi-gcc-cs-c++).
# Add CXX back here if you introduce .cpp sources.
project(${CMAKE_PROJECT_NAME} C ASM)
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
    Core/Src/DAQ/daq.c
    Core/Src/DAQ/ringbuffer.c
    Core/Src/DAQ/uart.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    Core/Src/DAQ
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)

#######################################
# Flash / debug helpers (OpenOCD + gdb)
#
# Note: the G4 family is driven by OpenOCD's stm32l4x flash driver -- see
# /usr/share/openocd/scripts/target/stm32g4x.cfg -- hence the erase command.
#######################################
set(OPENOCD_CFG "board/st_nucleo_g4.cfg" CACHE STRING "OpenOCD board config")

add_custom_target(flash
    COMMAND openocd -f ${OPENOCD_CFG}
            -c "program $<TARGET_FILE:${CMAKE_PROJECT_NAME}> verify reset exit"
    DEPENDS ${CMAKE_PROJECT_NAME}
    USES_TERMINAL
    COMMENT "Flashing ${CMAKE_PROJECT_NAME}.elf via OpenOCD")

add_custom_target(openocd
    COMMAND openocd -f ${OPENOCD_CFG}
    USES_TERMINAL
    COMMENT "OpenOCD gdbserver on :3333 (Ctrl-C to stop)")

add_custom_target(erase
    COMMAND openocd -f ${OPENOCD_CFG}
            -c "init\; reset halt\; stm32l4x mass_erase 0\; exit"
    USES_TERMINAL
    COMMENT "Mass-erasing the STM32G474")
