# T-17 soak firmware for the M5StickS3 (ESP32-S3). Build: idf.py build
cmake_minimum_required(VERSION 3.21)
list(APPEND EXTRA_COMPONENT_DIRS "..")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ea_soak)
