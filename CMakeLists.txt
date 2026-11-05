cmake_minimum_required(VERSION 3.16)
set(MOSAICO_UTILS_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../submodule/esp-mosaico-utils")
set(MOSAICO_BSP_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../submodule/esp-mosaico-bsp")
if(NOT SDKCONFIG)
    set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")
endif()
if(NOT SDKCONFIG_DEFAULTS)
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.application.defaults")
endif()
include("${MOSAICO_UTILS_ROOT}/mosaico-tools/cmake/gsp_compiler.cmake")
list(APPEND EXTRA_COMPONENT_DIRS
    "${MOSAICO_UTILS_ROOT}/esp-mosaico-vibe-mode/components/esp_mosaico_app_recovery"
    "${MOSAICO_UTILS_ROOT}/mosaico-tools/components/esp_mosaico_gsp_bundle"
    "${MOSAICO_UTILS_ROOT}/mosaico-tools/components/esp_mosaico_gsp_iris")
include("${MOSAICO_UTILS_ROOT}/esp-mosaico-vibe-mode/cmake/mosaico_idf_project.cmake")
project(mifi_hotspot VERSION 1.0.0)
include("${MOSAICO_UTILS_ROOT}/mosaico-tools/cmake/system_update.cmake")
# One phone can hold hundreds of TCP flows. The lwIP default keeps each of
# them for 30 minutes in a 512-entry table, so the next station cannot open
# a new flow. These overrides apply to the lwIP component that owns the table.
idf_component_get_property(lwip_lib lwip COMPONENT_LIB)
target_compile_definitions(${lwip_lib} PRIVATE
    IP_NAPT_MAX=2048
    IP_NAPT_TIMEOUT_MS_TCP=180000
    ARP_TABLE_SIZE=32)
