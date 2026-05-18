# Build firmware
FILE(GLOB SRCS "src/*.c")
idf_component_register(SRCS ${SRCS}
    INCLUDE_DIRS "include" "src"
    REQUIRES driver esp_timer mdns bt esp_http_server nvs_flash esp_wifi spiffs esp_event lwip lua
)

# Build UI
# Use `idf.py storage-flash` to upload UI manually
file(GLOB_RECURSE UI_SRCS
    "${CMAKE_SOURCE_DIR}/ui/src/*"
)
unset(SYSTEM_PATH CACHE)
set(SYSTEM_PATH "$ENV{SYSTEM_PATH}")
if("${SYSTEM_PATH}" STREQUAL "")
    set(SYSTEM_PATH "$ENV{PATH}")
endif()
add_custom_command(
    COMMENT "Compiling frontend assets with node..."
    DEPENDS ${UI_SRCS}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/ui"
    COMMAND ${CMAKE_COMMAND} -E env "PATH=${SYSTEM_PATH}" npm run build
    OUTPUT "${CMAKE_SOURCE_DIR}/data/index.js.gz" 
    VERBATIM
)
add_custom_target(build-ui
    DEPENDS "${CMAKE_SOURCE_DIR}/data/index.js.gz"
)
spiffs_create_partition_image(storage ../data DEPENDS build-ui)
