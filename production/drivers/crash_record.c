/**
 * @file crash_record.c
 * @brief Pure crash-record, fault-status and reset-cause logic (issue #45).
 * See crash_record.h. No register access: fault_stm32.c reads the hardware
 * and hands plain values in, which is what lets the desktop tests cover it.
 */
#include "crash_record.h"

#include <string.h>

_Static_assert(offsetof(CrashRecord, crc) == (17U * sizeof(uint32_t)) + CRASH_TASK_NAME_LEN,
               "CrashRecord must have no padding: the CRC covers every byte before crc");
_Static_assert(sizeof(CrashRecord) == offsetof(CrashRecord, crc) + sizeof(uint32_t),
               "CrashRecord must have no tail padding");

/* ---- Cortex-M4 fault status bits (ARMv7-M ARM, B3.2.15 / B3.2.16) ------- */

/* CFSR[7:0] MemManage status */
#define CFSR_IACCVIOL  (1UL << 0)
#define CFSR_DACCVIOL  (1UL << 1)
#define CFSR_MUNSTKERR (1UL << 3)
#define CFSR_MSTKERR   (1UL << 4)
#define CFSR_MLSPERR   (1UL << 5)
#define CFSR_MMARVALID (1UL << 7)
/* CFSR[15:8] BusFault status */
#define CFSR_IBUSERR     (1UL << 8)
#define CFSR_PRECISERR   (1UL << 9)
#define CFSR_IMPRECISERR (1UL << 10)
#define CFSR_UNSTKERR    (1UL << 11)
#define CFSR_STKERR      (1UL << 12)
#define CFSR_LSPERR      (1UL << 13)
#define CFSR_BFARVALID   (1UL << 15)
/* CFSR[31:16] UsageFault status */
#define CFSR_UNDEFINSTR (1UL << 16)
#define CFSR_INVSTATE   (1UL << 17)
#define CFSR_INVPC      (1UL << 18)
#define CFSR_NOCP       (1UL << 19)
#define CFSR_UNALIGNED  (1UL << 24)
#define CFSR_DIVBYZERO  (1UL << 25)
/* HFSR */
#define HFSR_VECTTBL  (1UL << 1)
#define HFSR_FORCED   (1UL << 30)
#define HFSR_DEBUGEVT (1UL << 31)

/* EXC_RETURN bit 2: 1 = return to thread mode on PSP. */
#define EXC_RETURN_SPSEL (1UL << 2)

/* Exception numbers (IPSR) of the four configurable/fixed fault vectors. */
#define EXC_NUM_HARD_FAULT  3U
#define EXC_NUM_MEM_MANAGE  4U
#define EXC_NUM_BUS_FAULT   5U
#define EXC_NUM_USAGE_FAULT 6U

/* Words the core pushes on exception entry (basic frame). */
#define FRAME_WORDS 8U

/* ---- STM32F446 RCC_CSR reset flags (RM0390, 6.3.21) ---------------------- */
#define CSR_BORRSTF  (1UL << 25)
#define CSR_PINRSTF  (1UL << 26)
#define CSR_PORRSTF  (1UL << 27)
#define CSR_SFTRSTF  (1UL << 28)
#define CSR_IWDGRSTF (1UL << 29)
#define CSR_WWDGRSTF (1UL << 30)
#define CSR_LPWRRSTF (1UL << 31)

#define CRC32_POLY_REFLECTED 0xEDB88320UL
#define CRC32_INIT           0xFFFFFFFFUL

uint32_t crash_crc32(const void *data, size_t len)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t       crc   = CRC32_INIT;

    for (size_t i = 0U; i < len; i++) {
        crc ^= bytes[i];
        for (unsigned int bit = 0U; bit < 8U; bit++) {
            const uint32_t mask = 0U - (crc & 1UL);
            crc                 = (crc >> 1) ^ (CRC32_POLY_REFLECTED & mask);
        }
    }
    return crc ^ CRC32_INIT;
}

uint32_t crash_record_crc(const CrashRecord *rec)
{
    return crash_crc32(rec, offsetof(CrashRecord, crc));
}

void crash_record_seal(CrashRecord *rec)
{
    rec->magic   = CRASH_RECORD_MAGIC;
    rec->version = CRASH_RECORD_VERSION;
    rec->crc     = crash_record_crc(rec);
}

