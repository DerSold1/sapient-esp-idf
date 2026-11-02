cmake_minimum_required(VERSION 3.16)

# The sapient component is the repository root (two levels up),
# Wi-Fi helper comes from ESP-IDF's example components.
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../.."
    "$ENV{IDF_PATH}/examples/common_components/protocol_examples_common")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sapient_sensor_node)
