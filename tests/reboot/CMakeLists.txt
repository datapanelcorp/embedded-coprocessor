cmake_minimum_required(VERSION 3.20.0)

# Use the application's native_sim stand-ins for the ECP hardware
set(DTC_OVERLAY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../../boards/native_sim.overlay)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ecp_reboot_test)

set(APP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(COMMANDS_TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../commands)

# Code under test
target_sources(app PRIVATE ${APP_DIR}/src/commands.c ${APP_DIR}/src/bindesc_dp.c)

# The commands test provides test_main() and the helpers
target_sources(app PRIVATE ${COMMANDS_TEST_DIR}/src/main.c src/main.c)
target_include_directories(app PRIVATE ${COMMANDS_TEST_DIR}/src)

target_sources(app PRIVATE ${ZEPHYR_DPUNITY_MODULE_DIR}/ztest/common/ecp_sim_host/ecp_sim_host.c)
target_include_directories(app PRIVATE ${ZEPHYR_DPUNITY_MODULE_DIR}/ztest/common/ecp_sim_host)
