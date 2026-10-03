# The PE reader is read-only and depends on nothing but the standard library, so it stays
# usable on its own: it is what answers "can this run, and how much memory does it get".
add_library(omsi_pe STATIC
    src/pe.cpp
)

add_library(omsi::pe ALIAS omsi_pe)

target_include_directories(omsi_pe PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(omsi_pe PUBLIC omsi::core omsi::warnings)