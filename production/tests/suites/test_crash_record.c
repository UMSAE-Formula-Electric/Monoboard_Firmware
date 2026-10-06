/**
 * @file test_crash_record.c
 * @brief Exercises the pure half of the crash-dump / reset-cause path
 * (production/drivers/crash_record.h, issue #45): record sealing and
 * validation, frame capture, fault-status and RCC_CSR decoding, and the
 * boot report text.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "crash_record.h"
#include "test_framework.h"

/* Register bits under test, spelled out so the tests pin the decoders to
 * the reference manuals rather than to the implementation's own macros. */
#define CFSR_DACCVIOL    (1UL << 1)
#define CFSR_MMARVALID   (1UL << 7)
#define CFSR_PRECISERR   (1UL << 9)
#define CFSR_IMPRECISERR (1UL << 10)
#define CFSR_STKERR      (1UL << 12)
#define CFSR_BFARVALID   (1UL << 15)
#define CFSR_UNDEFINSTR  (1UL << 16)
#define CFSR_INVSTATE    (1UL << 17)
#define CFSR_UNALIGNED   (1UL << 24)
#define CFSR_DIVBYZERO   (1UL << 25)
#define HFSR_FORCED      (1UL << 30)

#define CSR_BORRSTF  (1UL << 25)
#define CSR_PINRSTF  (1UL << 26)
#define CSR_PORRSTF  (1UL << 27)
#define CSR_SFTRSTF  (1UL << 28)
#define CSR_IWDGRSTF (1UL << 29)
#define CSR_WWDGRSTF (1UL << 30)
#define CSR_LPWRRSTF (1UL << 31)
#define CSR_LSION    (1UL << 0)

#define RAM_START 0x20000000UL
#define RAM_END   0x20020000UL

static CrashRecord make_sealed_record(void)
{
    static const uint32_t frame[8] = {0x11U, 0x22U,       0x33U,       0x44U,
                                      0xCCU, 0x08000123U, 0x08000456U, 0x01000000U};
    CrashRecord           rec;

    (void)memset(&rec, 0xA5, sizeof(rec));
    rec.cause = (uint32_t)CRASH_CAUSE_USAGE_FAULT;
    crash_record_set_frame(&rec, frame);
    rec.exc_return = 0xFFFFFFFDU; /* thread mode, PSP */
    rec.sp         = 0x20001000U;
    rec.cfsr       = CFSR_UNALIGNED;
    rec.hfsr       = 0U;
    rec.mmfar      = 0U;
    rec.bfar       = 0U;
    crash_record_set_task_name(&rec, "pedal");
    crash_record_seal(&rec);
    return rec;
}

/* ---- CRC ------------------------------------------------------------------ */

TEST(crash_record, crc32_matches_ieee_check_value)
{
    /* The standard CRC-32 check value for "123456789". */
    CHECK(crash_crc32("123456789", 9U) == 0xCBF43926UL);
    CHECK(crash_crc32("", 0U) == 0U);
}

/* ---- seal / validate / invalidate ---------------------------------------- */

TEST(crash_record, sealed_record_is_valid)
{
    CrashRecord rec = make_sealed_record();

    CHECK(rec.magic == CRASH_RECORD_MAGIC);
    CHECK(rec.version == CRASH_RECORD_VERSION);
    CHECK(crash_record_is_valid(&rec));
}

TEST(crash_record, null_is_not_valid)
{
    CHECK(!crash_record_is_valid(NULL));
    CHECK(!crash_record_is_reportable(NULL, RESET_CAUSE_SOFTWARE));
}

TEST(crash_record, any_flipped_bit_invalidates)
{
    const CrashRecord good = make_sealed_record();
    const size_t      size = sizeof(good);

    /* Every single-bit flip anywhere in the record -- payload, magic,
     * version or the CRC itself -- must be detected. */
    for (size_t byte = 0U; byte < size; byte++) {
        for (unsigned int bit = 0U; bit < 8U; bit++) {
            CrashRecord bad = good;
            uint8_t    *raw = (uint8_t *)&bad;

            raw[byte] = (uint8_t)(raw[byte] ^ (uint8_t)(1U << bit));
            CHECK(!crash_record_is_valid(&bad));
        }
    }
}

