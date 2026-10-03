/**
 * @file build_info_types.h
 * @brief BuildInfo -- the identity of a firmware image (see issue #49).
 *
 * One `const BuildInfo` is embedded in every image (production/services/
 * fw_info.c) in its own `.build_info` section, which the MCU linker script
 * places directly after the vector table. It answers "which firmware is on
 * the car?" two ways:
 *
 * - at runtime, through fw_info_get() (boot banner, status CAN frame);
 * - without running anything: tools/build_info.py finds the record in a
 *   `.elf`, `.bin` or `.hex` by its magic word and decodes it.
 *
 * The values are produced at build time by cmake/build_info.cmake from git
 * and the toolchain; nothing here is hand-maintained.
 *
 * Layout rules -- this struct is read by a host script, so it is an on-flash
 * format, not just a C type:
 * - Little-endian, naturally aligned, no implicit padding. Every field's
 *   offset is checked by the desktop unit test (suites/test_fw_info.c).
 * - Strings are fixed-size, always NUL-terminated (the generator truncates),
 *   and restricted to printable ASCII.
 * - Never reorder or resize a field. A change bumps
 *   #BUILD_INFO_LAYOUT_VERSION and tools/build_info.py together.
 *
 * Header-only, standard-library types only -- see ARCHITECTURE.md,
 * Interface Layer rules.
 */
#ifndef MONOBOARD_BUILD_INFO_TYPES_H
#define MONOBOARD_BUILD_INFO_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** First word of the record. Little-endian bytes spell "MBFI". */
#define BUILD_INFO_MAGIC 0x4946424DU

/** Bumped on any layout change; tools/build_info.py checks it. */
#define BUILD_INFO_LAYOUT_VERSION 1U

/** The image was built from a tree with uncommitted changes to tracked files. */
#define BUILD_INFO_FLAG_DIRTY 0x01U
/** git was unavailable (not a checkout, or no git) -- SHA/branch/tag are unknown. */
#define BUILD_INFO_FLAG_NO_VCS 0x02U
/** HEAD is exactly a tag and the tree is clean: a release-candidate build. */
#define BUILD_INFO_FLAG_TAGGED 0x04U

/* Field capacities, including the terminating NUL. */
#define BUILD_INFO_VERSION_LEN    32U
#define BUILD_INFO_GIT_SHA_LEN    48U
#define BUILD_INFO_GIT_BRANCH_LEN 32U
#define BUILD_INFO_GIT_TAG_LEN    32U
#define BUILD_INFO_DATE_LEN       16U
#define BUILD_INFO_BUILD_TYPE_LEN 16U
#define BUILD_INFO_TOOLCHAIN_LEN  32U

typedef struct {
    uint32_t magic;          /* #BUILD_INFO_MAGIC */
    uint16_t layout_version; /* #BUILD_INFO_LAYOUT_VERSION */
    uint16_t size_bytes;     /* sizeof(BuildInfo) */
    uint32_t git_sha32;      /* first 8 hex digits of the commit SHA; 0 if unknown */
    uint32_t flags;          /* BUILD_INFO_FLAG_* */
    /* `git describe`-style: "v1.2.0" on a clean tagged commit,
     * "v1.2.0-3-g1a2b3c4d" after it, "0.0.0-g1a2b3c4d" with no tags at all,
     * "unknown" without git; "-dirty" appended for a dirty tree. */
    char version[BUILD_INFO_VERSION_LEN];
    char git_sha[BUILD_INFO_GIT_SHA_LEN];       /* full 40-hex commit SHA, or "unknown" */
    char git_branch[BUILD_INFO_GIT_BRANCH_LEN]; /* branch, or "detached" / "unknown" */
    char git_tag[BUILD_INFO_GIT_TAG_LEN];       /* nearest reachable tag, or "none" */
    /* "YYYY-MM-DD" (UTC). The ONLY field allowed to differ between two
     * builds of the same commit; honours SOURCE_DATE_EPOCH. */
    char build_date[BUILD_INFO_DATE_LEN];
    char build_type[BUILD_INFO_BUILD_TYPE_LEN]; /* CMAKE_BUILD_TYPE, e.g. "Debug" */
    char toolchain[BUILD_INFO_TOOLCHAIN_LEN];   /* compiler id + version, e.g. "GNU 13.3.1" */
} BuildInfo;

#ifdef __cplusplus
}
#endif

#endif  // MONOBOARD_BUILD_INFO_TYPES_H
