file (GLOB SOURCES src/*.cpp src/*.c)
idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS src
    REQUIRES arduino
)

list(APPEND DEFINITIONS "ARDUINO_TTGO_T1")

target_compile_definitions(${COMPONENT_TARGET} PUBLIC ${DEFINITIONS})
