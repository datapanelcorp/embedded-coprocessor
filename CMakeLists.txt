cmake_minimum_required(VERSION 3.20.0)
list(APPEND SYSCALL_INCLUDE_DIRS "${CMAKE_CURRENT_SRC_DIR}/../../include/zephyr/")
set(CACHE SYSCALL_INCLUDE_DIRS TYPE INTERNAL VALUE "${SYSCALL_INCLUDE_DIRS}")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(app LANGUAGES C)

target_sources(app PRIVATE src/main.c src/shell.c)
