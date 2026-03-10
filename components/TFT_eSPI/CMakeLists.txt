# Only compile TFT_eSPI.cpp — Extensions are #included by it internally
idf_component_register(
    SRCS
        "TFT_eSPI.cpp"
    INCLUDE_DIRS
        "."
        "Extensions"
        "Fonts"
        "Processors"
    REQUIRES arduino-esp32
)

target_compile_options(__idf_TFT_eSPI PRIVATE
    -Wno-error
    -Wno-address
    -Wno-narrowing
    -Wno-deprecated-declarations
)
