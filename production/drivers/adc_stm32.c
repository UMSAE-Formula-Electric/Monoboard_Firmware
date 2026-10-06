/**
 * @file adc_stm32.c
 * @brief AdcIf implementation for the STM32F446's ADC1 (issue #58).
 *
 * Why registers and not HAL_ADC: the vendored HAL tree does not ship
 * stm32f4xx_hal_adc.{c,h} (or the LL ADC header) -- the current .ioc has
 * no ADC configured, so CubeMX never generated it, and HAL_ADC_MODULE_
 * ENABLED is off in stm32f4xx_hal_conf.h. Rather than hand-add vendor
 * files, ADC1, TIM8 and DMA2 Stream0 are programmed directly through the
 * CMSIS device registers (this is still the driver layer, the one layer
 * allowed to touch them). HAL is used only for GPIO, clocks and NVIC.
 * Once issue #29 adds the analog pins to the .ioc, this can move onto
 * HAL_ADC without changing the adc_if.h contract.
 *
 * Acquisition chain -- nothing here ever triggers a conversion and waits:
 *
 *   TIM8 update @ ADC_STM32_SCAN_RATE_HZ --TRGO--> ADC1 regular group,
 *   scan mode, one rank per entry of start_scan()'s channel list
 *   --DMA2 Stream0 Ch0, circular, two scans deep--> dma_buf[2][n]
 *
 * Timer-triggered, not free-running, so the scan interval is set by a
 * hardware timer and every channel is sampled at a fixed, repeatable
 * offset from the trigger: rank k starts sum(SMP_i + 12) ADCCLK cycles
 * (i < k) after it. Two channels in adjacent ranks (put the two APPS
 * channels next to each other) are therefore always exactly one
 * conversion apart -- (28 + 12) cycles = 5.0 us at the current 8 MHz
 * ADCCLK (16 MHz HSI / 2), 1.8 us at 22.5 MHz (90 MHz PCLK2 / 4).
 *
 * Buffer coherency: the DMA buffer holds two complete scans. The DMA
 * half-transfer interrupt means scan half 0 is complete and the DMA has
 * moved on to half 1 (and vice versa for transfer-complete), so the ISR
 * only ever copies the half the DMA is NOT writing. The copy target,
 * latest[], is one AdcSample per channel; a 16-bit aligned load is a
 * single LDRH on the Cortex-M4, so read_latest() can never see a torn
 * value. (A coherent snapshot of several channels from the *same* scan
 * would need a new, appended contract entry -- read_latest() is
 * per-channel by design.)
 *
 * Out-of-range detection: the F446 analog watchdog has one LTR/HTR
 * threshold pair shared by every guarded channel, so it cannot express
 * adc_if.h's per-channel limits (a pedal and an NTC have different valid
 * bands). Instead the same DMA ISR that publishes each completed scan
 * compares every conversion against that channel's limits and fires
 * on_watchdog once per out-of-range conversion, as the contract requires.
 * Detection latency is bounded by one scan period plus ISR latency --
 * the same bound the hardware watchdog interrupt would give at this scan
 * rate, without the shared-threshold restriction.
 *
 * Scan rate: 1 kHz, matching the pedal/control task rate, so every 1 ms
 * control step reads a sample no older than one scan period.
 *
 * Per-channel sample time -- DS10693 (STM32F446 datasheet), ADC
 * characteristics, Equation 1, maximum source impedance for 1/4 LSB
 * error at N = 12 bits:
 *
 *   R_AIN_max = (k - 0.5) / (f_ADC * C_ADC * ln(2^(N+2))) - R_ADC
 *
 * with the datasheet worst cases C_ADC = 7 pF, R_ADC = 6 kOhm, and f_ADC
 * at its 36 MHz ceiling (the prescaler below never exceeds it; a slower
 * ADCCLK only adds margin). ln(2^14) = 9.704, so the denominator is
 * 36e6 * 7e-12 * 9.704 = 2.445e-3 per cycle:
 *
 *   k = 15 cycles:  14.5 / 2.445e-3 - 6000 = -0.07 kOhm  (unusable at 36 MHz)
 *   k = 28 cycles:  27.5 / 2.445e-3 - 6000 =  5.2 kOhm  -> LOW_Z class
 *   k = 56 cycles:  55.5 / 2.445e-3 - 6000 = 16.7 kOhm
 *   k = 84 cycles:  83.5 / 2.445e-3 - 6000 = 28.1 kOhm  -> HIGH_Z class
 *
 * LOW_Z (28 cycles): pedal position sensors -- a sensor output of at
 * most ~1 kOhm plus a ~1 kOhm series filter resistor, 2.6x margin.
 * HIGH_Z (84 cycles): NTC dividers -- Thevenin resistance tends to the
 * pull-up as the thermistor opens; with a 10 kOhm pull-up that is
 * <= 10 kOhm, 2.8x margin. Unknown sources default to HIGH_Z. Worst-case
 * scan: 16 ranks * (84 + 12) cycles / 8 MHz = 192 us, well inside the
 * 1 ms scan period. (Equation 1 ignores any external anti-alias
 * capacitor; one much larger than C_ADC only makes these numbers safer.)
 *
 * Reference: the driver returns raw counts against VREF+. Scaling --
 * including any ratiometric correction if the pedal supply and VREF+ can
 * move independently, or a VREFINT (ADC1_IN17) supply compensation -- is
 * service work; which is needed depends on the schematic (issue #29).
 *
 * Faults: an ADC overrun (OVR) or DMA transfer error is counted and the
 * scan is re-armed from rank 1 in the ISR, so a lost scan can never shift
 * every later sample into the wrong channel's slot.
 */
