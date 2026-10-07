cmake_minimum_required(VERSION 3.16)
set(PROJECT_VER "1.1.1")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(gpt_voice_terminal)

# Use the device stack directly: SD ownership and lifecycle are managed here,
# rather than an MSC helper that can automatically mount/format the card.
idf_component_get_property(tinyusb_lib espressif__tinyusb COMPONENT_LIB)
target_include_directories(${tinyusb_lib} PUBLIC "${CMAKE_CURRENT_LIST_DIR}/src/usb_config")

# UI objects do not need DMA memory. Keep them out of the internal heap used
# by Wi-Fi and the AES DMA buffers required for realtime TLS traffic.
idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
target_include_directories(${lvgl_lib} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/src/core")
target_compile_definitions(${lvgl_lib} PRIVATE
    LV_MEM_CUSTOM_INCLUDE="lvgl_memory.h"
    LV_MEM_CUSTOM_ALLOC=terminal_lv_malloc
    LV_MEM_CUSTOM_FREE=heap_caps_free
    LV_MEM_CUSTOM_REALLOC=terminal_lv_realloc)
