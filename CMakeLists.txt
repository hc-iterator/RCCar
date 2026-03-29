# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(Project C CXX ASM)

set(PICO_CXX_ENABLE_EXCEPTIONS 1)

set(PICO_CXX_ENABLE_RTTI 1)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(Project main.cpp
        RCCar.h
        RemoteControl.cpp
        MotorControl.cpp )

pico_set_program_name(Project "Project")
pico_set_program_version(Project "0.1")


# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(Project 1)
pico_enable_stdio_usb(Project 1)

# 添加并链接NRF24库，以及所有其他标准库
add_subdirectory(lib/pico_nrf24/lib/nrf24l01)

# Add the standard library to the build
target_link_libraries(Project
    PRIVATE
    # 注意：移除了 pico_stdlib、hardware_spi、hardware_gpio
    # 因为它们已经通过 nrf24_driver 传递过来了
    hardware_i2c
    hardware_dma
    hardware_adc
    hardware_pwm
    hardware_pio
    hardware_interp
    hardware_timer
    hardware_watchdog
    hardware_clocks
)

# 接口库必须链接在最后
# 这确保所有依赖正确传递
target_link_libraries(Project
    PRIVATE
    nrf24_driver  # 接口库放在最后
)

# Add the standard include files to the build
target_include_directories(Project PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)
pico_add_extra_outputs(Project)