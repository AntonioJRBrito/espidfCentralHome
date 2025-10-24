# Gera imagem LittleFS a partir da pasta data/
set(LITTLEFS_IMAGE_PATH "${CMAKE_BINARY_DIR}/littlefs_image.bin")
set(LITTLEFS_DIR "${CMAKE_SOURCE_DIR}/data")
add_custom_target(littlefs_image ALL
  COMMAND python ${IDF_PATH}/components/partition_table/gen_espfs.py
          --input "${LITTLEFS_DIR}"
          --output "${LITTLEFS_IMAGE_PATH}"
          --partition "littlefs"
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Gerando imagem LittleFS a partir de /data"
)