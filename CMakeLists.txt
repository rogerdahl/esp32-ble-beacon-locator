cmake_minimum_required(VERSION 3.5)
add_compile_options(-fdiagnostics-color=always)

#set(CMAKE_CXX_STANDARD 17)
#set(CMAKE_C_STANDARD 99)

set(IDF_PATH $ENV{HOME}/sdk/esp-idf)
set(IDF_TARGET "esp32")

set(
    EXTRA_COMPONENT_DIRS
#    $ENV{HOME}/sdk/esp-idf/components
    $ENV{HOME}/sdk/esp-idf-lib/components
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(ble-beacon-finder)

# The path to the Python venv is printed when activating ESP-IDF.
#set(ENV{IDF_PYTHON_ENV_PATH} $ENV{HOME}/.espressif/python_env/idf4.3_py3.9_env/bin/python)
# /home/dahl/sdk/esp-idf/venv/bin/python
#set(
#  CMAKE_CXX_FLAGS
#  "${CMAKE_CXX_FLAGS} -Wno-unused-function"
#)

#CFLAGS += -std=c99 -pedantic -pedantic-errors -Werror -g0 -Os \
#-fno-strict-overflow -fno-strict-aliasing \
#-pthread \
#-Wno-unused-label \
#-Wno-unused-function \
#$(INC)
#SET(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} ${GCC_COVERAGE_LINK_FLAGS}")
#
