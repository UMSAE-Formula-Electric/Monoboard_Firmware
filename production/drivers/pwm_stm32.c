/**
 * @file pwm_stm32.c
 * @brief pwm_if implementation for ST HAL's general-purpose timers.
 *
 * Wiring (provisional until issue #29, "Add pin definitions to IOC", puts
 * the timer pads in vendor.ioc -- change here, and only here, if they
 * move):
 *   - PWM_CH_TEST     -> TIM3_CH1, PA6, AF2 (output, safe level low)
 *   - PWM_CAP_CH_TEST -> TIM4_CH1, PB6, AF2 (input capture, pull-down)
 *
 * Output policy
 * -------------
 * Every logical output channel owns its timer outright (init() rejects a
 * second channel on an already-claimed timer), so per-channel frequency is
 * a real per-channel choice: a fan and a buzzer do not share a carrier.
 * For a requested frequency the driver picks the *smallest* prescaler that
 * lets the period fit in ARR <= 0xFFFE, which maximises duty resolution;
 * ARR tops out at 0xFFFE rather than 0xFFFF so that CCR = ARR + 1 (exactly
 * 100%) still fits the 16-bit compare register. A period of fewer than
 * PWM_STM32_MIN_COUNTS timer ticks is rejected, so every 1% duty step is a
 * distinct compare value. pwm_stm32_actual_frequency_hz() reports what the
 * rounding actually produced -- the value to check on the scope.
 *
 * Duty endpoints are exact: PWM mode 1 holds the output low for CCR = 0
 * and high for CCR > ARR, so 0% and 100% are DC levels, not 1/65535 off.
 *
 * Glitch-free updates: ARR (ARPE) and CCRx (OCxPE) are preloaded and PSC is
 * always buffered, so new values only reach the counter at the next update
 * event -- a mid-period write cannot shorten the period in flight into a
 * runt pulse. set_frequency_hz() writes PSC, ARR and CCR under UDIS so all
 * three land on the same update event, never a mix of old and new.
 *
 * Safe state: each output has a safe level (OutputMap::safe_high), e.g.
 * full duty for cooling, off for the buzzer. init() drives the pad to that
 * level as a plain GPIO output *first*, then configures and starts the
 * timer, and only hands the pad to the timer (alternate function) once the
 * compare value for the requested duty is already loaded. A failure at any
 * step leaves the pad parked at the safe level. pwm_stm32_enter_safe_state()
 * re-parks every output for the fault path. Between power-on and init()
 * the pad is a floating input (reset state), so the board must also hold
 * each load at its safe level with a resistor -- firmware cannot act before
 * it runs.
 *
 * Input capture
 * -------------
 * PWM-input mode on one channel pair: TI1 rising edge -> IC1 (period) and
 * resets the counter via slave reset mode; TI1 falling edge -> IC2 (high
 * time). The hardware latches both values every period on its own; the
 * CC1 ISR only copies the latched pair into one 32-bit word (atomic for the
 * reader, no lock). No FreeRTOS call is made from the ISR, and nothing is
 * lost if a task runs late -- reads always see the latest complete period.
 *
 * Why not DMA (which the issue suggested): the latching is already done in
 * hardware, so DMA would only move the copy, and the overflow guard below
 * needs to see each capture anyway to discard the aliased one. A single
 * short ISR at a low priority is simpler and costs one interrupt per input
 * period (tens of Hz for the IMD).
 *
 * Counter overflow is explicit: URS is set so slave resets do not raise
 * the update flag, which therefore means exactly "the counter wrapped with
 * no rising edge" -- i.e. 65536 ticks of silence. That clears the cached
 * sample (reads go to IF_TIMEOUT, distinct from any measured duty) and
 * marks the next capture as aliased (its period is > 65535 ticks, wrapped)
 * so it is discarded rather than reported as a plausible fast signal. The
 * first capture after start_capture() is discarded for the same reason.
 * At the 100 kHz capture tick used below that silence window is 655 ms,
 * and the measurable range is ~1.5 Hz to ~50 kHz.
 */
