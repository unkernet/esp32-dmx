set(_d "${CMAKE_CURRENT_LIST_DIR}")

set(SRCS)
if(CONFIG_SOC_UART_SUPPORTED)
    list(APPEND SRCS "${_d}/src/uart.c" "${_d}/src/uart_wakeup.c")
    if(CONFIG_SOC_UHCI_SUPPORTED)
        list(APPEND SRCS "${_d}/src/uhci.c")
    endif()
endif()

set(INCLUDE_DIRS "${_d}/include")
set(PRIV_INCLUDE_DIRS "${_d}/src")

set(REQUIRES esp_hal_uart)

idf_build_get_property(target IDF_TARGET)
if(${target} STREQUAL "linux")
    set(PRIV_REQUIRES esp_ringbuf)
else()
    set(PRIV_REQUIRES esp_pm esp_driver_gpio esp_driver_dma esp_ringbuf esp_mm esp_psram)
endif()

set(LDFRAGMENTS "${_d}/linker.lf")

function(uart_apply_custom_settings)
    if(CONFIG_VFS_SUPPORT_IO AND CONFIG_SOC_UART_SUPPORTED)
        target_link_libraries(${COMPONENT_LIB} PUBLIC idf::vfs)
        target_sources(${COMPONENT_LIB} PRIVATE "${_d}/src/uart_vfs.c")
        if(CONFIG_ESP_CONSOLE_UART)
            target_link_libraries(${COMPONENT_LIB} INTERFACE "-u uart_vfs_include_dev_init")
        endif()
    endif()
endfunction()