bool crash_record_is_valid(const CrashRecord *rec)
{
    if (rec == NULL) {
        return false;
    }
    if ((rec->magic != CRASH_RECORD_MAGIC) || (rec->version != CRASH_RECORD_VERSION)) {
        return false;
    }
    if ((rec->cause == (uint32_t)CRASH_CAUSE_NONE) || (rec->cause >= (uint32_t)CRASH_CAUSE_COUNT)) {
        return false;
    }
    return rec->crc == crash_record_crc(rec);
}

bool crash_record_is_reportable(const CrashRecord *rec, ResetCause reset_cause)
{
    if ((reset_cause == RESET_CAUSE_POWER_ON) || (reset_cause == RESET_CAUSE_BROWN_OUT)) {
        return false;
    }
    return crash_record_is_valid(rec);
}

void crash_record_invalidate(CrashRecord *rec)
{
    (void)memset(rec, 0, sizeof(*rec));
}

void crash_record_set_frame(CrashRecord *rec, const uint32_t *frame)
{
    uint32_t words[FRAME_WORDS] = {0U};

    if (frame != NULL) {
        for (unsigned int i = 0U; i < FRAME_WORDS; i++) {
            words[i] = frame[i];
        }
    }
    rec->r0   = words[0];
    rec->r1   = words[1];
    rec->r2   = words[2];
    rec->r3   = words[3];
    rec->r12  = words[4];
    rec->lr   = words[5];
    rec->pc   = words[6];
    rec->xpsr = words[7];
}

void crash_record_set_task_name(CrashRecord *rec, const char *name)
{
    size_t i = 0U;

    if (name != NULL) {
        for (; (i < (CRASH_TASK_NAME_LEN - 1U)) && (name[i] != '\0'); i++) {
            rec->task_name[i] = name[i];
        }
    }
    for (; i < CRASH_TASK_NAME_LEN; i++) {
        rec->task_name[i] = '\0';
    }
}

bool crash_exc_return_uses_psp(uint32_t exc_return)
{
    return (exc_return & EXC_RETURN_SPSEL) != 0U;
}

CrashCause crash_cause_from_exception(uint32_t exception_number)
{
    switch (exception_number) {
        case EXC_NUM_MEM_MANAGE:
            return CRASH_CAUSE_MEM_MANAGE;
        case EXC_NUM_BUS_FAULT:
            return CRASH_CAUSE_BUS_FAULT;
        case EXC_NUM_USAGE_FAULT:
            return CRASH_CAUSE_USAGE_FAULT;
        case EXC_NUM_HARD_FAULT:
        default:
            return CRASH_CAUSE_HARD_FAULT;
    }
}

bool crash_frame_address_ok(uint32_t sp, uint32_t ram_start, uint32_t ram_end)
{
    const uint32_t frame_bytes = FRAME_WORDS * (uint32_t)sizeof(uint32_t);

    if ((sp & 3U) != 0U) {
        return false;
    }
    if ((sp < ram_start) || (ram_end < frame_bytes)) {
        return false;
    }
    return sp <= (ram_end - frame_bytes);
}

const char *crash_cause_name(CrashCause cause)
{
    switch (cause) {
        case CRASH_CAUSE_HARD_FAULT:
            return "HardFault";
        case CRASH_CAUSE_MEM_MANAGE:
            return "MemManage";
        case CRASH_CAUSE_BUS_FAULT:
            return "BusFault";
        case CRASH_CAUSE_USAGE_FAULT:
            return "UsageFault";
        case CRASH_CAUSE_STACK_OVERFLOW:
            return "StackOverflow";
        case CRASH_CAUSE_MALLOC_FAILED:
            return "MallocFailed";
        case CRASH_CAUSE_ASSERT:
            return "Assert";
        case CRASH_CAUSE_NONE:
        case CRASH_CAUSE_COUNT:
        default:
            return "none";
    }
}

typedef struct {
    uint32_t    bit;
    const char *reason;
} FaultBit;