#include "pwm_stm32.h"

#include <stdbool.h>
#include <stddef.h>

#include "stm32f4xx_hal.h"

#define PWM_STM32_MAX_COUNTS     0xFFFFU  /* ARR + 1 upper bound; keeps CCR = ARR + 1 in 16 bits */
#define PWM_STM32_MIN_COUNTS     100U     /* below this, adjacent 1% steps collapse */
#define PWM_STM32_MAX_PRESCALE   0x10000U /* PSC + 1 upper bound (16-bit PSC) */
#define PWM_STM32_CAPTURE_FILTER 0x3U     /* ICxF: fCK_INT, N=8 -- ~0.5 us at 16 MHz */
#define PWM_STM32_IRQ_PRIORITY   5U       /* >= configMAX_SYSCALL_INTERRUPT_PRIORITY */

typedef struct {
    TIM_TypeDef  *timer;
    uint32_t      tim_channel; /* TIM_CHANNEL_x */
    GPIO_TypeDef *port;
    uint16_t      pad;
    uint8_t       alternate;
    bool          safe_high; /* safe level: true = 100% (e.g. cooling), false = 0% */
} OutputMap;

typedef struct {
    TIM_TypeDef  *timer; /* signal on TIx_CH1; CH1 + CH2 both used */
    IRQn_Type     irq;
    GPIO_TypeDef *port;
    uint16_t      pad;
    uint8_t       alternate;
    uint32_t      tick_hz; /* counter rate; sets resolution and silence window */
} CaptureMap;

typedef struct {
    TIM_HandleTypeDef htim;
    bool              configured; /* accepting set_*(); cleared by the safe state */
    bool              started;    /* timer running (HAL channel state BUSY) */
    uint8_t           duty_pct;
    uint32_t          counts; /* ARR + 1 */
    uint32_t          actual_frequency_hz;
} OutputState;

typedef struct {
    TIM_HandleTypeDef htim;
    bool              armed; /* start_capture() succeeded at least once */
    volatile bool     capturing;
    volatile bool     primed;  /* a non-aliased period start has been seen */
    volatile uint32_t sample;  /* high_ticks << 16 | period_ticks; 0 = no sample */
    uint32_t          tick_hz; /* actual counter rate after PSC rounding */
} CaptureState;

/* PwmChannel -> timer/pad. Adding a channel: add the enumerator to
 * pwm_if.h, add the row here (its own timer), record the load's frequency
 * and why in this table's comment, and bump the _Static_assert.
 *   PWM_CH_TEST: bring-up channel, no load; frequency is caller's choice. */
static const OutputMap output_map[PWM_CH_COUNT] = {
    [PWM_CH_TEST] = {TIM3, TIM_CHANNEL_1, GPIOA, GPIO_PIN_6, GPIO_AF2_TIM3, false},
};

/* PwmCaptureChannel -> timer/pad. 100 kHz tick: 10 us resolution (0.05%
 * duty at 50 Hz) with a 655 ms silence window, covering the IMD's slowest
 * state with margin. Adding a channel also needs its TIMx_IRQHandler below. */
static const CaptureMap capture_map[PWM_CAP_CH_COUNT] = {
    [PWM_CAP_CH_TEST] = {TIM4, TIM4_IRQn, GPIOB, GPIO_PIN_6, GPIO_AF2_TIM4, 100000U},
};

_Static_assert(PWM_CH_COUNT == 1,
               "PwmChannel catalogue changed -- add/remove the matching output_map[] row above");
_Static_assert(PWM_CAP_CH_COUNT == 1,
               "PwmCaptureChannel catalogue changed -- add/remove the capture_map[] row above");

static OutputState  outputs[PWM_CH_COUNT];
static CaptureState captures[PWM_CAP_CH_COUNT];

static bool channel_valid(PwmChannel channel)
{
    return (unsigned int)channel < (unsigned int)PWM_CH_COUNT;
}

static bool capture_channel_valid(PwmCaptureChannel channel)
{
    return (unsigned int)channel < (unsigned int)PWM_CAP_CH_COUNT;
}