#include "adc_stm32.h"

#include <stdbool.h>
#include <stddef.h>

#include "stm32f4xx_hal.h"

#define ADC_STM32_SCAN_RATE_HZ   1000U     /* one full scan per 1 ms control step */
#define ADC_STM32_TIMER_TICK_HZ  1000000U  /* TIM8 counter clock after prescaling */
#define ADC_STM32_MAX_RANKS      16U       /* ADC1 regular sequence length (SQR1.L) */
#define ADC_STM32_ADCCLK_MAX_HZ  36000000U /* DS10693 f_ADC max, VDDA 2.4-3.6 V */
#define ADC_STM32_ADCPRE_CODES   4U        /* ADC_CCR.ADCPRE: /2, /4, /6, /8 */
#define ADC_STM32_SMP_BITS       3U        /* width of one SMPx field */
#define ADC_STM32_SQ_BITS        5U        /* width of one SQx field */
#define ADC_STM32_RANKS_PER_SQR  6U        /* SQR3: ranks 1-6, SQR2: 7-12, SQR1: 13-16 */
#define ADC_STM32_SMPR2_CHANNELS 10U       /* SMPR2 holds inputs 0-9, SMPR1 10-18 */
#define ADC_STM32_EXTSEL_TIM8    14U       /* ADC_CR2.EXTSEL = 1110: TIM8 TRGO */
#define ADC_STM32_DMA_CHANNEL    0U        /* DMA2 Stream0 Channel0 = ADC1 (fixed map) */
#define ADC_STM32_IRQ_PRIORITY   5U        /* >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY */

/* ADC_SMPRx.SMPn codes -- see the sample-time calculation above. */
#define ADC_STM32_SMP_LOW_Z  2U /* 28 cycles: <= 5.2 kOhm source (pedal sensors) */
#define ADC_STM32_SMP_HIGH_Z 4U /* 84 cycles: <= 28.1 kOhm source (NTC dividers) */

#define ADC_STM32_DMA_STREAM0_FLAGS \
    (DMA_LIFCR_CFEIF0 | DMA_LIFCR_CDMEIF0 | DMA_LIFCR_CTEIF0 | DMA_LIFCR_CHTIF0 | DMA_LIFCR_CTCIF0)

typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pad;
    uint8_t       input; /* ADC1_INx */
    uint8_t       smp;   /* ADC_STM32_SMP_* */
} ChannelMap;

/* AdcChannel -> silicon. PA0 / ADC1_IN0 is this board's test input (not
 * yet in the .ioc -- issue #29); change here, and only here, if it moves.
 * Source impedance unknown, so the conservative HIGH_Z sample time. */
static const ChannelMap channel_map[ADC_CH_COUNT] = {
    [ADC_CH_TEST] = {GPIOA, GPIO_PIN_0, 0U, ADC_STM32_SMP_HIGH_Z},
};

_Static_assert(ADC_CH_COUNT == 1,
               "AdcChannel catalogue changed -- add/remove the matching channel_map[] row above");

/* Two complete scans, written by DMA2 Stream0 while the CPU reads the other half. */
static volatile AdcSample dma_buf[2U * ADC_STM32_MAX_RANKS];

static AdcChannel scan_order[ADC_STM32_MAX_RANKS];
static uint32_t   scan_length;
static bool       in_scan[ADC_CH_COUNT];
static bool       scanning;

static volatile AdcSample latest[ADC_CH_COUNT];
static volatile bool      has_sample[ADC_CH_COUNT];

static AdcWatchdogLimits watchdog_limits[ADC_CH_COUNT];
static AdcWatchdogCb     watchdog_cb;
static void             *watchdog_ctx;

static volatile uint32_t overrun_count;
static volatile uint32_t dma_error_count;

static bool channel_valid(AdcChannel channel)
{
    return (unsigned int)channel < (unsigned int)ADC_CH_COUNT;
}

static bool watchdog_tripped(const AdcWatchdogLimits *limits, AdcSample sample)
{
    return (limits->low != limits->high) && ((sample < limits->low) || (sample > limits->high));
}

/**
 * @brief Pick the smallest ADC_CCR.ADCPRE divider (/2, /4, /6, /8) that keeps
 * ADCCLK at or below the datasheet's 36 MHz ceiling.
 * @param pclk2_hz  APB2 clock feeding the ADC prescaler
 * @return ADCPRE field code (0..3)
 */
static uint32_t adc_prescaler_code(uint32_t pclk2_hz)
{
    uint32_t code;

    for (code = 0U; code < ADC_STM32_ADCPRE_CODES; code++) {
        if ((pclk2_hz / (2U * (code + 1U))) <= ADC_STM32_ADCCLK_MAX_HZ) {
            return code;
        }
    }
    return ADC_STM32_ADCPRE_CODES - 1U;
}

/**
 * @brief TIM8's kernel clock: PCLK2 if the APB2 prescaler is 1, otherwise
 * 2 x PCLK2 (the F4's timer clock doubler).
 */
static uint32_t tim8_clock_hz(void)
{
    uint32_t pclk2_hz = HAL_RCC_GetPCLK2Freq();

    return ((RCC->CFGR & RCC_CFGR_PPRE2) == RCC_CFGR_PPRE2_DIV1) ? pclk2_hz : (2U * pclk2_hz);
}

/**
 * @brief Disable DMA2 Stream0 and wait for the hardware to confirm. EN
 * drops once the in-flight halfword finishes, i.e. within a few bus cycles.
 */
static void dma_disable(void)
{
    DMA2_Stream0->CR &= ~DMA_SxCR_EN;
    while ((DMA2_Stream0->CR & DMA_SxCR_EN) != 0U) {
    }
    DMA2->LIFCR = ADC_STM32_DMA_STREAM0_FLAGS;
}

/**
 * @brief (Re)arm DMA2 Stream0 for a circular, two-scan-deep transfer from
 * ADC1->DR into dma_buf, with half/full/error interrupts. Always starts
 * writing at slot 0, so it must be paired with an ADC sequencer reset.
 */
