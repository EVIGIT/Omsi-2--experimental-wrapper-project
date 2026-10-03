cmake_minimum_required(VERSION 3.28)

project(OmsiEvigit
    VERSION 0.1.0
    DESCRIPTION "A launcher and companion tool for OMSI 2 (Windows / Vulkan)"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# Keep the project's own headers out of any system include path.
set(OMSI_IS_TOP_LEVEL OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(OMSI_IS_TOP_LEVEL ON)
endif()

option(OMSI_BUILD_TESTS    "Build the test suite"            ${OMSI_IS_TOP_LEVEL})
option(OMSI_BUILD_GUI      "Build the ImGui front end"        OFF)
option(OMSI_BUILD_RENDER   "Build the Vulkan renderer"       OFF)
option(OMSI_WERROR         "Treat warnings as errors"        OFF)

include(cmake/warnings.cmake)

# ---------------------------------------------------------------------------
# Modules
# ---------------------------------------------------------------------------
add_subdirectory(src/core)
add_subdirectory(src/cfg)
add_subdirectory(src/pe)
add_subdirectory(src/content)
add_subdirectory(src/app)

# ---------------------------------------------------------------------------
# Vulkan renderer (optional; needs the Vulkan SDK)
#
# -DOMSI_BUILD_RENDER=ON adds it. The rest of the project builds and tests without the
# SDK, which keeps the parsers usable on a machine that has no graphics toolchain.
# ---------------------------------------------------------------------------
if(OMSI_BUILD_RENDER)
    # The SDK exports its own FindVulkan, and VULKAN_SDK is set by its installer. Asking
    # for the SDK explicitly is clearer than relying on a machine-wide environment.
    find_package(Vulkan REQUIRED)

    # find_package(Vulkan) reports which optional tools it found; glslangValidator is one
    # of them and is what the build compiles shaders with.
    set(OMSI_GLSLANG "${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}")

    # volk loads vulkan-1.dll at run time and gives the same entry points the loader
    # exports. Linking the import library directly costs a loader DLL in the output.
    # The tag tracks the SDK release it was cut against, so it is pinned to the version
    # this project was developed against; bump both together.
    include(FetchContent)
    FetchContent_Declare(
        volk
        GIT_REPOSITORY https://github.com/zeux/volk.git
        GIT_TAG vulkan-sdk-1.4.363.0
        GIT_SHALLOW TRUE
    )
    set(VOLK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(VOLK_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(volk)

    add_library(omsi_render STATIC
        src/render/src/device.cpp
    )
    add_library(omsi::render ALIAS omsi_render)

    target_include_directories(omsi_render PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/render/include")

    target_link_libraries(omsi_render PUBLIC omsi::cfg omsi::core omsi::warnings volk::volk)

    target_compile_definitions(omsi_render PUBLIC
        VK_NO_PROTOTYPES          # every entry point comes from volk
        VK_USE_PLATFORM_WIN32_KHR)

    # Shaders are compiled to SPIR-V at build time, so a broken shader fails the build
    # rather than the first frame. This is the same guarantee openOMSI gets from
    # validating its WGSL in CI.
    # find_package(Vulkan) already located glslangValidator (it reports the component when
    # the SDK is installed); fall back to a search for a plain SDK install.
    if(NOT OMSI_GLSLANG)
        find_program(OMSI_GLSLANG
            NAMES glslangValidator
            HINTS "${Vulkan_INCLUDE_DIR}/../Bin" ENV VULKAN_SDK
            DOC "glslangValidator from the Vulkan SDK"
        )
    endif()
    if(NOT OMSI_GLSLANG)
        find_program(OMSI_GLSLANG
            NAMES glslangValidator
            HINTS ENV VULKAN_SDK
            PATH_SUFFIXES Bin
            DOC "glslangValidator from the Vulkan SDK"
        )
    endif()
    if(OMSI_GLSLANG)
        message(STATUS "  shaders ........... ${OMSI_GLSLANG}")
        file(GLOB_RECURSE OMSI_SHADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/*.vert"
                                                             "${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/*.frag")
        foreach(shader IN LISTS OMSI_SHADERS)
            get_filename_component(stage "${shader}" NAME)
            set(output "${CMAKE_CURRENT_BINARY_DIR}/shaders/${stage}.spv")
            add_custom_command(
                OUTPUT "${output}"
                COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
                COMMAND "${OMSI_GLSLANG}" -V --target-env vulkan1.3 -o "${output}" "${shader}"
                DEPENDS "${shader}"
                COMMENT "Compiling ${stage}"
                VERBATIM
            )
            list(APPEND OMSI_SHADER_OUTPUTS "${output}")
        endforeach()
        add_custom_target(omsi_shaders DEPENDS ${OMSI_SHADER_OUTPUTS})
        add_dependencies(omsi_render omsi_shaders)
        target_compile_definitions(omsi_render PUBLIC OMSI_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/shaders")
    else()
        message(WARNING "glslangValidator not found; shaders will not be compiled at build time")
    endif()

    add_executable(evigit-vk src/render/src/main.cpp)
    target_link_libraries(evigit-vk PRIVATE omsi::render)
endif()

# ---------------------------------------------------------------------------
# Web front end
#
# The launcher's window is HTML, CSS and JavaScript served by a small loopback HTTP
# server. It needs no graphics dependency, so it is built by default.
# ---------------------------------------------------------------------------
option(OMSI_BUILD_WEB "Build the web front end (evigit-web)" ${OMSI_IS_TOP_LEVEL})

if(OMSI_BUILD_WEB)
    add_library(omsi_web STATIC
        src/web/src/api.cpp
        src/web/src/json.cpp
        src/web/src/server.cpp
    )
    add_library(omsi::web ALIAS omsi_web)

    target_include_directories(omsi_web PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/web/include")

    target_link_libraries(omsi_web PUBLIC omsi::content omsi::core omsi::warnings)

    # The socket layer. The project is Windows-only, so the platform SDK is not optional.
    target_link_libraries(omsi_web PUBLIC ws2_32)

    add_executable(evigit-web src/web/src/main.cpp)
    target_link_libraries(evigit-web PRIVATE omsi::web)

    # The front end is copied next to the binary so it runs straight out of the build
    # tree; there is no install step to forget.
    add_custom_command(
        TARGET evigit-web POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/web"
                "$<TARGET_FILE_DIR:evigit-web>/web"
        COMMENT "Copying the front end next to evigit-web.exe"
        VERBATIM
    )
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(OMSI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

message(STATUS "")
message(STATUS "  Omsi-EVIGIT ${PROJECT_VERSION}")
message(STATUS "  build type ....... ${CMAKE_BUILD_TYPE}")
message(STATUS "  compiler ......... ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  tests ............ ${OMSI_BUILD_TESTS}")
message(STATUS "  gui .............. ${OMSI_BUILD_GUI}")
message(STATUS "  vulkan renderer .. ${OMSI_BUILD_RENDER}")
message(STATUS "")