static bool enable_port_clock(const GPIO_TypeDef *port)
{
    if (port == GPIOA) {
        __HAL_RCC_GPIOA_CLK_ENABLE();
    } else if (port == GPIOB) {
        __HAL_RCC_GPIOB_CLK_ENABLE();
    } else if (port == GPIOC) {
        __HAL_RCC_GPIOC_CLK_ENABLE();
    } else if (port == GPIOD) {
        __HAL_RCC_GPIOD_CLK_ENABLE();
    } else {
        return false;
    }
    return true;
}

/* Enables the timer's bus clock. Only the general-purpose timers this
 * driver may own are listed; TIM10 is the HAL timebase and is excluded. */
static bool enable_timer_clock(const TIM_TypeDef *timer)
{
    if (timer == TIM2) {
        __HAL_RCC_TIM2_CLK_ENABLE();
    } else if (timer == TIM3) {
        __HAL_RCC_TIM3_CLK_ENABLE();
    } else if (timer == TIM4) {
        __HAL_RCC_TIM4_CLK_ENABLE();
    } else if (timer == TIM5) {
        __HAL_RCC_TIM5_CLK_ENABLE();
    } else {
        return false;
    }
    return true;
}

/**
 * @brief Counter input clock of an APB1 timer: PCLK1 when the APB1
 * prescaler is 1, else 2 x PCLK1 (RM0390 6.2; assumes the reset value
 * TIMPRE = 0). Every timer enable_timer_clock() accepts is on APB1.
 */
static uint32_t timer_clock_hz(void)
{
    uint32_t pclk1_hz = HAL_RCC_GetPCLK1Freq();

    if ((RCC->CFGR & RCC_CFGR_PPRE1) == RCC_CFGR_PPRE1_DIV1) {
        return pclk1_hz;
    }
    return 2U * pclk1_hz;
}

/**
 * @brief Pick PSC/ARR for @p frequency_hz: smallest prescaler whose period
 * fits ARR <= 0xFFFE (best duty resolution), period rounded to nearest.
 * @param      frequency_hz  requested carrier frequency
 * @param[out] prescale      PSC + 1
 * @param[out] counts        ARR + 1, in [PWM_STM32_MIN_COUNTS, 0xFFFF]
 * @return false if the frequency is zero or unreachable from the clock
 */
static bool compute_timing(uint32_t frequency_hz, uint32_t *prescale, uint32_t *counts)
{
    uint32_t clock_hz = timer_clock_hz();
    uint64_t ticks;
    uint64_t div;
    uint64_t period;

    if ((frequency_hz == 0U) || (frequency_hz > (clock_hz / PWM_STM32_MIN_COUNTS))) {
        return false;
    }
    ticks  = ((uint64_t)clock_hz + (frequency_hz / 2U)) / frequency_hz;
    div    = (ticks + PWM_STM32_MAX_COUNTS - 1U) / PWM_STM32_MAX_COUNTS;
    period = (ticks + (div / 2U)) / div;
    if ((div > PWM_STM32_MAX_PRESCALE) || (period < PWM_STM32_MIN_COUNTS)
        || (period > PWM_STM32_MAX_COUNTS)) {
        return false;
    }
    *prescale = (uint32_t)div;
    *counts   = (uint32_t)period;
    return true;
}

/* Compare value for duty_pct over a period of counts: 0 -> 0 (DC low),
 * 100 -> counts (= ARR + 1, DC high), nearest tick in between. */
static uint32_t compare_for(uint32_t counts, uint8_t duty_pct)
{
    return ((counts * (uint32_t)duty_pct) + 50U) / 100U;
}

/* Park an output pad at its safe level as a plain GPIO output. Level is
 * written before the mode switch so the pad never glitches. */
static void park_output(const OutputMap *map)
{
    GPIO_InitTypeDef gpio_init = {0};

    HAL_GPIO_WritePin(map->port, map->pad, map->safe_high ? GPIO_PIN_SET : GPIO_PIN_RESET);
    gpio_init.Pin   = map->pad;
    gpio_init.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull  = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(map->port, &gpio_init);
}

