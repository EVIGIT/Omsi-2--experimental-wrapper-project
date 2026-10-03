add_library(omsi_cfg STATIC
    src/codepage.cpp
    src/number.cpp
    src/reader.cpp
)

add_library(omsi::cfg ALIAS omsi_cfg)

target_include_directories(omsi_cfg PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(omsi_cfg PUBLIC omsi::warnings)

# tables.hpp is included by src/*.cpp only; keep it out of the installed interface.
target_include_directories(omsi_cfg PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")