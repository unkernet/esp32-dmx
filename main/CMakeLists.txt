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
add_custom_command(
    OUTPUT "${CMAKE_SOURCE_DIR}/data/index.js.gz" 
    DEPENDS ${UI_SRCS}
    COMMAND ${CMAKE_COMMAND} -E env "PATH=$ENV{SYSTEM_PATH}" npm run build
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/ui"
    COMMENT "Compiling frontend assets with node..."
    VERBATIM
)
add_custom_target(build-ui
    DEPENDS "${CMAKE_SOURCE_DIR}/data/index.js.gz"
)
spiffs_create_partition_image(storage ../data DEPENDS build-ui)
