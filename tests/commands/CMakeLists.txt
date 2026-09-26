cmake_minimum_required(VERSION 3.20.0)

# Use the application's native_sim stand-ins for the ECP hardware
set(DTC_OVERLAY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../../boards/native_sim.overlay)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ecp_commands_test)

set(APP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# Code under test
target_sources(app PRIVATE ${APP_DIR}/src/commands.c ${APP_DIR}/src/bindesc_dp.c)

target_sources(app PRIVATE
  src/main.c
  src/test_system.c
  src/test_unconfigured.c
  src/test_enum.c
  src/test_io.c
  src/test_estop.c
)
target_sources_ifdef(CONFIG_APP_ECP_GPIO app PRIVATE src/test_gpio.c)
target_sources_ifdef(CONFIG_APP_ECP_PEEK_POKE app PRIVATE src/test_peek_poke.c)
target_sources_ifdef(CONFIG_APP_ECP_LED app PRIVATE src/test_led.c)

target_sources(app PRIVATE ${ZEPHYR_DPUNITY_MODULE_DIR}/ztest/common/ecp_sim_host/ecp_sim_host.c)
target_include_directories(app PRIVATE ${ZEPHYR_DPUNITY_MODULE_DIR}/ztest/common/ecp_sim_host)
