# The warning flags arrive transitively through omsi::cfg -> omsi::warnings, so there is
# nothing to include here.

# ---------------------------------------------------------------------------
# Test framework
#
# doctest is a single header, so FetchContent keeps the repository free of a submodule
# and of a vcpkg bootstrap. OMSI_BUILD_TESTS=OFF removes the dependency entirely.
# ---------------------------------------------------------------------------
include(FetchContent)

# doctest still declares cmake_minimum_required(VERSION 3.0), and CMake 4 removed
# compatibility with anything below 3.5. This is the documented way to keep such a
# dependency working; it is set here, before the dependency is added, so it only relaxes
# the policy for the fetched project's own scope.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.12
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(doctest)

add_executable(omsi_tests
    test_main.cpp
    test_number.cpp
    test_text.cpp
    test_reader.cpp
    test_library.cpp
    test_pe.cpp
    test_json.cpp
    test_web.cpp
)

target_link_libraries(omsi_tests PRIVATE
    omsi::cfg
    omsi::content
    omsi::core
    omsi::pe
    doctest::doctest)

if(TARGET omsi::web)
    target_link_libraries(omsi_tests PRIVATE omsi::web)
    target_compile_definitions(omsi_tests PRIVATE OMSI_HAVE_WEB=1)
endif()

# doctest specialises std::tuple, which MSVC reports as C5285. That is a warning in a
# third-party header, not in our code, so the include is SYSTEM: under OMSI_WERROR it
# would otherwise stop the build and we cannot fix it upstream from here.
target_include_directories(omsi_tests SYSTEM PRIVATE "${doctest_SOURCE_DIR}")

add_test(NAME unit COMMAND omsi_tests)
set_tests_properties(unit PROPERTIES TIMEOUT 120)

# doctest prints "Status: SUCCESS!" or "Status: FAILURE!" as its last line. Matching that is
# far safer than guessing at its other wording: a pattern like /assertion/ also matches the
# "assertions: 149 | 149 passed" line of every *successful* run.
set_property(TEST unit PROPERTY PASS_REGULAR_EXPRESSION "Status: SUCCESS!")
set_property(TEST unit PROPERTY FAIL_REGULAR_EXPRESSION "Status: FAILURE!")