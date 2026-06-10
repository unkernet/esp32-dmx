if(${IDF_VERSION_MAJOR} EQUAL 5)
    set(ver_dir "v5")
elseif(${IDF_VERSION_MAJOR} EQUAL 6)
    set(ver_dir "v6")
else()
    message(FATAL_ERROR
        "esp_driver_uart fork supports IDF 5.x and 6.x only "
        "(detected ${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR})")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/${ver_dir}/component.cmake")

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS ${INCLUDE_DIRS}
    PRIV_INCLUDE_DIRS ${PRIV_INCLUDE_DIRS}
    REQUIRES "${REQUIRES}"
    PRIV_REQUIRES "${PRIV_REQUIRES}"
    LDFRAGMENTS "${LDFRAGMENTS}"
)

if(COMMAND uart_apply_custom_settings)
    uart_apply_custom_settings()
endif()
