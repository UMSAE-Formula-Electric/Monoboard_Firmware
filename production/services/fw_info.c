/**
 * @file fw_info.c
 * @brief The embedded BuildInfo record and its accessors (issue #49).
 *
 * build_info.h is generated into the build tree by cmake/build_info.cmake on
 * every build and is included by this file only, so a new commit recompiles
 * exactly one translation unit.
 */
#include "fw_info.h"

#include <string.h>

#include "build_info.h"

/* Placed in its own section so the linker script can pin it directly after
 * the vector table (and KEEP it through --gc-sections), and so
 * tools/build_info.py can find it by section name in an .elf. `used` stops
 * the compiler discarding it while nothing references it. */
__attribute__((section(".build_info"), used)) static const BuildInfo build_info = {
    .magic          = BUILD_INFO_MAGIC,
    .layout_version = (uint16_t)BUILD_INFO_LAYOUT_VERSION,
    .size_bytes     = (uint16_t)sizeof(BuildInfo),
    .git_sha32      = BUILD_INFO_GIT_SHA32,
    .flags          = BUILD_INFO_FLAGS,
    .version        = BUILD_INFO_VERSION,
    .git_sha        = BUILD_INFO_GIT_SHA,
    .git_branch     = BUILD_INFO_GIT_BRANCH,
    .git_tag        = BUILD_INFO_GIT_TAG,
    .build_date     = BUILD_INFO_DATE,
    .build_type     = BUILD_INFO_BUILD_TYPE,
    .toolchain      = BUILD_INFO_TOOLCHAIN,
};

const BuildInfo *fw_info_get(void)
{
    return &build_info;
}

static bool is_terminated(const char *str, size_t capacity)
{
    return memchr(str, '\0', capacity) != NULL;
}

bool fw_info_is_valid(const BuildInfo *info)
{
    if (info == NULL) {
        return false;
    }
    if ((info->magic != BUILD_INFO_MAGIC) || (info->layout_version != BUILD_INFO_LAYOUT_VERSION)
        || (info->size_bytes != sizeof(BuildInfo))) {
        return false;
    }
    return is_terminated(info->version, sizeof(info->version))
           && is_terminated(info->git_sha, sizeof(info->git_sha))
           && is_terminated(info->git_branch, sizeof(info->git_branch))
           && is_terminated(info->git_tag, sizeof(info->git_tag))
           && is_terminated(info->build_date, sizeof(info->build_date))
           && is_terminated(info->build_type, sizeof(info->build_type))
           && is_terminated(info->toolchain, sizeof(info->toolchain));
}

/* Bounded string builder: counts the full length, stores what fits. */
typedef struct {
    char  *buf;
    size_t cap; /* bytes available for characters, i.e. buf_len - 1 */
    size_t len; /* characters appended so far, including any that did not fit */
} Banner;

static void banner_append(Banner *b, const char *str, size_t max_chars)
{
    size_t i;

    for (i = 0U; (i < max_chars) && (str[i] != '\0'); i++) {
        if (b->len < b->cap) {
            b->buf[b->len] = str[i];
        }
        b->len++;
    }
}

static void banner_puts(Banner *b, const char *str)
{
    banner_append(b, str, (size_t)-1);
}

size_t fw_info_format_banner(const BuildInfo *info, char *buf, size_t buf_len)
{
    Banner b;

    if ((buf == NULL) || !fw_info_is_valid(info)) {
        if ((buf != NULL) && (buf_len > 0U)) {
            buf[0] = '\0';
        }
        return 0U;
    }

    b.buf = buf;
    b.cap = (buf_len > 0U) ? (buf_len - 1U) : 0U;
    b.len = 0U;

    banner_puts(&b, "Monoboard ");
    banner_puts(&b, info->version);
    banner_puts(&b, " (");
    banner_append(&b, info->git_sha, FW_INFO_BANNER_SHA_CHARS);
    banner_puts(&b, ") ");
    banner_puts(&b, info->build_type);
    banner_puts(&b, " ");
    banner_puts(&b, info->build_date);
    banner_puts(&b, " ");
    banner_puts(&b, info->toolchain);
    if ((info->flags & BUILD_INFO_FLAG_DIRTY) != 0U) {
        banner_puts(&b, " DIRTY");
    }
    if ((info->flags & BUILD_INFO_FLAG_NO_VCS) != 0U) {
        banner_puts(&b, " NO-VCS");
    }

    if (buf_len > 0U) {
        buf[(b.len < b.cap) ? b.len : b.cap] = '\0';
    }
    return b.len;
}
