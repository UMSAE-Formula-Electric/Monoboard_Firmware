/**
 * @file crash_record.h
 * @brief Hardware-independent half of the crash dump / reset-cause path
 * (issue #45): the persisted record layout, its validity seal, and the
 * decoders for the Cortex-M fault-status and STM32F4 RCC_CSR registers.
 *
 * Everything here is pure -- plain integers in, plain values out, no
 * register access -- so it builds into the desktop unit tests as well as
 * the firmware. fault_stm32.c is the only producer of records on target:
 * its fault handlers fill and seal one in a no-init RAM section, and the
 * next boot validates, reports and invalidates it.
 */
#ifndef MONOBOARD_CRASH_RECORD_H
#define MONOBOARD_CRASH_RECORD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wdg_if.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Marks a sealed record ("CRSH"); anything else is leftover RAM. */
#define CRASH_RECORD_MAGIC 0x43525348UL
/** Bump whenever CrashRecord's layout changes, so an old image's record
 *  is rejected instead of misread. */
#define CRASH_RECORD_VERSION 1U
/** Bytes kept of the running task's name (configMAX_TASK_NAME_LEN). */
#define CRASH_TASK_NAME_LEN 16U

/** What produced the record. Stored as uint32_t in the record. */
typedef enum {
    CRASH_CAUSE_NONE = 0,
    CRASH_CAUSE_HARD_FAULT,     /**< HardFault (incl. escalated faults) */
    CRASH_CAUSE_MEM_MANAGE,     /**< MemManage fault (MPU / XN violation) */
    CRASH_CAUSE_BUS_FAULT,      /**< BusFault (bad address, bus error) */
    CRASH_CAUSE_USAGE_FAULT,    /**< UsageFault (undef instr, unaligned, div 0) */
    CRASH_CAUSE_STACK_OVERFLOW, /**< vApplicationStackOverflowHook (issue #46) */
    CRASH_CAUSE_MALLOC_FAILED,  /**< vApplicationMallocFailedHook (issue #46) */
    CRASH_CAUSE_ASSERT,         /**< configASSERT failure (issue #46) */
    CRASH_CAUSE_COUNT
} CrashCause;

/** Why the MCU last reset, decoded from RCC_CSR. Finer-grained than
 *  WdgResetCause -- see reset_cause_to_wdg(). */
typedef enum {
    RESET_CAUSE_UNKNOWN = 0,     /**< no flag set (already cleared?) */
    RESET_CAUSE_POWER_ON,        /**< PORRSTF: cold power-on */
    RESET_CAUSE_BROWN_OUT,       /**< BORRSTF without PORRSTF: supply dip */
    RESET_CAUSE_PIN,             /**< only PINRSTF: external NRST */
    RESET_CAUSE_SOFTWARE,        /**< SFTRSTF: NVIC_SystemReset() */
    RESET_CAUSE_INDEPENDENT_WDG, /**< IWDGRSTF: independent watchdog */
    RESET_CAUSE_WINDOW_WDG,      /**< WWDGRSTF: window watchdog */
    RESET_CAUSE_LOW_POWER,       /**< LPWRRSTF: illegal Stop/Standby entry */
    RESET_CAUSE_COUNT
} ResetCause;

/**
 * The persisted crash dump. Lives in a no-init RAM section on target, so
 * every field is fixed-width and the layout has no implicit padding --
 * the CRC covers every byte before @c crc.
 */
typedef struct {
    uint32_t magic;   /**< CRASH_RECORD_MAGIC when sealed */
    uint32_t version; /**< CRASH_RECORD_VERSION when sealed */
    uint32_t cause;   /**< CrashCause */
    /* Exception frame the core stacked on fault entry (0 if not a fault
     * or the stack pointer was unusable). */
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r12;
    uint32_t lr;                             /**< stacked LR: caller of the faulting function */
    uint32_t pc;                             /**< stacked PC: the faulting instruction */
    uint32_t xpsr;                           /**< stacked xPSR */
    uint32_t exc_return;                     /**< handler LR (EXC_RETURN); bit 2 = PSP */
    uint32_t sp;                             /**< address of the stacked frame */
    uint32_t cfsr;                           /**< SCB->CFSR (MMFSR | BFSR << 8 | UFSR << 16) */
    uint32_t hfsr;                           /**< SCB->HFSR */
    uint32_t mmfar;                          /**< SCB->MMFAR (meaningful iff CFSR.MMARVALID) */
    uint32_t bfar;                           /**< SCB->BFAR (meaningful iff CFSR.BFARVALID) */
    char     task_name[CRASH_TASK_NAME_LEN]; /**< NUL-padded; "" pre-scheduler */
    uint32_t crc;                            /**< crash_record_crc() of the above */
} CrashRecord;

/** CRC-32 (IEEE 802.3, reflected, init/xorout 0xFFFFFFFF) of @p len bytes. */
uint32_t crash_crc32(const void *data, size_t len);