TEST(crash_record, wrong_version_is_rejected_even_with_matching_crc)
{
    CrashRecord rec = make_sealed_record();

    rec.version = CRASH_RECORD_VERSION + 1U;
    rec.crc     = crash_record_crc(&rec);
    CHECK(!crash_record_is_valid(&rec));
}

TEST(crash_record, out_of_range_cause_is_rejected_even_with_matching_crc)
{
    CrashRecord rec = make_sealed_record();

    rec.cause = (uint32_t)CRASH_CAUSE_NONE;
    rec.crc   = crash_record_crc(&rec);
    CHECK(!crash_record_is_valid(&rec));

    rec.cause = (uint32_t)CRASH_CAUSE_COUNT;
    rec.crc   = crash_record_crc(&rec);
    CHECK(!crash_record_is_valid(&rec));
}

TEST(crash_record, garbage_ram_is_not_valid)
{
    CrashRecord rec;
    uint32_t    lcg = 12345U;
    uint8_t    *raw = (uint8_t *)&rec;

    /* Pseudo-random fill, as SRAM looks after power-up. */
    for (int round = 0; round < 256; round++) {
        for (size_t i = 0U; i < sizeof(rec); i++) {
            lcg    = (lcg * 1103515245U) + 12345U;
            raw[i] = (uint8_t)(lcg >> 24);
        }
        CHECK(!crash_record_is_valid(&rec));
    }

    (void)memset(&rec, 0, sizeof(rec));
    CHECK(!crash_record_is_valid(&rec));
    (void)memset(&rec, 0xFF, sizeof(rec));
    CHECK(!crash_record_is_valid(&rec));
}

TEST(crash_record, invalidate_makes_it_unreportable)
{
    CrashRecord rec = make_sealed_record();

    crash_record_invalidate(&rec);
    CHECK(!crash_record_is_valid(&rec));
    CHECK(rec.magic == 0U);
}

TEST(crash_record, cold_boot_never_reports_a_crash)
{
    const CrashRecord rec = make_sealed_record();

    CHECK(!crash_record_is_reportable(&rec, RESET_CAUSE_POWER_ON));
    CHECK(!crash_record_is_reportable(&rec, RESET_CAUSE_BROWN_OUT));
}

TEST(crash_record, warm_resets_report_a_valid_crash)
{
    const CrashRecord rec = make_sealed_record();

    CHECK(crash_record_is_reportable(&rec, RESET_CAUSE_SOFTWARE));
    CHECK(crash_record_is_reportable(&rec, RESET_CAUSE_INDEPENDENT_WDG));
    CHECK(crash_record_is_reportable(&rec, RESET_CAUSE_WINDOW_WDG));
    CHECK(crash_record_is_reportable(&rec, RESET_CAUSE_PIN));
    CHECK(crash_record_is_reportable(&rec, RESET_CAUSE_UNKNOWN));
}

/* ---- frame / task name ---------------------------------------------------- */

TEST(crash_record, frame_words_land_in_order)
{
    const CrashRecord rec = make_sealed_record();

    CHECK(rec.r0 == 0x11U);
    CHECK(rec.r1 == 0x22U);
    CHECK(rec.r2 == 0x33U);
    CHECK(rec.r3 == 0x44U);
    CHECK(rec.r12 == 0xCCU);
    CHECK(rec.lr == 0x08000123U);
    CHECK(rec.pc == 0x08000456U);
    CHECK(rec.xpsr == 0x01000000U);
}

TEST(crash_record, null_frame_zeroes_registers)
{
    CrashRecord rec = make_sealed_record();

    crash_record_set_frame(&rec, NULL);
    CHECK(rec.r0 == 0U);
    CHECK(rec.pc == 0U);
    CHECK(rec.lr == 0U);
    CHECK(rec.xpsr == 0U);
}