/* Hand a pad to its timer (alternate function). */
static void connect_pad(GPIO_TypeDef *port, uint16_t pad, uint8_t alternate, uint32_t pull)
{
    GPIO_InitTypeDef gpio_init = {0};

    gpio_init.Pin       = pad;
    gpio_init.Mode      = GPIO_MODE_AF_PP;
    gpio_init.Pull      = pull;
    gpio_init.Speed     = GPIO_SPEED_FREQ_LOW;
    gpio_init.Alternate = alternate;
    HAL_GPIO_Init(port, &gpio_init);
}

/* True if timer is already running for an output other than self_output
 * or a capture channel other than self_capture (pass PWM_CH_COUNT /
 * PWM_CAP_CH_COUNT for "none"). One timer, one logical channel. */
static bool timer_claimed(const TIM_TypeDef *timer, size_t self_output, size_t self_capture)
{
    size_t i;

    for (i = 0U; i < (size_t)PWM_CH_COUNT; i++) {
        if ((i != self_output) && outputs[i].started && (output_map[i].timer == timer)) {
            return true;
        }
    }
    for (i = 0U; i < (size_t)PWM_CAP_CH_COUNT; i++) {
        if ((i != self_capture) && captures[i].capturing && (capture_map[i].timer == timer)) {
            return true;
        }
    }
    return false;
}

/**
 * @brief PwmIf::init -- park the pad at its safe level, configure the
 * timer for @p config, start it, then connect the pad. Re-init of a
 * running channel passes back through the safe level.
 */
static IfStatus stm32_init(PwmChannel channel, const PwmConfig *config)
{
    TIM_OC_InitTypeDef oc = {0};
    const OutputMap   *map;
    OutputState       *out;
    uint32_t           prescale;
    uint32_t           counts;

    if (!channel_valid(channel)) {
        return IF_HW_FAULT;
    }
    map = &output_map[channel];
    out = &outputs[channel];
    if (!enable_port_clock(map->port)) {
        return IF_HW_FAULT;
    }
    park_output(map); /* safe level before anything else is touched */

    out->configured = false;
    if (out->started) {
        (void)HAL_TIM_PWM_Stop(&out->htim, map->tim_channel);
        out->started = false;
    }
    if ((config == NULL) || (config->duty_pct > 100U)
        || !compute_timing(config->frequency_hz, &prescale, &counts)
        || timer_claimed(map->timer, (size_t)channel, (size_t)PWM_CAP_CH_COUNT)
        || !enable_timer_clock(map->timer)) {
        return IF_HW_FAULT;
    }

    out->htim.Instance               = map->timer;
    out->htim.Init.Prescaler         = prescale - 1U;
    out->htim.Init.CounterMode       = TIM_COUNTERMODE_UP;
    out->htim.Init.Period            = counts - 1U;
    out->htim.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    out->htim.Init.RepetitionCounter = 0U;
    out->htim.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_PWM_Init(&out->htim) != HAL_OK) {
        return IF_HW_FAULT;
    }

    oc.OCMode       = TIM_OCMODE_PWM1;
    oc.Pulse        = compare_for(counts, config->duty_pct);
    oc.OCPolarity   = TIM_OCPOLARITY_HIGH;
    oc.OCNPolarity  = TIM_OCNPOLARITY_HIGH;
    oc.OCFastMode   = TIM_OCFAST_DISABLE;
    oc.OCIdleState  = TIM_OCIDLESTATE_RESET;
    oc.OCNIdleState = TIM_OCNIDLESTATE_RESET;
    if (HAL_TIM_PWM_ConfigChannel(&out->htim, &oc, map->tim_channel) != HAL_OK) {
        return IF_HW_FAULT;
    }
    /* Load PSC/ARR/CCR into the active registers before the output runs. */
    if ((HAL_TIM_GenerateEvent(&out->htim, TIM_EVENTSOURCE_UPDATE) != HAL_OK)
        || (HAL_TIM_PWM_Start(&out->htim, map->tim_channel) != HAL_OK)) {
        return IF_HW_FAULT;
    }
    out->started = true;

    out->duty_pct            = config->duty_pct;
    out->counts              = counts;
    out->actual_frequency_hz = timer_clock_hz() / (prescale * counts);
    out->configured          = true;

    connect_pad(map->port, map->pad, map->alternate, GPIO_NOPULL);
    return IF_OK;
}

