/**
 * @file wdg_stm32.c
 * @brief WdgIf implementation for the STM32F446 independent watchdog (IWDG).
 *
 * Timeout: the IWDG counts LSI ticks, and the F446 datasheet only promises
 * 17 kHz <= f_LSI <= 47 kHz (32 kHz typical). init() sizes the prescaler and
 * reload from the *slowest* LSI, so a missed kick always resets the MCU within
 * WdgConfig.timeout_ms. The flip side is that a fast part bites after as
 * little as timeout_ms * 17/47 -- wdg_stm32_min_period_ms() reports that
 * figure, and it, not timeout_ms, is the deadline the kick policy must meet.
 * The real period still has to be measured on the bench (issue #18).
 *
 * Debug freeze: DBGMCU_APB1_FZ.DBG_IWDG_STOP decides whether the countdown
 * pauses while a debugger has the core halted. The build sets
 * WDG_STM32_FREEZE_ON_DEBUG_HALT explicitly (CMakeLists.txt: 1 for Debug,
 * 0 otherwise) and init() writes the bit both ways: frozen on the bench, so
 * a breakpoint does not reset the board; running on track. Clearing it
 * matters too -- DBGMCU survives every reset except power-on, so a board last
 * flashed with a debug image would otherwise carry the freeze into release.
 *
 * Once started the IWDG cannot be stopped by anything short of a reset, so
 * init() belongs in main() before the scheduler starts (see wdg_if.h).
 * Nothing here is ISR-driven: no NVIC configuration, no FreeRTOS calls.
 */
#include "wdg_stm32.h"

#include <stdbool.h>
#include <stddef.h>

#include "stm32f4xx_hal.h"

#ifndef WDG_STM32_FREEZE_ON_DEBUG_HALT
#    error "WDG_STM32_FREEZE_ON_DEBUG_HALT must be set by the build (0 or 1) -- see CMakeLists.txt"
#endif

/* STM32F446 datasheet, LSI oscillator characteristics. */
#define LSI_MIN_HZ 17000U
#define LSI_MAX_HZ 47000U

/* The down-counter is 12 bits: a reload of RLR gives RLR + 1 ticks. */
#define IWDG_MAX_TICKS (IWDG_RLR_RL + 1U)

typedef struct {
    uint32_t divider;
    uint32_t hal_prescaler;
} PrescalerOption;

/* Smallest divider first: the first one that fits gives the finest reload
 * resolution. */
static const PrescalerOption prescalers[] = {
    {4U, IWDG_PRESCALER_4},     {8U, IWDG_PRESCALER_8},   {16U, IWDG_PRESCALER_16},
    {32U, IWDG_PRESCALER_32},   {64U, IWDG_PRESCALER_64}, {128U, IWDG_PRESCALER_128},
    {256U, IWDG_PRESCALER_256},
};

#define PRESCALER_COUNT (sizeof(prescalers) / sizeof(prescalers[0]))

/* Every reset flag in RCC->CSR, cleared together by RMVF. */
#define CSR_RESET_FLAGS                                                                       \
    (RCC_CSR_BORRSTF | RCC_CSR_PINRSTF | RCC_CSR_PORRSTF | RCC_CSR_SFTRSTF | RCC_CSR_IWDGRSTF \
     | RCC_CSR_WWDGRSTF | RCC_CSR_LPWRRSTF)

static IWDG_HandleTypeDef hiwdg;
static bool               initialized;
static uint32_t           max_period_ms;
static uint32_t           min_period_ms;
static uint32_t           raw_reset_flags;

static uint32_t period_ms(uint32_t ticks, uint32_t divider, uint32_t lsi_hz)
{
    return (uint32_t)(((uint64_t)ticks * divider * 1000U) / lsi_hz);
}

static IfStatus stm32_init(const WdgConfig *config)
{
    size_t   i;
    uint32_t ticks = 0U;

    if ((config == NULL) || (config->timeout_ms == 0U)) {
        return IF_HW_FAULT;
    }

    /* Round down: at the slowest LSI the period never exceeds timeout_ms. */
    for (i = 0U; i < PRESCALER_COUNT; i++) {
        ticks = (uint32_t)(((uint64_t)config->timeout_ms * LSI_MIN_HZ)
                           / (1000U * (uint64_t)prescalers[i].divider));
        if (ticks <= IWDG_MAX_TICKS) {
            break;
        }
    }
    if ((i == PRESCALER_COUNT) || (ticks == 0U)) {
        return IF_HW_FAULT; /* longer than ~61 s, or shorter than one tick */
    }

#if WDG_STM32_FREEZE_ON_DEBUG_HALT
    __HAL_DBGMCU_FREEZE_IWDG();
#else
    __HAL_DBGMCU_UNFREEZE_IWDG();
#endif

    hiwdg.Instance       = IWDG;
    hiwdg.Init.Prescaler = prescalers[i].hal_prescaler;
    hiwdg.Init.Reload    = ticks - 1U;
    /* Starts the countdown before it programs PR/RLR: from here on the
     * watchdog is live whatever this call returns. */
    if (HAL_IWDG_Init(&hiwdg) != HAL_OK) {
        return IF_HW_FAULT;
    }

    max_period_ms = period_ms(ticks, prescalers[i].divider, LSI_MIN_HZ);
    min_period_ms = period_ms(ticks, prescalers[i].divider, LSI_MAX_HZ);
    initialized   = true;
    return IF_OK;
}

static void stm32_kick(void)
{
    if (initialized) {
        (void)HAL_IWDG_Refresh(&hiwdg); /* writes the reload key; always HAL_OK */
    }
}

static WdgResetCause decode_reset_flags(uint32_t flags)
{
    /* Flags accumulate until cleared, and every internal reset also pulses
     * NRST (so PINRSTF rides along) -- check the most specific cause first. */
    if ((flags & (RCC_CSR_IWDGRSTF | RCC_CSR_WWDGRSTF)) != 0U) {
        return WDG_RESET_CAUSE_WATCHDOG;
    }
    if ((flags & RCC_CSR_SFTRSTF) != 0U) {
        return WDG_RESET_CAUSE_SOFTWARE;
    }
    if ((flags & (RCC_CSR_PORRSTF | RCC_CSR_BORRSTF)) != 0U) {
        return WDG_RESET_CAUSE_POWER_ON;
    }
    if ((flags & RCC_CSR_LPWRRSTF) != 0U) {
        return WDG_RESET_CAUSE_UNKNOWN; /* low-power reset: no enumerator for it */
    }
    if ((flags & RCC_CSR_PINRSTF) != 0U) {
        return WDG_RESET_CAUSE_PIN;
    }
    return WDG_RESET_CAUSE_UNKNOWN;
}

static WdgResetCause stm32_read_and_clear_reset_cause(void)
{
    uint32_t flags = RCC->CSR & CSR_RESET_FLAGS;

    __HAL_RCC_CLEAR_RESET_FLAGS();
    raw_reset_flags = flags;
    return decode_reset_flags(flags);
}

const WdgIf wdg_stm32 = {
    .init                       = stm32_init,
    .kick                       = stm32_kick,
    .read_and_clear_reset_cause = stm32_read_and_clear_reset_cause,
};

uint32_t wdg_stm32_max_period_ms(void)
{
    return max_period_ms;
}

uint32_t wdg_stm32_min_period_ms(void)
{
    return min_period_ms;
}

uint32_t wdg_stm32_raw_reset_flags(void)
{
    return raw_reset_flags;
}
