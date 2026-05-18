cmake_minimum_required(VERSION 3.16.0)
set(SDKCONFIG "${CMAKE_SOURCE_DIR}/sdkconfig.c3")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32-dmx)
