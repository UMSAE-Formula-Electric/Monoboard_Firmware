/**
 * @file test_fw_info.c
 * @brief Exercises fw_info (production/services/fw_info.h) and pins the
 * on-flash BuildInfo layout that tools/build_info.py decodes.
 */
#include <stddef.h>
#include <string.h>

#include "fw_info.h"
#include "test_framework.h"

static BuildInfo make_info(uint32_t flags, const char *version)
{
    BuildInfo info;

    (void)memset(&info, 0, sizeof(info));
    info.magic          = BUILD_INFO_MAGIC;
    info.layout_version = (uint16_t)BUILD_INFO_LAYOUT_VERSION;
    info.size_bytes     = (uint16_t)sizeof(BuildInfo);
    info.git_sha32      = 0x1a2b3c4dU;
    info.flags          = flags;
    (void)strncpy(info.version, version, sizeof(info.version) - 1U);
    (void)strncpy(info.git_sha, "1a2b3c4d5e6f7a8b9c0d1a2b3c4d5e6f7a8b9c0d",
                  sizeof(info.git_sha) - 1U);
    (void)strncpy(info.git_branch, "main", sizeof(info.git_branch) - 1U);
    (void)strncpy(info.git_tag, "v1.2.0", sizeof(info.git_tag) - 1U);
    (void)strncpy(info.build_date, "2026-10-03", sizeof(info.build_date) - 1U);
    (void)strncpy(info.build_type, "Debug", sizeof(info.build_type) - 1U);
    (void)strncpy(info.toolchain, "GNU 13.3.1", sizeof(info.toolchain) - 1U);
    return info;
}

/* tools/build_info.py hard-codes these offsets; changing one is a layout
 * change and must bump BUILD_INFO_LAYOUT_VERSION. */
TEST(fw_info, layout_matches_host_decoder)
{
    CHECK(sizeof(BuildInfo) == 224U);
    CHECK(offsetof(BuildInfo, magic) == 0U);
    CHECK(offsetof(BuildInfo, layout_version) == 4U);
    CHECK(offsetof(BuildInfo, size_bytes) == 6U);
    CHECK(offsetof(BuildInfo, git_sha32) == 8U);
    CHECK(offsetof(BuildInfo, flags) == 12U);
    CHECK(offsetof(BuildInfo, version) == 16U);
    CHECK(offsetof(BuildInfo, git_sha) == 48U);
    CHECK(offsetof(BuildInfo, git_branch) == 96U);
    CHECK(offsetof(BuildInfo, git_tag) == 128U);
    CHECK(offsetof(BuildInfo, build_date) == 160U);
    CHECK(offsetof(BuildInfo, build_type) == 176U);
    CHECK(offsetof(BuildInfo, toolchain) == 192U);
}

TEST(fw_info, embedded_record_is_valid)
{
    const BuildInfo *info = fw_info_get();

    CHECK(fw_info_is_valid(info)); /* also false for NULL */
    if (info == NULL) {
        return;
    }
    CHECK(info->version[0] != '\0');
    CHECK(strlen(info->build_date) == 10U); /* YYYY-MM-DD */
}

/* The generator's promises, checked against whatever tree this build came from. */
TEST(fw_info, embedded_record_is_self_consistent)
{
    const BuildInfo *info    = fw_info_get();
    size_t           ver_len = strlen(info->version);
    bool             dirty   = (info->flags & BUILD_INFO_FLAG_DIRTY) != 0U;
    bool ends_dirty = (ver_len >= 6U) && (strcmp(&info->version[ver_len - 6U], "-dirty") == 0);

    CHECK(dirty == ends_dirty);
    if ((info->flags & BUILD_INFO_FLAG_NO_VCS) != 0U) {
        CHECK(info->git_sha32 == 0U);
        CHECK(strcmp(info->git_sha, "unknown") == 0);
    } else {
        CHECK(strlen(info->git_sha) == 40U);
    }
    if ((info->flags & BUILD_INFO_FLAG_TAGGED) != 0U) {
        CHECK(!dirty);
        CHECK(strcmp(info->version, info->git_tag) == 0);
    }
}

TEST(fw_info, rejects_null_and_bad_header)
{
    BuildInfo info = make_info(0U, "v1.2.0");

    CHECK(fw_info_is_valid(&info));
    CHECK(!fw_info_is_valid(NULL));

    info.magic = 0U;
    CHECK(!fw_info_is_valid(&info));

    info                = make_info(0U, "v1.2.0");
    info.layout_version = 99U;
    CHECK(!fw_info_is_valid(&info));

    info            = make_info(0U, "v1.2.0");
    info.size_bytes = 1U;
    CHECK(!fw_info_is_valid(&info));
}

TEST(fw_info, rejects_unterminated_string)
{
    BuildInfo info = make_info(0U, "v1.2.0");

    (void)memset(info.git_tag, 'x', sizeof(info.git_tag));
    CHECK(!fw_info_is_valid(&info));
}

TEST(fw_info, banner_for_clean_build)
{
    BuildInfo info = make_info(BUILD_INFO_FLAG_TAGGED, "v1.2.0");
    char      buf[128];
    size_t    len = fw_info_format_banner(&info, buf, sizeof(buf));

    CHECK(strcmp(buf, "Monoboard v1.2.0 (1a2b3c4d5e6f) Debug 2026-10-03 GNU 13.3.1") == 0);
    CHECK(len == strlen(buf));
}

TEST(fw_info, banner_marks_dirty_build)
{
    BuildInfo info = make_info(BUILD_INFO_FLAG_DIRTY, "v1.2.0-dirty");
    char      buf[128];

    (void)fw_info_format_banner(&info, buf, sizeof(buf));
    CHECK(strcmp(buf, "Monoboard v1.2.0-dirty (1a2b3c4d5e6f) Debug 2026-10-03 GNU 13.3.1 DIRTY")
          == 0);
}

TEST(fw_info, banner_marks_build_without_vcs)
{
    BuildInfo info = make_info(BUILD_INFO_FLAG_NO_VCS, "unknown");
    char      buf[128];
    size_t    len = fw_info_format_banner(&info, buf, sizeof(buf));

    CHECK(len >= 7U);
    CHECK(strcmp(&buf[len - 7U], " NO-VCS") == 0);
}

TEST(fw_info, banner_truncates_and_reports_full_length)
{
    BuildInfo info = make_info(0U, "v1.2.0");
    char      full[128];
    char      small[11];
    size_t    full_len = fw_info_format_banner(&info, full, sizeof(full));

    (void)memset(small, '#', sizeof(small));
    CHECK(fw_info_format_banner(&info, small, sizeof(small)) == full_len);
    CHECK(strcmp(small, "Monoboard ") == 0);
}

TEST(fw_info, banner_invalid_input_writes_empty_string)
{
    BuildInfo info = make_info(0U, "v1.2.0");
    char      buf[8];

    info.magic = 0U;
    buf[0]     = '#';
    CHECK(fw_info_format_banner(&info, buf, sizeof(buf)) == 0U);
    CHECK(buf[0] == '\0');
    CHECK(fw_info_format_banner(fw_info_get(), NULL, 8U) == 0U);
    CHECK(fw_info_format_banner(fw_info_get(), buf, 0U) > 0U); /* length only */
}
