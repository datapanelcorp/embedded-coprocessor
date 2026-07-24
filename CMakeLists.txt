cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED)
project(app LANGUAGES C)

target_sources(app PRIVATE src/commands.c src/main.c)
target_sources_ifdef(CONFIG_SHELL app PRIVATE src/shell.c)
target_sources_ifdef(CONFIG_BINDESC_DEFINE app PRIVATE src/bindesc_dp.c)