static void dma_arm(void)
{
    dma_disable();

    DMA2_Stream0->PAR  = (uint32_t)(uintptr_t)&ADC1->DR;
    DMA2_Stream0->M0AR = (uint32_t)(uintptr_t)dma_buf;
    DMA2_Stream0->NDTR = 2U * scan_length;
    DMA2_Stream0->FCR  = 0U; /* direct mode: PSIZE == MSIZE, no FIFO */
    DMA2_Stream0->CR   = (ADC_STM32_DMA_CHANNEL << DMA_SxCR_CHSEL_Pos) | DMA_SxCR_PL_1
                       | DMA_SxCR_MSIZE_0 | DMA_SxCR_PSIZE_0 | DMA_SxCR_MINC | DMA_SxCR_CIRC
                       | DMA_SxCR_HTIE | DMA_SxCR_TCIE | DMA_SxCR_TEIE;
    DMA2_Stream0->CR |= DMA_SxCR_EN;
}

/**
 * @brief Recover from an overrun or DMA error (ISR context): power-cycle
 * ADON so the sequencer restarts at rank 1, re-arm the DMA at slot 0, and
 * push the next trigger a full period out so the ADC has its stabilisation
 * time (t_STAB, a few us) before converting again.
 */
static void restart_scan_from_isr(void)
{
    ADC1->CR2 &= ~(ADC_CR2_ADON | ADC_CR2_DMA);
    dma_arm();
    ADC1->SR  = 0U;
    TIM8->CNT = 0U;
    ADC1->CR2 |= ADC_CR2_DMA;
    ADC1->CR2 |= ADC_CR2_ADON;
}

/**
 * @brief Copy one completed scan out of the DMA buffer into latest[] and
 * fire on_watchdog for every out-of-range conversion (ISR context).
 * @param half  0 or 1: which of the two scans in dma_buf just completed
 */
static void publish_scan_from_isr(uint32_t half)
{
    uint32_t slot;

    for (slot = 0U; slot < scan_length; slot++) {
        AdcChannel channel = scan_order[slot];
        AdcSample  sample  = dma_buf[(half * scan_length) + slot];

        latest[channel]     = sample;
        has_sample[channel] = true;

        if ((watchdog_cb != NULL) && watchdog_tripped(&watchdog_limits[channel], sample)) {
            watchdog_cb(watchdog_ctx, channel, sample);
        }
    }
}

/**
 * @brief DMA2 Stream0 interrupt vector (ADC1 data): half-transfer means scan
 * 0 of dma_buf is complete, transfer-complete means scan 1 is. A transfer
 * error has already disabled the stream in hardware, so the scan is
 * restarted rather than left dead.
 */
void DMA2_Stream0_IRQHandler(void)
{
    uint32_t status = DMA2->LISR & (DMA_LISR_TEIF0 | DMA_LISR_HTIF0 | DMA_LISR_TCIF0);

    DMA2->LIFCR = status; /* LIFCR clear bits share LISR's positions */

    if ((status & DMA_LISR_TEIF0) != 0U) {
        dma_error_count++;
        restart_scan_from_isr();
        return;
    }
    if ((status & DMA_LISR_HTIF0) != 0U) {
        publish_scan_from_isr(0U);
    }
    if ((status & DMA_LISR_TCIF0) != 0U) {
        publish_scan_from_isr(1U);
    }
}

/**
 * @brief ADC1/2/3 shared interrupt vector: only OVR is enabled. An overrun
 * stops ADC1's DMA requests, so recover by restarting the scan.
 */
void ADC_IRQHandler(void)
{
    if ((ADC1->SR & ADC_SR_OVR) != 0U) {
        overrun_count++;
        restart_scan_from_isr();
    }
}

/**
 * @brief Enable every clock the scan needs and put each scanned channel's
 * pad into analog mode. ADC1 inputs live on ports A, B and C only.
 */
