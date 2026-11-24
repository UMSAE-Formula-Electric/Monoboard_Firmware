/**
 * @file time_stm32.c
 * @brief STM32 monotonic time implementation backed by the FreeRTOS tick.
 */
#include "time_stm32.h"
#include "time_if.h"
#include "timing_if.h"

#include "FreeRTOS.h"
#include "stm32f4xx.h"
#include "task.h"

void time_stm32_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    DWT->CYCCNT = 0U;
}

bool time_stm32_cycle_counter_running(void)
{
    const uint32_t first = DWT->CYCCNT;
    uint32_t       spin;

    /* A running counter moves within a few reads; bounded so a dead one
     * cannot hang boot. */
    for (spin = 0U; spin < 16U; spin++) {
        if (DWT->CYCCNT != first) {
            return true;
        }
    }
    return false;
}

uint32_t dwt_cycles(void)
{
    return DWT->CYCCNT;
}

uint32_t dwt_cycles_per_us(void)
{
    const uint32_t per_us = SystemCoreClock / 1000000U;

    return (per_us != 0U) ? per_us : 1U;
}

uint32_t dwt_cycles_to_us(uint32_t cycles)
{
    return cycles / dwt_cycles_per_us();
}

static uint32_t ticks_to_ms(TickType_t ticks)
{
    return (uint32_t)(((uint64_t)ticks * 1000U) / configTICK_RATE_HZ);
}

static uint32_t cycles_to_us(uint32_t cycles)
{
    return (uint32_t)(((uint64_t)cycles * 1000000U) / SystemCoreClock);
}

uint32_t now_ms(void)
{
    return ticks_to_ms(xTaskGetTickCount());
}

uint32_t now_ms_isr(void)
{
    return ticks_to_ms(xTaskGetTickCountFromISR());
}

uint32_t now_us(void)
{
    return cycles_to_us(DWT->CYCCNT);
}

uint32_t now_us_isr(void)
{
    return cycles_to_us(DWT->CYCCNT);
}

bool deadline_passed(uint32_t start, uint32_t timeout_ms)
{
    return deadline_passed_at(now_ms(), start, timeout_ms);
}

bool deadline_passed_at(uint32_t now, uint32_t start, uint32_t timeout_ms)
{
    return (uint32_t)(now - start) > timeout_ms;
}
