add_executable(evigit src/main.cpp)

target_link_libraries(evigit PRIVATE omsi::content omsi::core)

set_target_properties(evigit PROPERTIES
    OUTPUT_NAME "evigit"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")