static void configure_gpio_and_clocks(const AdcChannel *chans, size_t channel_count)
{
    GPIO_InitTypeDef gpio_init = {0};
    size_t           i;

    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_TIM8_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    gpio_init.Mode = GPIO_MODE_ANALOG;
    gpio_init.Pull = GPIO_NOPULL;
    for (i = 0U; i < channel_count; i++) {
        gpio_init.Pin = channel_map[chans[i]].pad;
        HAL_GPIO_Init(channel_map[chans[i]].port, &gpio_init);
    }
}

/**
 * @brief Program TIM8 to emit TRGO on every update at ADC_STM32_SCAN_RATE_HZ.
 * The counter is left stopped; start_scan() sets CEN last.
 */
static void configure_trigger_timer(void)
{
    TIM8->CR1 = 0U;
    TIM8->PSC = (tim8_clock_hz() / ADC_STM32_TIMER_TICK_HZ) - 1U;
    TIM8->ARR = (ADC_STM32_TIMER_TICK_HZ / ADC_STM32_SCAN_RATE_HZ) - 1U;
    TIM8->CNT = 0U;
    TIM8->CR2 = TIM_CR2_MMS_1; /* MMS = 010: update event -> TRGO */
    TIM8->EGR = TIM_EGR_UG;    /* latch PSC/ARR now (ADC is still off) */
    TIM8->SR  = 0U;
}

/**
 * @brief Program ADC1's regular sequence from the caller's channel list:
 * scan mode, 12-bit right-aligned, rising-edge TIM8 TRGO trigger, DMA with
 * continuous requests (DDS), per-input sample times from channel_map[].
 */
static void configure_adc(const AdcChannel *chans, size_t channel_count)
{
    uint32_t sqr[3]  = {0U, 0U, 0U}; /* SQR3, SQR2, SQR1 */
    uint32_t smpr[2] = {0U, 0U};     /* SMPR2, SMPR1 */
    uint32_t rank;

    ADC1->CR2 = 0U; /* ADON off while reconfiguring */

    ADC123_COMMON->CCR = (ADC123_COMMON->CCR & ~ADC_CCR_ADCPRE)
                         | (adc_prescaler_code(HAL_RCC_GetPCLK2Freq()) << ADC_CCR_ADCPRE_Pos);

    for (rank = 0U; rank < (uint32_t)channel_count; rank++) {
        const ChannelMap *map   = &channel_map[chans[rank]];
        uint32_t          input = map->input;
        uint32_t          bank  = (input < ADC_STM32_SMPR2_CHANNELS) ? 0U : 1U;
        uint32_t          field = (bank == 0U) ? input : (input - ADC_STM32_SMPR2_CHANNELS);

        smpr[bank] |= (uint32_t)map->smp << (field * ADC_STM32_SMP_BITS);
        sqr[rank / ADC_STM32_RANKS_PER_SQR] |= input << ((rank % ADC_STM32_RANKS_PER_SQR)
                                                         * ADC_STM32_SQ_BITS);
    }
    sqr[2] |= ((uint32_t)channel_count - 1U) << ADC_SQR1_L_Pos;

    ADC1->CR1   = ADC_CR1_SCAN | ADC_CR1_OVRIE; /* RES = 00: 12-bit */
    ADC1->SMPR2 = smpr[0];
    ADC1->SMPR1 = smpr[1];
    ADC1->SQR3  = sqr[0];
    ADC1->SQR2  = sqr[1];
    ADC1->SQR1  = sqr[2];
    ADC1->CR2   = ADC_CR2_DMA | ADC_CR2_DDS | ADC_CR2_EXTEN_0
                | (ADC_STM32_EXTSEL_TIM8 << ADC_CR2_EXTSEL_Pos);
    ADC1->SR = 0U;
}

/**
 * @brief AdcIf::start_scan -- arm the timer-triggered DMA scan of @p chans,
 * in the given order (rank 1 first). See adc_if.h for the full contract.
 * @return #IF_OK once armed, #IF_BUSY if already scanning, #IF_HW_FAULT on
 *         bad arguments, including more than ADC_STM32_MAX_RANKS channels
 */