/* Most specific first: address-carrying faults, then the rest. */
static const FaultBit cfsr_reasons[] = {
    {CFSR_DACCVIOL, "DACCVIOL: data access violation (see MMFAR)"},
    {CFSR_PRECISERR, "PRECISERR: precise data bus error (see BFAR)"},
    {CFSR_IACCVIOL, "IACCVIOL: instruction fetch from XN/protected region"},
    {CFSR_IBUSERR, "IBUSERR: instruction fetch bus error"},
    {CFSR_IMPRECISERR, "IMPRECISERR: imprecise data bus error (PC is after the access)"},
    {CFSR_UNDEFINSTR, "UNDEFINSTR: undefined instruction"},
    {CFSR_INVSTATE, "INVSTATE: invalid EPSR state (Thumb bit clear -- bad function pointer?)"},
    {CFSR_INVPC, "INVPC: invalid EXC_RETURN on exception return"},
    {CFSR_NOCP, "NOCP: coprocessor (FPU) access while disabled"},
    {CFSR_UNALIGNED, "UNALIGNED: unaligned load/store"},
    {CFSR_DIVBYZERO, "DIVBYZERO: integer divide by zero"},
    {CFSR_MSTKERR, "MSTKERR: MemManage fault stacking exception entry (stack overflow?)"},
    {CFSR_MUNSTKERR, "MUNSTKERR: MemManage fault unstacking exception return"},
    {CFSR_MLSPERR, "MLSPERR: MemManage fault during lazy FP state save"},
    {CFSR_STKERR, "STKERR: bus fault stacking exception entry (stack overflow?)"},
    {CFSR_UNSTKERR, "UNSTKERR: bus fault unstacking exception return"},
    {CFSR_LSPERR, "LSPERR: bus fault during lazy FP state save"},
};

static const FaultBit hfsr_reasons[] = {
    {HFSR_VECTTBL, "VECTTBL: bus fault reading the vector table"},
    {HFSR_FORCED, "FORCED: escalated fault (handler disabled or masked)"},
    {HFSR_DEBUGEVT, "DEBUGEVT: debug event (breakpoint without debugger?)"},
};

const char *crash_fault_reason(uint32_t cfsr, uint32_t hfsr)
{
    for (size_t i = 0U; i < (sizeof(cfsr_reasons) / sizeof(cfsr_reasons[0])); i++) {
        if ((cfsr & cfsr_reasons[i].bit) != 0U) {
            return cfsr_reasons[i].reason;
        }
    }
    for (size_t i = 0U; i < (sizeof(hfsr_reasons) / sizeof(hfsr_reasons[0])); i++) {
        if ((hfsr & hfsr_reasons[i].bit) != 0U) {
            return hfsr_reasons[i].reason;
        }
    }
    return "none";
}

bool crash_mmfar_valid(uint32_t cfsr)
{
    return (cfsr & CFSR_MMARVALID) != 0U;
}

bool crash_bfar_valid(uint32_t cfsr)
{
    return (cfsr & CFSR_BFARVALID) != 0U;
}

ResetCause reset_cause_decode(uint32_t rcc_csr)
{
    if ((rcc_csr & CSR_IWDGRSTF) != 0U) {
        return RESET_CAUSE_INDEPENDENT_WDG;
    }
    if ((rcc_csr & CSR_WWDGRSTF) != 0U) {
        return RESET_CAUSE_WINDOW_WDG;
    }
    if ((rcc_csr & CSR_LPWRRSTF) != 0U) {
        return RESET_CAUSE_LOW_POWER;
    }
    if ((rcc_csr & CSR_SFTRSTF) != 0U) {
        return RESET_CAUSE_SOFTWARE;
    }
    if ((rcc_csr & CSR_PORRSTF) != 0U) {
        return RESET_CAUSE_POWER_ON;
    }
    if ((rcc_csr & CSR_BORRSTF) != 0U) {
        return RESET_CAUSE_BROWN_OUT;
    }
    if ((rcc_csr & CSR_PINRSTF) != 0U) {
        return RESET_CAUSE_PIN;
    }
    return RESET_CAUSE_UNKNOWN;
}

const char *reset_cause_name(ResetCause cause)
{
    switch (cause) {
        case RESET_CAUSE_POWER_ON:
            return "POWER_ON";
        case RESET_CAUSE_BROWN_OUT:
            return "BROWN_OUT";
        case RESET_CAUSE_PIN:
            return "PIN";
        case RESET_CAUSE_SOFTWARE:
            return "SOFTWARE";
        case RESET_CAUSE_INDEPENDENT_WDG:
            return "IWDG";
        case RESET_CAUSE_WINDOW_WDG:
            return "WWDG";
        case RESET_CAUSE_LOW_POWER:
            return "LOW_POWER";
        case RESET_CAUSE_UNKNOWN:
        case RESET_CAUSE_COUNT:
        default:
            return "UNKNOWN";
    }
}

