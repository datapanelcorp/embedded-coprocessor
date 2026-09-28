cmake_minimum_required(VERSION 3.20.0)

# find_package(Zephyr REQUIRED)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(app LANGUAGES C)

target_sources(app PRIVATE src/commands.c src/main.c)
target_sources_ifdef(CONFIG_SHELL app PRIVATE src/shell.c)
target_sources_ifdef(CONFIG_BINDESC_DEFINE app PRIVATE src/bindesc_dp.c)
target_sources_ifdef(CONFIG_SOC_SERIES_STM32G0X app PRIVATE src/soc_stm32g0.c)
target_sources_ifdef(CONFIG_TASK_WDT app PRIVATE src/watchdog.c)