static IfStatus stm32_start_scan(const AdcChannel       *chans,
                                 size_t                  channel_count,
                                 const AdcWatchdogLimits limits[ADC_CH_COUNT],
                                 AdcWatchdogCb           on_watchdog,
                                 void                   *ctx)
{
    size_t i;

    if (scanning) {
        return IF_BUSY;
    }
    if ((chans == NULL) || (channel_count == 0U) || (channel_count > ADC_STM32_MAX_RANKS)) {
        return IF_HW_FAULT;
    }
    for (i = 0U; i < channel_count; i++) {
        if (!channel_valid(chans[i])) {
            return IF_HW_FAULT;
        }
    }

    for (i = 0U; i < (size_t)ADC_CH_COUNT; i++) {
        in_scan[i]         = false;
        has_sample[i]      = false;
        watchdog_limits[i] = (limits != NULL) ? limits[i] : (AdcWatchdogLimits){0, 0};
    }
    for (i = 0U; i < channel_count; i++) {
        scan_order[i]     = chans[i];
        in_scan[chans[i]] = true;
    }
    scan_length  = (uint32_t)channel_count;
    watchdog_cb  = on_watchdog;
    watchdog_ctx = ctx;

    configure_gpio_and_clocks(chans, channel_count);
    configure_trigger_timer();
    configure_adc(chans, channel_count);
    dma_arm();

    HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, ADC_STM32_IRQ_PRIORITY, 0U);
    HAL_NVIC_ClearPendingIRQ(DMA2_Stream0_IRQn);
    HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);
    HAL_NVIC_SetPriority(ADC_IRQn, ADC_STM32_IRQ_PRIORITY, 0U);
    HAL_NVIC_ClearPendingIRQ(ADC_IRQn);
    HAL_NVIC_EnableIRQ(ADC_IRQn);

    ADC1->CR2 |= ADC_CR2_ADON; /* first trigger is a full period away: >> t_STAB */
    TIM8->CR1 |= TIM_CR1_CEN;

    scanning = true;
    return IF_OK;
}

/**
 * @brief AdcIf::stop_scan -- stop the trigger, then the ADC and DMA.
 * latest[] is left intact as last known good.
 * @return #IF_OK, or #IF_HW_FAULT if no scan was running
 */
static IfStatus stm32_stop_scan(void)
{
    if (!scanning) {
        return IF_HW_FAULT;
    }

    TIM8->CR1 &= ~TIM_CR1_CEN;
    HAL_NVIC_DisableIRQ(DMA2_Stream0_IRQn);
    HAL_NVIC_DisableIRQ(ADC_IRQn);
    ADC1->CR2 &= ~(ADC_CR2_ADON | ADC_CR2_DMA);
    dma_disable();

    scanning = false;
    return IF_OK;
}

/**
 * @brief AdcIf::read_latest -- return the cached result from the most recent
 * completed scan. Never touches the peripheral and never blocks.
 * @return #IF_OK, #IF_TIMEOUT if no scan has completed since start_scan(),
 *         or #IF_HW_FAULT on bad arguments / a channel outside the scan
 */
static IfStatus stm32_read_latest(AdcChannel channel, AdcSample *out_value)
{
    if (!channel_valid(channel) || (out_value == NULL) || !in_scan[channel]) {
        return IF_HW_FAULT;
    }
    if (!has_sample[channel]) {
        return IF_TIMEOUT;
    }
    *out_value = latest[channel];
    return IF_OK;
}

const AdcIf adc_stm32 = {
    .start_scan  = stm32_start_scan,
    .stop_scan   = stm32_stop_scan,
    .read_latest = stm32_read_latest,
};

uint32_t adc_stm32_overrun_count(void)
{
    return overrun_count;
}

uint32_t adc_stm32_dma_error_count(void)
{
    return dma_error_count;
}