/**
 * @brief PwmIf::set_duty_pct -- one preloaded CCR write; takes effect at
 * the next update event, so the period in flight completes untouched.
 */
static IfStatus stm32_set_duty_pct(PwmChannel channel, uint8_t duty_pct)
{
    OutputState *out;

    if (!channel_valid(channel) || (duty_pct > 100U) || !outputs[channel].configured) {
        return IF_HW_FAULT;
    }
    out = &outputs[channel];
    __HAL_TIM_SET_COMPARE(&out->htim, output_map[channel].tim_channel,
                          compare_for(out->counts, duty_pct));
    out->duty_pct = duty_pct;
    return IF_OK;
}

/**
 * @brief PwmIf::set_frequency_hz -- recompute PSC/ARR, rescale CCR to keep
 * the duty, and write all three under UDIS so they take effect together
 * at the next natural update event.
 */
static IfStatus stm32_set_frequency_hz(PwmChannel channel, uint32_t frequency_hz)
{
    OutputState *out;
    TIM_TypeDef *timer;
    uint32_t     prescale;
    uint32_t     counts;

    if (!channel_valid(channel) || !outputs[channel].configured
        || !compute_timing(frequency_hz, &prescale, &counts)) {
        return IF_HW_FAULT;
    }
    out   = &outputs[channel];
    timer = out->htim.Instance;

    timer->CR1 |= TIM_CR1_UDIS;
    timer->PSC = prescale - 1U;
    timer->ARR = counts - 1U;
    __HAL_TIM_SET_COMPARE(&out->htim, output_map[channel].tim_channel,
                          compare_for(counts, out->duty_pct));
    timer->CR1 &= ~TIM_CR1_UDIS;

    out->htim.Init.Prescaler = prescale - 1U;
    out->htim.Init.Period    = counts - 1U;
    out->counts              = counts;
    out->actual_frequency_hz = timer_clock_hz() / (prescale * counts);
    return IF_OK;
}

/**
 * @brief Capture-timer ISR body: minimal work, no FreeRTOS calls. Overflow
 * is handled before capture so a capture racing a wrap is treated as
 * aliased (conservative).
 */
static void capture_isr(PwmCaptureChannel channel)
{
    CaptureState *cap   = &captures[channel];
    TIM_TypeDef  *timer = cap->htim.Instance;
    uint32_t      period_ticks;
    uint32_t      high_ticks;

    if ((timer->SR & TIM_SR_UIF) != 0U) {
        timer->SR   = ~TIM_SR_UIF; /* rc_w0: writing 1 elsewhere is a no-op */
        cap->sample = 0U;          /* silence window expired: stale */
        cap->primed = false;       /* next capture spans the wrap: aliased */
    }
    if ((timer->SR & TIM_SR_CC1IF) != 0U) {
        period_ticks = timer->CCR1 & 0xFFFFU; /* reading CCR1 clears CC1IF */
        high_ticks   = timer->CCR2 & 0xFFFFU;
        timer->SR    = ~(TIM_SR_CC1OF | TIM_SR_CC2OF | TIM_SR_CC2IF);
        if (!cap->primed) {
            cap->primed = true; /* first edge: period started at an unknown time */
        } else if ((period_ticks != 0U) && (high_ticks <= period_ticks)) {
            cap->sample = (high_ticks << 16U) | period_ticks;
        } else {
            cap->sample = 0U; /* edge faster than the tick, or torn pair */
        }
    }
}