TEST(crash_record, task_name_is_truncated_and_nul_padded)
{
    CrashRecord rec;

    (void)memset(&rec, 0x5A, sizeof(rec));
    crash_record_set_task_name(&rec, "a_very_long_task_name_indeed");
    CHECK(rec.task_name[CRASH_TASK_NAME_LEN - 1U] == '\0');
    CHECK(strncmp(rec.task_name, "a_very_long_tas", CRASH_TASK_NAME_LEN) == 0);

    crash_record_set_task_name(&rec, "can");
    CHECK(strcmp(rec.task_name, "can") == 0);
    for (size_t i = 3U; i < CRASH_TASK_NAME_LEN; i++) {
        CHECK(rec.task_name[i] == '\0');
    }

    crash_record_set_task_name(&rec, NULL);
    CHECK(rec.task_name[0] == '\0');
}

/* ---- exception entry helpers ---------------------------------------------- */

TEST(crash_record, exc_return_bit2_selects_psp)
{
    CHECK(crash_exc_return_uses_psp(0xFFFFFFFDU));  /* thread, PSP, no FP */
    CHECK(crash_exc_return_uses_psp(0xFFFFFFEDU));  /* thread, PSP, FP frame */
    CHECK(!crash_exc_return_uses_psp(0xFFFFFFF9U)); /* thread, MSP */
    CHECK(!crash_exc_return_uses_psp(0xFFFFFFF1U)); /* handler, MSP */
}

TEST(crash_record, exception_number_maps_to_cause)
{
    CHECK(crash_cause_from_exception(3U) == CRASH_CAUSE_HARD_FAULT);
    CHECK(crash_cause_from_exception(4U) == CRASH_CAUSE_MEM_MANAGE);
    CHECK(crash_cause_from_exception(5U) == CRASH_CAUSE_BUS_FAULT);
    CHECK(crash_cause_from_exception(6U) == CRASH_CAUSE_USAGE_FAULT);
    CHECK(crash_cause_from_exception(0U) == CRASH_CAUSE_HARD_FAULT);
    CHECK(crash_cause_from_exception(42U) == CRASH_CAUSE_HARD_FAULT);
}

TEST(crash_record, frame_address_must_be_aligned_and_inside_ram)
{
    CHECK(crash_frame_address_ok(RAM_START, RAM_START, RAM_END));
    CHECK(crash_frame_address_ok(RAM_END - 32U, RAM_START, RAM_END));
    CHECK(!crash_frame_address_ok(RAM_END - 28U, RAM_START, RAM_END)); /* runs off the end */
    CHECK(!crash_frame_address_ok(RAM_START - 4U, RAM_START, RAM_END));
    CHECK(!crash_frame_address_ok(RAM_START + 2U, RAM_START, RAM_END)); /* misaligned */
    CHECK(!crash_frame_address_ok(0U, RAM_START, RAM_END));
    CHECK(!crash_frame_address_ok(0xFFFFFFFCU, RAM_START, RAM_END));
    CHECK(!crash_frame_address_ok(0U, 0U, 16U)); /* RAM smaller than a frame */
}

/* ---- fault status decode -------------------------------------------------- */

TEST(crash_record, fault_reason_names_the_usage_faults)
{
    CHECK(strncmp(crash_fault_reason(CFSR_UNALIGNED, HFSR_FORCED), "UNALIGNED", 9U) == 0);
    CHECK(strncmp(crash_fault_reason(CFSR_DIVBYZERO, 0U), "DIVBYZERO", 9U) == 0);
    CHECK(strncmp(crash_fault_reason(CFSR_UNDEFINSTR, 0U), "UNDEFINSTR", 10U) == 0);
    CHECK(strncmp(crash_fault_reason(CFSR_INVSTATE, 0U), "INVSTATE", 8U) == 0);
}

