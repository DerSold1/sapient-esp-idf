# sapient-esp-idf
# - As ESP-IDF component: drop this repo into components/ or use idf_component.yml
# - On a host (Linux/macOS): add_subdirectory() it, builds a static lib "sapient"

file(GLOB_RECURSE SAPIENT_GENERATED_SRCS "${CMAKE_CURRENT_LIST_DIR}/generated/*.c")
set(SAPIENT_SRCS
    "${CMAKE_CURRENT_LIST_DIR}/src/sapient_core.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/sapient_msg.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/sapient_codec.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/sapient_registration.c"
    "${CMAKE_CURRENT_LIST_DIR}/third_party/nanopb/pb_common.c"
    "${CMAKE_CURRENT_LIST_DIR}/third_party/nanopb/pb_encode.c"
    "${CMAKE_CURRENT_LIST_DIR}/third_party/nanopb/pb_decode.c"
    ${SAPIENT_GENERATED_SRCS})
set(SAPIENT_INCLUDE_DIRS include generated third_party/nanopb)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SAPIENT_SRCS}
                           INCLUDE_DIRS ${SAPIENT_INCLUDE_DIRS}
                           PRIV_REQUIRES esp_hw_support)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC PB_FIELD_32BIT=1)
else()
    cmake_minimum_required(VERSION 3.16)
    project(sapient C)
    add_library(sapient STATIC ${SAPIENT_SRCS})
    target_include_directories(sapient PUBLIC ${SAPIENT_INCLUDE_DIRS})
    target_compile_definitions(sapient PUBLIC PB_FIELD_32BIT=1)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        set_source_files_properties(
            "${CMAKE_CURRENT_LIST_DIR}/src/sapient_core.c"
            "${CMAKE_CURRENT_LIST_DIR}/src/sapient_msg.c"
            "${CMAKE_CURRENT_LIST_DIR}/src/sapient_codec.c"
            "${CMAKE_CURRENT_LIST_DIR}/src/sapient_registration.c"
            PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror")
    endif()
endif()
