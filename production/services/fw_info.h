/**
 * @file fw_info.h
 * @brief Read-only access to this image's BuildInfo record (issue #49).
 *
 * The record itself is generated at build time and lives in flash in the
 * `.build_info` section (see build_info_types.h for the layout and
 * tools/build_info.py for reading it out of a binary). This module is the one
 * place that knows it exists: the boot banner and the periodic status CAN
 * frame get version, SHA and dirty flag from here, never from the generated
 * header directly.
 *
 * No RTOS, no hardware, no mutable state, no printf -- safe to call from any
 * context, including before the scheduler starts.
 */
#ifndef MONOBOARD_FW_INFO_H
#define MONOBOARD_FW_INFO_H

#include <stdbool.h>
#include <stddef.h>

#include "build_info_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Characters of the commit SHA shown in the boot banner. */
#define FW_INFO_BANNER_SHA_CHARS 12U

/**
 * The BuildInfo record embedded in this image.
 * @return pointer to the record in flash; never NULL
 */
const BuildInfo *fw_info_get(void);

/**
 * Check that @p info looks like a BuildInfo this firmware understands:
 * right magic, layout version and size, and every string NUL-terminated.
 * @param info  record to check (may be NULL)
 * @return true if @p info is safe to read and print
 */
bool fw_info_is_valid(const BuildInfo *info);

/**
 * Format the one-line boot banner, e.g.
 * `Monoboard v1.2.0-dirty (1a2b3c4d5e6f) Debug 2026-10-03 GNU 13.3.1 DIRTY`.
 * A dirty image always carries the trailing `DIRTY` marker, so it cannot be
 * mistaken for a clean build even if its version string was truncated.
 * Never writes more than @p buf_len bytes, and always NUL-terminates when
 * @p buf_len > 0.
 * @param info     record to describe (typically fw_info_get())
 * @param buf      destination
 * @param buf_len  capacity of @p buf in bytes, including the NUL
 * @return length of the full banner (excluding the NUL), even when it had
 *         to be truncated to fit; 0 if @p info is invalid or @p buf is NULL
 */
size_t fw_info_format_banner(const BuildInfo *info, char *buf, size_t buf_len);

#ifdef __cplusplus
}
#endif

#endif  // MONOBOARD_FW_INFO_H