TEST(crash_record, fault_reason_prefers_precise_address_faults)
{
    /* A null dereference: precise bus error with BFAR valid. */
    const uint32_t null_deref = CFSR_PRECISERR | CFSR_BFARVALID;

    CHECK(strncmp(crash_fault_reason(null_deref | CFSR_STKERR, HFSR_FORCED), "PRECISERR", 9U) == 0);
    CHECK(strncmp(crash_fault_reason(CFSR_DACCVIOL | CFSR_MMARVALID, 0U), "DACCVIOL", 8U) == 0);
    CHECK(strncmp(crash_fault_reason(CFSR_IMPRECISERR, 0U), "IMPRECISERR", 11U) == 0);
}

TEST(crash_record, fault_reason_falls_back_to_hfsr_then_none)
{
    CHECK(strncmp(crash_fault_reason(0U, HFSR_FORCED), "FORCED", 6U) == 0);
    CHECK(strcmp(crash_fault_reason(0U, 0U), "none") == 0);
}

TEST(crash_record, fault_address_valid_bits)
{
    CHECK(crash_mmfar_valid(CFSR_MMARVALID));
    CHECK(!crash_mmfar_valid(CFSR_BFARVALID));
    CHECK(crash_bfar_valid(CFSR_BFARVALID));
    CHECK(!crash_bfar_valid(CFSR_MMARVALID));
}

/* ---- reset cause decode --------------------------------------------------- */

TEST(crash_record, reset_cause_power_on_beats_bor_and_pin)
{
    /* What the F446 actually latches on a cold power-up. */
    CHECK(reset_cause_decode(CSR_PORRSTF | CSR_BORRSTF | CSR_PINRSTF | CSR_LSION)
          == RESET_CAUSE_POWER_ON);
    CHECK(reset_cause_decode(CSR_BORRSTF | CSR_PINRSTF) == RESET_CAUSE_BROWN_OUT);
}

TEST(crash_record, reset_cause_watchdog_is_distinct_from_power_on)
{
    CHECK(reset_cause_decode(CSR_IWDGRSTF | CSR_PINRSTF) == RESET_CAUSE_INDEPENDENT_WDG);
    CHECK(reset_cause_decode(CSR_WWDGRSTF | CSR_PINRSTF) == RESET_CAUSE_WINDOW_WDG);
    CHECK(reset_cause_decode(CSR_IWDGRSTF | CSR_PINRSTF)
          != reset_cause_decode(CSR_PORRSTF | CSR_BORRSTF | CSR_PINRSTF));
}

TEST(crash_record, reset_cause_software_pin_low_power_unknown)
{
    CHECK(reset_cause_decode(CSR_SFTRSTF | CSR_PINRSTF) == RESET_CAUSE_SOFTWARE);
    CHECK(reset_cause_decode(CSR_PINRSTF) == RESET_CAUSE_PIN);
    CHECK(reset_cause_decode(CSR_LPWRRSTF | CSR_PINRSTF) == RESET_CAUSE_LOW_POWER);
    CHECK(reset_cause_decode(0U) == RESET_CAUSE_UNKNOWN);
    CHECK(reset_cause_decode(CSR_LSION) == RESET_CAUSE_UNKNOWN);
}

TEST(crash_record, reset_cause_names)
{
    CHECK(strcmp(reset_cause_name(RESET_CAUSE_INDEPENDENT_WDG), "IWDG") == 0);
    CHECK(strcmp(reset_cause_name(RESET_CAUSE_POWER_ON), "POWER_ON") == 0);
    CHECK(strcmp(reset_cause_name(RESET_CAUSE_COUNT), "UNKNOWN") == 0);
    CHECK(strcmp(crash_cause_name(CRASH_CAUSE_STACK_OVERFLOW), "StackOverflow") == 0);
    CHECK(strcmp(crash_cause_name(CRASH_CAUSE_COUNT), "none") == 0);
}

