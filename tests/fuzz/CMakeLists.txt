cmake_minimum_required(VERSION 3.20.0)

# Use the application's native_sim stand-ins for the ECP hardware
set(DTC_OVERLAY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../../boards/native_sim.overlay)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ecp_fuzz)

set(APP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# Code under test
target_sources(app PRIVATE ${APP_DIR}/src/commands.c ${APP_DIR}/src/bindesc_dp.c)

target_sources(app PRIVATE src/main.c)
