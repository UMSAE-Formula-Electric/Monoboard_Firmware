# Generates build_info.h -- the values embedded in the BuildInfo record
# (production/interfaces/build_info_types.h, issue #49).
#
# Run in script mode, both once at configure time (so cppcheck/clang-tidy see
# the header without a build) and on every build via the build_info_gen target:
#
#   cmake -DBI_SOURCE_DIR=<repo> -DBI_OUTPUT=<.../build_info.h>
#         -DBI_BUILD_TYPE=<Debug> -DBI_TOOLCHAIN=<GNU 13.3.1>
#         [-DBI_GIT=<path to git>] -P cmake/build_info.cmake
#
# The header is only rewritten when its content changes, so a rebuild of an
# unchanged tree recompiles nothing. The only time-dependent field is the build
# DATE (not time), so builds of one commit are identical within a day -- and
# always identical with SOURCE_DATE_EPOCH set (CMake's string(TIMESTAMP) honours
# it).
#
# Degrades instead of failing: no git, not a checkout (source tarball), a
# shallow clone or no tags all still produce a valid header, with
# BUILD_INFO_FLAG_NO_VCS / "0.0.0-g<sha>" telling you which.

cmake_minimum_required(VERSION 3.21)

foreach(var BI_SOURCE_DIR BI_OUTPUT)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "build_info.cmake: ${var} is required")
    endif()
endforeach()
if(NOT BI_BUILD_TYPE)
    set(BI_BUILD_TYPE "none")
endif()
if(NOT BI_TOOLCHAIN)
    set(BI_TOOLCHAIN "unknown")
endif()
if(NOT BI_GIT)
    set(BI_GIT git)
endif()

# Runs git in the source tree; OUT_VAR is empty on any failure.
function(bi_git out_var)
    execute_process(
            COMMAND ${BI_GIT} ${ARGN}
            WORKING_DIRECTORY ${BI_SOURCE_DIR}
            RESULT_VARIABLE rc
            OUTPUT_VARIABLE out
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT rc EQUAL 0)
        set(out "")
    endif()
    set(${out_var} "${out}" PARENT_SCOPE)
endfunction()

# Restricts to a C-string-safe charset and truncates to fit a char[LEN]
# (LEN includes the NUL), so no quote or backslash can reach the header.
function(bi_sanitize out_var value len)
    string(REGEX REPLACE "[^A-Za-z0-9._/+ -]" "_" value "${value}")
    math(EXPR max "${len} - 1")
    string(SUBSTRING "${value}" 0 ${max} value)
    set(${out_var} "${value}" PARENT_SCOPE)
endfunction()

set(flags 0)
set(dirty FALSE)
bi_git(git_sha rev-parse --verify HEAD)

# A source tarball unpacked inside some OTHER repository must not report that
# repository's commit: only trust git if this tree is its top level.
bi_git(toplevel rev-parse --show-toplevel)
file(REAL_PATH "${BI_SOURCE_DIR}" src_real)
if(NOT toplevel STREQUAL "")
    file(REAL_PATH "${toplevel}" toplevel)
endif()
if(NOT toplevel STREQUAL src_real)
    set(git_sha "")
endif()

if(git_sha STREQUAL "")
    math(EXPR flags "${flags} | 0x02")   # BUILD_INFO_FLAG_NO_VCS
    set(git_sha "unknown")
    set(git_sha32 "0")
    set(branch "unknown")
    set(tag "none")
    set(version "unknown")
