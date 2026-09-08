# Desktop-only unit tests. test_framework.* is the harness; every
# suites/test_<name>.c self-registers its cases at load time (see
# test_framework.h), so a new suite is one new file and nothing here
# changes -- CONFIGURE_DEPENDS re-globs on the next build.
file(GLOB TEST_SUITES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/suites/test_*.c)

add_executable(unit_test
        test_main.c
        test_framework.c
        ${TEST_SUITES}
)
target_include_directories(unit_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(unit_test PRIVATE
        drivers_fake
        sanitizers
        warnings
)
# isatty()/STDOUT_FILENO for the coloured PASS/FAIL tags -- hidden by the
# project-wide -std=c99 without this.
target_compile_definitions(unit_test PRIVATE _POSIX_C_SOURCE=200809L)

# One CTest entry per suite (so `ctest` and the IDE list them separately);
# the suite name is the filename minus its test_ prefix.
foreach(suite_src IN LISTS TEST_SUITES)
    get_filename_component(suite_name "${suite_src}" NAME_WE)
    string(REGEX REPLACE "^test_" "" suite_name "${suite_name}")
    add_test(NAME ${suite_name} COMMAND unit_test ${suite_name})
endforeach()
