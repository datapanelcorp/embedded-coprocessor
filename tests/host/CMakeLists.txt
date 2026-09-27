cmake_minimum_required(VERSION 3.20.0)

# The application's native_sim stand-ins for the ECP hardware, plus the UARTs that
# connect the host to the ECP
set(DTC_OVERLAY_FILE
  "${CMAKE_CURRENT_SOURCE_DIR}/../../boards/native_sim.overlay;${CMAKE_CURRENT_SOURCE_DIR}/host.overlay"
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ecp_host_test)

set(APP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# Code under test
target_sources(app PRIVATE ${APP_DIR}/src/commands.c ${APP_DIR}/src/bindesc_dp.c)

target_sources(app PRIVATE src/main.c)

# With the sanitizers, GCC 14 reports false "may be used uninitialized" warnings in
# Zephyr 3.7's UART emulator, which twister treats as errors
if(CONFIG_ASAN OR CONFIG_UBSAN)
  set_source_files_properties(${ZEPHYR_BASE}/drivers/serial/uart_emul.c
    TARGET_DIRECTORY drivers__serial
    PROPERTIES COMPILE_OPTIONS -Wno-maybe-uninitialized
  )
endif()
