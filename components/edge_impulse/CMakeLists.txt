cmake_minimum_required(VERSION 3.13.1)

set(MODEL_FOLDER ${CMAKE_CURRENT_LIST_DIR})
set(EI_SDK_FOLDER ${CMAKE_CURRENT_LIST_DIR}/edge-impulse-sdk)

if(CONFIG_EDGE_IMPULSE_ENABLE)
    if(NOT CMAKE_BUILD_EARLY_EXPANSION)
        add_definitions(-DEI_CLASSIFIER_TFLITE_ENABLE_ESP_NN=1)
        add_definitions(-DEIDSP_USE_ESP_DSP=1)
        if(${IDF_TARGET} STREQUAL "esp32s3")
            add_definitions(-DEI_CLASSIFIER_TFLITE_ENABLE_ESP_NN_S3=1)
        endif()
    endif()

    set(include_dirs
        ${MODEL_FOLDER}
        ${MODEL_FOLDER}/tflite-model
        ${MODEL_FOLDER}/model-parameters
        ${EI_SDK_FOLDER}
        ${EI_SDK_FOLDER}/porting/espressif/esp-dsp/modules/fft/include
        ${EI_SDK_FOLDER}/porting/espressif/esp-dsp/modules/common/include
        $ENV{IDF_PATH}/components/esp_driver_gpio/include
        $ENV{IDF_PATH}/components/esp_driver_i2s/include
        $ENV{IDF_PATH}/components/esp_wifi/include
        $ENV{IDF_PATH}/components/esp_event/include
        $ENV{IDF_PATH}/components/esp_http_client/include
        $ENV{IDF_PATH}/components/esp_netif/include
        $ENV{IDF_PATH}/components/nvs_flash/include
        $ENV{IDF_PATH}/components/esp_partition/include
        $ENV{IDF_PATH}/components/esp_timer/include
        $ENV{IDF_PATH}/components/json/cJSON
        $ENV{IDF_PATH}/components/mbedtls/mbedtls/include
        ${CMAKE_SOURCE_DIR}/managed_components/espressif__esp_websocket_client/include
        $ENV{IDF_PATH}/components/tcp_transport/include
    )

    include(${EI_SDK_FOLDER}/cmake/utils.cmake)

    RECURSIVE_FIND_FILE_EXCLUDE_DIR(SOURCE_FILES "${EI_SDK_FOLDER}" "CMSIS" "*.cpp")
    RECURSIVE_FIND_FILE_EXCLUDE_DIR(MODEL_FILES "${MODEL_FOLDER}/tflite-model" "CMSIS" "*.cpp")
    RECURSIVE_FIND_FILE_EXCLUDE_DIR(CC_FILES "${EI_SDK_FOLDER}" "CMSIS" "*.cc")
    RECURSIVE_FIND_FILE_EXCLUDE_DIR(C_FILES "${EI_SDK_FOLDER}" "CMSIS" "*.c")

    RECURSIVE_FIND_FILE_EXCLUDE_DIR(S_FILES_ESP_NN "${EI_SDK_FOLDER}/porting/espressif/ESP-NN" "CMSIS" "*.s")
    RECURSIVE_FIND_FILE_EXCLUDE_DIR(S_FILES_ESP_DSP "${EI_SDK_FOLDER}/porting/espressif/esp-dsp/modules/fft" "CMSIS" "*.s")

    list(APPEND SOURCE_FILES ${C_FILES})
    list(APPEND SOURCE_FILES ${CC_FILES})
    list(APPEND SOURCE_FILES ${MODEL_FILES})
    list(APPEND SOURCE_FILES ${S_FILES_ESP_NN})
    list(APPEND SOURCE_FILES ${S_FILES_ESP_DSP})

    idf_component_register(SRCS "edge_impulse_impl.cpp" "${SOURCE_FILES}"
                           INCLUDE_DIRS "${include_dirs}"
                           REQUIRES esp_http_client esp_websocket_client esp_wifi esp_event nvs_flash esp_partition esp_netif esp_driver_gpio esp_driver_i2s driver esp_timer esp_system esp_common ui_state)

    idf_component_get_property(JSON_LIB json COMPONENT_LIB)
    idf_component_get_property(DSP_LIB espressif__esp-dsp COMPONENT_LIB)
    if(NOT JSON_LIB)
        message(FATAL_ERROR "json component not found")
    endif()
    if(NOT DSP_LIB)
        message(FATAL_ERROR "espressif__esp-dsp component not found")
    endif()
    target_link_libraries(${COMPONENT_LIB} PRIVATE ${JSON_LIB} ${DSP_LIB})

    target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++17)
else()
    idf_component_register(SRCS "edge_impulse.cpp"
                           INCLUDE_DIRS "."
                           REQUIRES ui_state)
endif()