/**
 * @brief TIM4 global interrupt vector -- PWM_CAP_CH_TEST. Flags are handled
 * directly rather than through HAL_TIM_IRQHandler so this driver does not
 * claim the global weak HAL_TIM_*Callback hooks (the HAL timebase on TIM10
 * may need them).
 */
void TIM4_IRQHandler(void)
{
    capture_isr(PWM_CAP_CH_TEST);
}

/**
 * @brief PwmIf::start_capture -- bring the timer up in PWM-input mode
 * (CH1 rising = period + counter reset, CH2 falling = high time) and arm
 * the CC1 and overflow interrupts.
 */
static IfStatus stm32_start_capture(PwmCaptureChannel channel)
{
    TIM_IC_InitTypeDef     ic    = {0};
    TIM_SlaveConfigTypeDef slave = {0};
    const CaptureMap      *map;
    CaptureState          *cap;
    uint32_t               clock_hz = timer_clock_hz();
    uint32_t               prescale;

    if (!capture_channel_valid(channel)) {
        return IF_HW_FAULT;
    }
    map = &capture_map[channel];
    cap = &captures[channel];
    if (cap->capturing) {
        return IF_BUSY;
    }
    prescale = clock_hz / map->tick_hz;
    if ((prescale == 0U) || (prescale > PWM_STM32_MAX_PRESCALE)
        || timer_claimed(map->timer, (size_t)PWM_CH_COUNT, (size_t)channel)
        || !enable_port_clock(map->port) || !enable_timer_clock(map->timer)) {
        return IF_HW_FAULT;
    }

    cap->htim.Instance               = map->timer;
    cap->htim.Init.Prescaler         = prescale - 1U;
    cap->htim.Init.CounterMode       = TIM_COUNTERMODE_UP;
    cap->htim.Init.Period            = 0xFFFFU;
    cap->htim.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    cap->htim.Init.RepetitionCounter = 0U;
    cap->htim.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_IC_Init(&cap->htim) != HAL_OK) {
        return IF_HW_FAULT;
    }

    ic.ICPolarity  = TIM_ICPOLARITY_RISING;
    ic.ICSelection = TIM_ICSELECTION_DIRECTTI;
    ic.ICPrescaler = TIM_ICPSC_DIV1;
    ic.ICFilter    = PWM_STM32_CAPTURE_FILTER;
    if (HAL_TIM_IC_ConfigChannel(&cap->htim, &ic, TIM_CHANNEL_1) != HAL_OK) {
        return IF_HW_FAULT;
    }
    ic.ICPolarity  = TIM_ICPOLARITY_FALLING;
    ic.ICSelection = TIM_ICSELECTION_INDIRECTTI;
    if (HAL_TIM_IC_ConfigChannel(&cap->htim, &ic, TIM_CHANNEL_2) != HAL_OK) {
        return IF_HW_FAULT;
    }

    slave.SlaveMode        = TIM_SLAVEMODE_RESET;
    slave.InputTrigger     = TIM_TS_TI1FP1;
    slave.TriggerPolarity  = TIM_TRIGGERPOLARITY_RISING;
    slave.TriggerPrescaler = TIM_TRIGGERPRESCALER_DIV1;
    slave.TriggerFilter    = PWM_STM32_CAPTURE_FILTER;
    if (HAL_TIM_SlaveConfigSynchro(&cap->htim, &slave) != HAL_OK) {
        return IF_HW_FAULT;
    }

    /* URS: only a genuine counter wrap raises UIF, never a slave reset. */
    map->timer->CR1 |= TIM_CR1_URS;
    map->timer->SR = 0U;

    cap->tick_hz = clock_hz / prescale;
    cap->sample  = 0U;
    cap->primed  = false;

    /* Pull-down: a disconnected input sits at a constant level -- no edges,
     * IF_TIMEOUT -- instead of floating into plausible-looking noise. */
    connect_pad(map->port, map->pad, map->alternate, GPIO_PULLDOWN);

    HAL_NVIC_SetPriority(map->irq, PWM_STM32_IRQ_PRIORITY, 0U);
    HAL_NVIC_EnableIRQ(map->irq);
    __HAL_TIM_ENABLE_IT(&cap->htim, TIM_IT_UPDATE);
    if ((HAL_TIM_IC_Start(&cap->htim, TIM_CHANNEL_2) != HAL_OK)
        || (HAL_TIM_IC_Start_IT(&cap->htim, TIM_CHANNEL_1) != HAL_OK)) {
        HAL_NVIC_DisableIRQ(map->irq);
        return IF_HW_FAULT;
    }

    cap->armed     = true;
    cap->capturing = true;
    return IF_OK;
}