/** CRC over every byte of @p rec that precedes its @c crc field. */
uint32_t crash_record_crc(const CrashRecord *rec);

/** Stamp magic, version and CRC so the record survives validation. Call
 *  last, after every other field is written. */
void crash_record_seal(CrashRecord *rec);

/** True iff @p rec carries the magic, the current version, an in-range
 *  cause, and a matching CRC. False for NULL. */
bool crash_record_is_valid(const CrashRecord *rec);

/**
 * True iff @p rec is valid AND @p reset_cause is one where RAM was kept
 * powered. After a power-on or brown-out reset SRAM content is undefined,
 * so even a record that happens to validate is discarded -- a cold boot
 * never reports a phantom crash.
 */
bool crash_record_is_reportable(const CrashRecord *rec, ResetCause reset_cause);

/** Wipe @p rec so it will never validate again (report-once semantics). */
void crash_record_invalidate(CrashRecord *rec);

/**
 * Fill the exception-frame fields of @p rec from the eight words the core
 * stacked on fault entry (R0, R1, R2, R3, R12, LR, PC, xPSR in that order).
 * @p frame may be NULL (frame unreadable): those fields are zeroed.
 */
void crash_record_set_frame(CrashRecord *rec, const uint32_t *frame);

/** Copy @p name (may be NULL) into @p rec, truncated and NUL-padded. */
void crash_record_set_task_name(CrashRecord *rec, const char *name);

/** EXC_RETURN bit 2: the faulting context ran on the process stack (PSP,
 *  i.e. a FreeRTOS task) rather than the main stack (MSP). */
bool crash_exc_return_uses_psp(uint32_t exc_return);

/** Map an active exception number (IPSR[8:0]) to the fault it denotes;
 *  anything that is not exception 3..6 reports CRASH_CAUSE_HARD_FAULT. */
CrashCause crash_cause_from_exception(uint32_t exception_number);

/**
 * True iff the eight-word frame at @p sp lies wholly inside
 * [@p ram_start, @p ram_end) and is word-aligned -- i.e. it is safe to read
 * from inside a fault handler without risking a nested fault.
 */
bool crash_frame_address_ok(uint32_t sp, uint32_t ram_start, uint32_t ram_end);

/** Short name of @p cause, e.g. "HardFault". Never NULL. */
const char *crash_cause_name(CrashCause cause);

/**
 * The most specific explanation of a fault, from CFSR then HFSR, e.g.
 * "UNALIGNED: unaligned load/store" or "DIVBYZERO: integer divide by
 * zero". Precise bus/memory faults come first because they carry an
 * address. Never NULL; "none" when no status bit is set.
 */
const char *crash_fault_reason(uint32_t cfsr, uint32_t hfsr);

/** CFSR.MMARVALID: MMFAR holds the faulting data address. */
bool crash_mmfar_valid(uint32_t cfsr);

/** CFSR.BFARVALID: BFAR holds the faulting data address. */
bool crash_bfar_valid(uint32_t cfsr);

/**
 * Decode a raw RCC_CSR value. Several flags are set together (a power-on
 * also sets BORRSTF and PINRSTF; every internal reset is also driven out
 * on NRST, so PINRSTF accompanies them), so the most specific source wins:
 * watchdogs and low-power first, then software, power-on, brown-out and
 * finally a bare pin reset.
 */
ResetCause reset_cause_decode(uint32_t rcc_csr);

/** Short name of @p cause, e.g. "IWDG". Never NULL. */
const char *reset_cause_name(ResetCause cause);

/** Collapse to the coarser wdg_if.h vocabulary, so a watchdog driver can
 *  report the cause latched here instead of re-reading cleared flags. */
WdgResetCause reset_cause_to_wdg(ResetCause cause);

/**
 * Render a human-readable boot report into @p buf: one banner line (firmware
 * version, reset cause, raw RCC_CSR, whether a dump was found) and, when
 * @p rec is non-NULL, the dump itself. The PC/LR lines are formatted so
 * tools/crash_decode.py can resolve them against the matching .elf.
 * @param buf          destination, always NUL-terminated when @p buf_len > 0
 * @param buf_len      size of @p buf in bytes
 * @param fw_version   version string for the banner (NULL prints "?")
 * @param reset_cause  decoded reset cause
 * @param rcc_csr      raw RCC_CSR as latched at boot
 * @param rec          the reported crash record, or NULL if none was found
 * @return the length the full report needs (excluding the NUL), like
 *         snprintf; compare against @p buf_len to detect truncation
 */
size_t crash_format_report(char              *buf,
                           size_t             buf_len,
                           const char        *fw_version,
                           ResetCause         reset_cause,
                           uint32_t           rcc_csr,
                           const CrashRecord *rec);

#ifdef __cplusplus
}
#endif

#endif  // MONOBOARD_CRASH_RECORD_H