else()
    string(SUBSTRING "${git_sha}" 0 8 sha8)
    set(git_sha32 "0x${sha8}")

    # Dirty = uncommitted changes to TRACKED files (staged or not). Untracked
    # files are ignored: firmware sources are listed explicitly in
    # CMakeLists.txt, so a new file cannot reach the image without a tracked
    # edit. `git status` also refreshes stale index stat info, unlike a bare
    # `git diff-index`.
    bi_git(status status --porcelain --untracked-files=no)
    if(NOT status STREQUAL "")
        set(dirty TRUE)
        math(EXPR flags "${flags} | 0x01")   # BUILD_INFO_FLAG_DIRTY
    endif()

    bi_git(branch rev-parse --abbrev-ref HEAD)
    if(branch STREQUAL "HEAD" OR branch STREQUAL "")
        # Detached HEAD -- the normal state in CI checkouts.
        if(DEFINED ENV{GITHUB_HEAD_REF} AND NOT "$ENV{GITHUB_HEAD_REF}" STREQUAL "")
            set(branch "$ENV{GITHUB_HEAD_REF}")
        elseif(DEFINED ENV{GITHUB_REF_NAME} AND NOT "$ENV{GITHUB_REF_NAME}" STREQUAL "")
            set(branch "$ENV{GITHUB_REF_NAME}")
        else()
            set(branch "detached")
        endif()
    endif()

    bi_git(tag describe --tags --abbrev=0)
    if(tag STREQUAL "")
        set(tag "none")   # shallow clone or no tags yet
    endif()

    bi_git(describe describe --tags --abbrev=8)
    if(describe STREQUAL "")
        set(version "0.0.0-g${sha8}")
    else()
        set(version "${describe}")
        bi_git(exact describe --tags --exact-match)
        if(NOT exact STREQUAL "" AND NOT dirty)
            math(EXPR flags "${flags} | 0x04")   # BUILD_INFO_FLAG_TAGGED
        endif()
    endif()
endif()

# Date only, UTC: changes at most once a day. Honours SOURCE_DATE_EPOCH.
string(TIMESTAMP build_date "%Y-%m-%d" UTC)

# Keep in sync with the BUILD_INFO_*_LEN capacities in build_info_types.h.
# "-dirty" is appended AFTER truncation so a long tag can never cut it off.
if(dirty)
    bi_sanitize(version "${version}" 26)
    string(APPEND version "-dirty")
else()
    bi_sanitize(version "${version}" 32)
endif()
bi_sanitize(git_sha    "${git_sha}"       48)
bi_sanitize(branch     "${branch}"        32)
bi_sanitize(tag        "${tag}"           32)
bi_sanitize(build_date "${build_date}"    16)
bi_sanitize(build_type "${BI_BUILD_TYPE}" 16)
bi_sanitize(toolchain  "${BI_TOOLCHAIN}"  32)
math(EXPR flags_hex "${flags}" OUTPUT_FORMAT HEXADECIMAL)

set(content "/* Generated by cmake/build_info.cmake on every build -- do not edit,
 * do not check in. Consumed only by production/services/fw_info.c. */
#ifndef MONOBOARD_BUILD_INFO_H
#define MONOBOARD_BUILD_INFO_H

#define BUILD_INFO_GIT_SHA32  ${git_sha32}U
#define BUILD_INFO_FLAGS      ${flags_hex}U
#define BUILD_INFO_VERSION    \"${version}\"
#define BUILD_INFO_GIT_SHA    \"${git_sha}\"
#define BUILD_INFO_GIT_BRANCH \"${branch}\"
#define BUILD_INFO_GIT_TAG    \"${tag}\"
#define BUILD_INFO_DATE       \"${build_date}\"
#define BUILD_INFO_BUILD_TYPE \"${build_type}\"
#define BUILD_INFO_TOOLCHAIN  \"${toolchain}\"

#endif /* MONOBOARD_BUILD_INFO_H */
")

# Write-if-different: an unchanged header keeps its mtime, so nothing that
# includes it rebuilds (Ninja restat via BYPRODUCTS; Make via timestamps).
set(old "")
if(EXISTS "${BI_OUTPUT}")
    file(READ "${BI_OUTPUT}" old)
endif()
if(NOT old STREQUAL content)
    file(WRITE "${BI_OUTPUT}" "${content}")
    message(STATUS "build_info: ${version} (${build_type}, ${build_date})")
endif()