/**
 * @brief PwmIf::stop_capture -- stop the timer and its interrupts; the
 * last sample stays readable (last known good).
 */
static IfStatus stm32_stop_capture(PwmCaptureChannel channel)
{
    CaptureState *cap;

    if (!capture_channel_valid(channel) || !captures[channel].capturing) {
        return IF_HW_FAULT;
    }
    cap = &captures[channel];
    HAL_NVIC_DisableIRQ(capture_map[channel].irq);
    __HAL_TIM_DISABLE_IT(&cap->htim, TIM_IT_UPDATE);
    (void)HAL_TIM_IC_Stop_IT(&cap->htim, TIM_CHANNEL_1);
    (void)HAL_TIM_IC_Stop(&cap->htim, TIM_CHANNEL_2);
    cap->capturing = false;
    return IF_OK;
}

/**
 * @brief Snapshot the latest sample for a read: one 32-bit load, so the
 * period/high pair is always from the same input period.
 */
static IfStatus
read_sample(PwmCaptureChannel channel, bool out_valid, uint32_t *period_ticks, uint32_t *high_ticks)
{
    uint32_t sample;

    if (!capture_channel_valid(channel) || !out_valid || !captures[channel].armed) {
        return IF_HW_FAULT;
    }
    sample = captures[channel].sample;
    if (sample == 0U) {
        return IF_TIMEOUT;
    }
    *period_ticks = sample & 0xFFFFU;
    *high_ticks   = sample >> 16U;
    return IF_OK;
}

static IfStatus stm32_read_frequency(PwmCaptureChannel channel, uint32_t *out_frequency_hz)
{
    uint32_t period_ticks = 0U;
    uint32_t high_ticks   = 0U;
    IfStatus status = read_sample(channel, out_frequency_hz != NULL, &period_ticks, &high_ticks);

    if (status == IF_OK) {
        *out_frequency_hz = (captures[channel].tick_hz + (period_ticks / 2U)) / period_ticks;
    }
    return status;
}

static IfStatus stm32_read_duty_permille(PwmCaptureChannel channel, uint16_t *out_duty_permille)
{
    uint32_t period_ticks = 0U;
    uint32_t high_ticks   = 0U;
    IfStatus status = read_sample(channel, out_duty_permille != NULL, &period_ticks, &high_ticks);

    if (status == IF_OK) {
        *out_duty_permille = (uint16_t)(((high_ticks * 1000U) + (period_ticks / 2U))
                                        / period_ticks);
    }
    return status;
}

const PwmIf pwm_stm32 = {
    .init               = stm32_init,
    .set_duty_pct       = stm32_set_duty_pct,
    .set_frequency_hz   = stm32_set_frequency_hz,
    .start_capture      = stm32_start_capture,
    .stop_capture       = stm32_stop_capture,
    .read_frequency     = stm32_read_frequency,
    .read_duty_permille = stm32_read_duty_permille,
};

void pwm_stm32_enter_safe_state(void)
{
    size_t i;

    for (i = 0U; i < (size_t)PWM_CH_COUNT; i++) {
        if (enable_port_clock(output_map[i].port)) {
            park_output(&output_map[i]);
        }
        outputs[i].configured = false;
    }
}

uint32_t pwm_stm32_actual_frequency_hz(PwmChannel channel)
{
    if (!channel_valid(channel) || !outputs[channel].configured) {
        return 0U;
    }
    return outputs[channel].actual_frequency_hz;
}
