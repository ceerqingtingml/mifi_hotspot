cmake_minimum_required(VERSION 3.20)
project(mifi_backend LANGUAGES C)

if(NOT ESP_GSP_COMPONENT_DIR AND DEFINED ENV{ESP_GSP_COMPONENT_DIR})
    set(ESP_GSP_COMPONENT_DIR "$ENV{ESP_GSP_COMPONENT_DIR}")
endif()
if(NOT ESP_GSP_COMPONENT_DIR)
    foreach(candidate IN ITEMS
            "${CMAKE_CURRENT_LIST_DIR}/../../../managed_components/espressif__esp-gsp")
        get_filename_component(candidate "${candidate}" ABSOLUTE)
        if(EXISTS "${candidate}/tools/sim_bridge/CMakeLists.txt")
            set(ESP_GSP_COMPONENT_DIR "${candidate}")
            break()
        endif()
    endforeach()
endif()
if(NOT EXISTS "${ESP_GSP_COMPONENT_DIR}/tools/sim_bridge/CMakeLists.txt")
    message(FATAL_ERROR
        "Install espressif/esp-gsp first, or set ESP_GSP_COMPONENT_DIR")
endif()

add_subdirectory("${ESP_GSP_COMPONENT_DIR}/tools/sim_bridge" sim_bridge)
gsp_add_backend(mifi_backend
    SOURCES ../app_mifi.c platform_pc.c
    INCLUDE_DIRECTORIES ../include
    SCENES ../ui/main.json)

# The host profile has no PSRAM, so the stock pack disables the image cache
# and leaves runtime icon slots at zero. Device rows need both.
set(_mifi_scene "${CMAKE_CURRENT_LIST_DIR}/../ui/main.json")
set(_mifi_generated "${CMAKE_CURRENT_BINARY_DIR}/mifi_backend_assets")
set(_mifi_profile
    "${ESP_GSP_COMPONENT_DIR}/tools/sim_bridge/profiles/rgb565.yaml")
execute_process(
    COMMAND python3
        "${CMAKE_CURRENT_LIST_DIR}/../../../../../submodule/esp-mosaico-utils/mosaico-tools/tools/gsp-sim/fetch_gspc.py"
    OUTPUT_VARIABLE _mifi_gspc
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _mifi_gspc_result)
if(NOT _mifi_gspc_result EQUAL 0 OR NOT EXISTS "${_mifi_gspc}")
    message(FATAL_ERROR "Could not resolve gspc for the MiFi simulator bundle")
endif()
add_custom_command(TARGET mifi_backend_bundle POST_BUILD
    COMMAND "${_mifi_gspc}" pack "${_mifi_scene}" --deployable
        --profile "${_mifi_profile}"
        --gen-dir "${_mifi_generated}"
        --api-header "${_mifi_generated}/bundle_gsp.h"
        --symbol bridge_bundle
        --image-cache-bytes 1048576
        --project-dynamic-image-slots 8
        -o "${_mifi_generated}/app.gspb"
    VERBATIM)
