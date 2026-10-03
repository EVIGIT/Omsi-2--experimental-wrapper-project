# Warning flags shared by every target in the project.
#
# We ask for /W4 on MSVC and -Wall -Wextra elsewhere, but we do *not* promote
# them to errors inside third-party or generated code. OMSI_WERROR applies the
# promotion to our own targets only.

add_library(omsi_warnings INTERFACE)
add_library(omsi::warnings ALIAS omsi_warnings)

# windows.h includes the legacy winsock.h unless it is told to stay lean, and winsock.h and
# winsock2.h cannot both be present. WIN32_LEAN_AND_MEAN keeps windows.h out of the
# socket headers' way; NOMINMAX stops its min/max macros colliding with <algorithm>.
target_compile_definitions(omsi_warnings INTERFACE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

if(MSVC)
    target_compile_options(omsi_warnings INTERFACE
        /W4
        /permissive-      # standards-conforming, no MSVC-specific extensions
        /utf-8            # source and execution charset are UTF-8
        /Zc:__cplusplus
        /Zc:preprocessor
        /MP               # parallel compilation
        /EHsc
        /bigobj
        /wd4324           # padding added before an enum is closed by alignas
        # getenv is deprecated by Microsoft's UCRT but is exactly the right call here: the
        # value is copied into a std::string straight away and never kept as a char*. The
        # suggested _dupenv_s is not portable, and this project builds on clang too.
        /D_CRT_SECURE_NO_WARNINGS
    )
    if(OMSI_WERROR)
        target_compile_options(omsi_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(omsi_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
    )
    if(OMSI_WERROR)
        target_compile_options(omsi_warnings INTERFACE -Werror)
    endif()
endif()