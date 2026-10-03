add_library(omsi_content STATIC
    src/library.cpp
    src/profile.cpp
    src/audit.cpp
)

add_library(omsi::content ALIAS omsi_content)

target_include_directories(omsi_content PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(omsi_content PUBLIC omsi::cfg omsi::core omsi::pe)

# The profile reads the volume's free space, which is a Win32 call rather than a standard
# library one. Everything else in this module is plain filesystem work.
target_link_libraries(omsi_content PRIVATE kernel32)