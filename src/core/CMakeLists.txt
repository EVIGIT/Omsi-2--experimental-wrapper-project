add_library(omsi_core STATIC
    src/log.cpp
)

add_library(omsi::core ALIAS omsi_core)

target_include_directories(omsi_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(omsi_core PUBLIC omsi::warnings)