WdgResetCause reset_cause_to_wdg(ResetCause cause)
{
    switch (cause) {
        case RESET_CAUSE_POWER_ON:
        case RESET_CAUSE_BROWN_OUT:
            return WDG_RESET_CAUSE_POWER_ON;
        case RESET_CAUSE_PIN:
            return WDG_RESET_CAUSE_PIN;
        case RESET_CAUSE_SOFTWARE:
            return WDG_RESET_CAUSE_SOFTWARE;
        case RESET_CAUSE_INDEPENDENT_WDG:
        case RESET_CAUSE_WINDOW_WDG:
            return WDG_RESET_CAUSE_WATCHDOG;
        case RESET_CAUSE_LOW_POWER:
        case RESET_CAUSE_UNKNOWN:
        case RESET_CAUSE_COUNT:
        default:
            return WDG_RESET_CAUSE_UNKNOWN;
    }
}

/* Report writer: appends into buf, always NUL-terminated, and keeps
 * counting past the end so the caller can detect truncation. No stdio --
 * newlib's snprintf drags in the FILE machinery and its syscall stubs, and
 * a hand-rolled hex writer is all the report needs. */
typedef struct {
    char  *buf;
    size_t cap;
    size_t len;
} ReportOut;

static void out_char(ReportOut *out, char c)
{
    if ((out->buf != NULL) && ((out->len + 1U) < out->cap)) {
        out->buf[out->len]      = c;
        out->buf[out->len + 1U] = '\0';
    }
    out->len++;
}

static void out_str(ReportOut *out, const char *str)
{
    for (size_t i = 0U; str[i] != '\0'; i++) {
        out_char(out, str[i]);
    }
}

/* At most max_len chars of a possibly unterminated fixed-size field. */
static void out_strn(ReportOut *out, const char *str, size_t max_len)
{
    for (size_t i = 0U; (i < max_len) && (str[i] != '\0'); i++) {
        out_char(out, str[i]);
    }
}

/* " <key>=0x%08X" */
static void out_hex(ReportOut *out, const char *key, uint32_t value)
{
    static const char digits[] = "0123456789ABCDEF";

    out_char(out, ' ');
    out_str(out, key);
    out_str(out, "=0x");
    for (unsigned int shift = 32U; shift > 0U; shift -= 4U) {
        out_char(out, digits[(value >> (shift - 4U)) & 0xFU]);
    }
}

size_t crash_format_report(char              *buf,
                           size_t             buf_len,
                           const char        *fw_version,
                           ResetCause         reset_cause,
                           uint32_t           rcc_csr,
                           const CrashRecord *rec)
{
    ReportOut out = {buf, buf_len, 0U};

    if ((buf != NULL) && (buf_len > 0U)) {
        buf[0] = '\0';
    }

    out_str(&out, "BOOT fw=");
    out_str(&out, (fw_version != NULL) ? fw_version : "?");
    out_str(&out, " reset=");
    out_str(&out, reset_cause_name(reset_cause));
    out_hex(&out, "csr", rcc_csr);
    out_str(&out, (rec != NULL) ? " crash=yes\n" : " crash=no\n");
    if (rec == NULL) {
        return out.len;
    }

    out_str(&out, "CRASH cause=");
    out_str(&out, crash_cause_name((CrashCause)rec->cause));
    out_str(&out, " task=");
    if (rec->task_name[0] != '\0') {
        out_strn(&out, rec->task_name, CRASH_TASK_NAME_LEN);
    } else {
        out_str(&out, "-");
    }
    out_str(&out, crash_exc_return_uses_psp(rec->exc_return) ? " stack=PSP\n" : " stack=MSP\n");

    out_str(&out, "CRASH");
    out_hex(&out, "pc", rec->pc);
    out_hex(&out, "lr", rec->lr);
    out_hex(&out, "xpsr", rec->xpsr);
    out_hex(&out, "sp", rec->sp);
    out_str(&out, "\nCRASH");
    out_hex(&out, "r0", rec->r0);
    out_hex(&out, "r1", rec->r1);
    out_hex(&out, "r2", rec->r2);
    out_hex(&out, "r3", rec->r3);
    out_hex(&out, "r12", rec->r12);
    out_str(&out, "\nCRASH");
    out_hex(&out, "cfsr", rec->cfsr);
    out_hex(&out, "hfsr", rec->hfsr);
    out_str(&out, " reason=");
    out_str(&out, crash_fault_reason(rec->cfsr, rec->hfsr));
    out_str(&out, "\n");
    if (crash_mmfar_valid(rec->cfsr)) {
        out_str(&out, "CRASH");
        out_hex(&out, "mmfar", rec->mmfar);
        out_str(&out, "\n");
    }
    if (crash_bfar_valid(rec->cfsr)) {
        out_str(&out, "CRASH");
        out_hex(&out, "bfar", rec->bfar);
        out_str(&out, "\n");
    }
    return out.len;
}