TEST(crash_record, reset_cause_maps_onto_wdg_if)
{
    CHECK(reset_cause_to_wdg(RESET_CAUSE_POWER_ON) == WDG_RESET_CAUSE_POWER_ON);
    CHECK(reset_cause_to_wdg(RESET_CAUSE_BROWN_OUT) == WDG_RESET_CAUSE_POWER_ON);
    CHECK(reset_cause_to_wdg(RESET_CAUSE_PIN) == WDG_RESET_CAUSE_PIN);
    CHECK(reset_cause_to_wdg(RESET_CAUSE_SOFTWARE) == WDG_RESET_CAUSE_SOFTWARE);
    CHECK(reset_cause_to_wdg(RESET_CAUSE_INDEPENDENT_WDG) == WDG_RESET_CAUSE_WATCHDOG);
    CHECK(reset_cause_to_wdg(RESET_CAUSE_WINDOW_WDG) == WDG_RESET_CAUSE_WATCHDOG);
    CHECK(reset_cause_to_wdg(RESET_CAUSE_LOW_POWER) == WDG_RESET_CAUSE_UNKNOWN);
    CHECK(reset_cause_to_wdg(RESET_CAUSE_UNKNOWN) == WDG_RESET_CAUSE_UNKNOWN);
}

/* ---- boot report ---------------------------------------------------------- */

TEST(crash_record, report_without_crash_is_one_banner_line)
{
    char         buf[128];
    const size_t len = crash_format_report(buf, sizeof(buf), "1.2.3", RESET_CAUSE_INDEPENDENT_WDG,
                                           CSR_IWDGRSTF | CSR_PINRSTF, NULL);

    CHECK(strcmp(buf, "BOOT fw=1.2.3 reset=IWDG csr=0x24000000 crash=no\n") == 0);
    CHECK(len == strlen(buf));
}

TEST(crash_record, report_with_crash_carries_pc_lr_and_reason)
{
    char              buf[512];
    const CrashRecord rec = make_sealed_record();
    const size_t      len = crash_format_report(buf, sizeof(buf), NULL, RESET_CAUSE_SOFTWARE,
                                                CSR_SFTRSTF, &rec);

    CHECK(len == strlen(buf));
    CHECK(strstr(buf, "BOOT fw=? reset=SOFTWARE csr=0x10000000 crash=yes\n") == buf);
    CHECK(strstr(buf, "CRASH cause=UsageFault task=pedal stack=PSP\n") != NULL);
    CHECK(strstr(buf, " pc=0x08000456 lr=0x08000123 ") != NULL);
    CHECK(strstr(buf, " r12=0x000000CC\n") != NULL);
    CHECK(strstr(buf, "reason=UNALIGNED") != NULL);
    CHECK(strstr(buf, "mmfar=") == NULL); /* not valid in CFSR -> not printed */
    CHECK(strstr(buf, "bfar=") == NULL);
}

TEST(crash_record, report_prints_fault_address_only_when_valid)
{
    char        buf[512];
    CrashRecord rec = make_sealed_record();

    rec.cfsr = CFSR_PRECISERR | CFSR_BFARVALID;
    rec.bfar = 0x00000004U;
    (void)crash_format_report(buf, sizeof(buf), "x", RESET_CAUSE_SOFTWARE, 0U, &rec);
    CHECK(strstr(buf, "CRASH bfar=0x00000004\n") != NULL);
    CHECK(strstr(buf, "mmfar=") == NULL);
}

TEST(crash_record, report_truncates_safely_and_reports_needed_length)
{
    char              full[512];
    char              small[24];
    const CrashRecord rec  = make_sealed_record();
    const size_t needed    = crash_format_report(full, sizeof(full), "v", RESET_CAUSE_SOFTWARE, 0U,
                                                 &rec);
    const size_t truncated = crash_format_report(small, sizeof(small), "v", RESET_CAUSE_SOFTWARE,
                                                 0U, &rec);

    CHECK(truncated == needed);
    CHECK(strlen(small) == sizeof(small) - 1U);
    CHECK(strncmp(small, full, sizeof(small) - 1U) == 0);
    CHECK(crash_format_report(NULL, 0U, "v", RESET_CAUSE_SOFTWARE, 0U, &rec) == needed);
}
