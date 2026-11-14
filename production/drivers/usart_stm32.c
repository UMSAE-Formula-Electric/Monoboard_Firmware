/**
 * @file usart_stm32.c
 * @brief uart_if implementation for ST HAL's USART2 peripheral (HAL_UART,
 * asynchronous).
 *
 * Wiring: USART2_TX -> PA2, USART2_RX -> PA3, AF7; TX DMA on DMA1 Stream6
 * Channel4, RX DMA on DMA1 Stream5 Channel4 (this board's schematic / the
 * F446's fixed DMA request map; change here, and only here, if either
 * moves). Frame format and baud rate come from UartConfig at init time.
 * The .ioc has USART2 as Asynchronous; CubeMX's generated usart.c is not
 * compiled, this file owns the peripheral.
 *
 * TX: non-blocking, DMA-driven, with a software queue of pending buffers
 * (issue #16). transmit() either kicks a DMA transfer immediately (nothing
 * in flight) or enqueues the buffer; HAL_UART_TxCpltCallback (ISR context)
 * fires the caller's completion callback for the buffer that just finished
 * and starts the next queued one, so tx-complete fires exactly once per
 * accepted transmit() call. A full queue returns IF_BUSY without blocking
 * or dropping, per the contract.
 *
 * RX (issue #51): DMA in circular mode into rx_buffer, plus idle-line
 * detection. The HAL reports the write position at three moments: buffer
 * half full, buffer full (the DMA wraps to the start), and when the line
 * goes idle after a burst. HAL_UARTEx_RxEventCallback (ISR context) hands
 * the bytes between the previous position and the new one to the callback
 * given to start_rx(). No per-byte interrupt, and a single typed character
 * is delivered one character-time after it arrives.
 *
 * Errors: overrun, framing and noise are counted (see
 * usart_stm32_*_error_count()). With DMA reception running the HAL aborts
 * the reception on any of them, so HAL_UART_ErrorCallback starts it again:
 * a noisy line costs the bytes around the error, never the link.
 */
#include "usart_stm32.h"

#include <stdbool.h>

#include "FreeRTOS.h"
#include "queue.h"

#include "stm32f4xx_hal.h"

#define USART_STM32_TX_PENDING_DEPTH 8U

/* Circular receive buffer. At 115200 baud a byte takes 87 us, so the
 * half/full events are at least 2.8 ms apart: nothing is overwritten unless
 * the receive interrupt is held off for longer than that. */
#define USART_STM32_RX_BUFFER_BYTES 64U

typedef struct {
    const uint8_t   *data;
    size_t           size;
    UartTxCompleteCb cb;
    void            *ctx;
} PendingTx;

static UART_HandleTypeDef huart2;
static DMA_HandleTypeDef  hdma_usart2_tx;
static DMA_HandleTypeDef  hdma_usart2_rx;

static StaticQueue_t pending_tx_queue_struct;
static uint8_t       pending_tx_queue_storage[USART_STM32_TX_PENDING_DEPTH * sizeof(PendingTx)];
static QueueHandle_t pending_tx_queue_handle;

/* The transfer currently owned by the DMA/peripheral, if any; only ever
 * touched with interrupts masked (taskENTER_CRITICAL / from ISR context). */
static volatile bool tx_in_flight;
static PendingTx     current_tx;

/* Receive state. rx_pos is only touched from the USART2 and DMA1 Stream5
 * interrupts, which share one priority and so never preempt each other. */
static uint8_t  rx_buffer[USART_STM32_RX_BUFFER_BYTES];
static size_t   rx_pos; /* next unread index in rx_buffer */
static UartRxCb rx_cb;
static void    *rx_ctx;
static bool     initialized;

static volatile uint32_t overrun_error_count;
static volatile uint32_t framing_error_count;
static volatile uint32_t noise_error_count;
static int32_t           baud_error_permille;

/**
 * @brief Enable the clocks USART2/GPIOA/DMA1 need and configure PA2/PA3 for
 * USART2's alternate function (AF7).
 */
static void stm32_configure_gpio_and_clock(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();

    gpio_init.Pin       = GPIO_PIN_2;
    gpio_init.Mode      = GPIO_MODE_AF_PP;
    gpio_init.Pull      = GPIO_NOPULL;
    gpio_init.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio_init.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &gpio_init);

    /* RX idles high. Pull it up so that an unconnected debug port reads as
     * an idle line instead of floating into a stream of framing errors. */
    gpio_init.Pin  = GPIO_PIN_3;
    gpio_init.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &gpio_init);
}

/**
 * @brief HAL MSP hook: called by HAL_UART_Init() to bring up everything the
 * peripheral itself doesn't own -- GPIO alternate function, the TX and RX
 * DMA streams, and NVIC priority/enable for the USART and DMA IRQs.
 * @param huart  handle being initialised; ignored unless it is USART2
 */
void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART2) {
        return;
    }

    stm32_configure_gpio_and_clock();

    hdma_usart2_tx.Instance                 = DMA1_Stream6;
    hdma_usart2_tx.Init.Channel             = DMA_CHANNEL_4;
    hdma_usart2_tx.Init.Direction           = DMA_MEMORY_TO_PERIPH;
    hdma_usart2_tx.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_usart2_tx.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_usart2_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart2_tx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
    hdma_usart2_tx.Init.Mode                = DMA_NORMAL;
    hdma_usart2_tx.Init.Priority            = DMA_PRIORITY_MEDIUM;
    hdma_usart2_tx.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    (void)HAL_DMA_Init(&hdma_usart2_tx);
    __HAL_LINKDMA(huart, hdmatx, hdma_usart2_tx);

    /* Circular: the DMA wraps to the start of rx_buffer by itself, so
     * reception never has to be re-armed between bursts. */
    hdma_usart2_rx.Instance                 = DMA1_Stream5;
    hdma_usart2_rx.Init.Channel             = DMA_CHANNEL_4;
    hdma_usart2_rx.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    hdma_usart2_rx.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_usart2_rx.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_usart2_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart2_rx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
    hdma_usart2_rx.Init.Mode                = DMA_CIRCULAR;
    hdma_usart2_rx.Init.Priority            = DMA_PRIORITY_MEDIUM;
    hdma_usart2_rx.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    (void)HAL_DMA_Init(&hdma_usart2_rx);
    __HAL_LINKDMA(huart, hdmarx, hdma_usart2_rx);

    /* Priorities must be numerically >= configLIBRARY_MAX_SYSCALL_INTERRUPT_
     * PRIORITY so these ISRs are allowed to call FromISR FreeRTOS APIs. All
     * three share one priority, so none of them preempts another. */
    HAL_NVIC_SetPriority(DMA1_Stream6_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream6_IRQn);
    HAL_NVIC_SetPriority(DMA1_Stream5_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream5_IRQn);
    HAL_NVIC_SetPriority(USART2_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
}

/**
 * @brief HAL MSP hook: called by HAL_UART_DeInit() to undo everything
 * HAL_UART_MspInit() set up -- NVIC, DMA, GPIO, and the peripheral clock.
 * @param huart  handle being de-initialised; ignored unless it is USART2
 */
/* cppcheck-suppress constParameterPointer ; signature fixed by stm32f4xx_hal_uart.h */
void HAL_UART_MspDeInit(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART2) {
        return;
    }

    HAL_NVIC_DisableIRQ(USART2_IRQn);
    HAL_NVIC_DisableIRQ(DMA1_Stream6_IRQn);
    HAL_NVIC_DisableIRQ(DMA1_Stream5_IRQn);
    (void)HAL_DMA_DeInit(&hdma_usart2_tx);
    (void)HAL_DMA_DeInit(&hdma_usart2_rx);
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_2 | GPIO_PIN_3);
    __HAL_RCC_USART2_CLK_DISABLE();
}

/**
 * @brief USART2 interrupt vector: hands off to the HAL, which decodes the
 * cause (transmit complete, idle line, error) and dispatches to the matching
 * HAL_UART*Callback().
 */
void USART2_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart2);
}

/**
 * @brief DMA1 Stream6 interrupt vector (USART2 TX): hands off to the HAL,
 * which dispatches to HAL_UART_TxCpltCallback() or HAL_UART_ErrorCallback()
 * as appropriate.
 */
void DMA1_Stream6_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_usart2_tx);
}

/**
 * @brief DMA1 Stream5 interrupt vector (USART2 RX): hands off to the HAL,
 * which calls HAL_UARTEx_RxEventCallback() when rx_buffer is half full and
 * when it is full.
 */
void DMA1_Stream5_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_usart2_rx);
}

/**
 * @brief Pull the next pending buffer off the queue, if any, and start its
 * DMA transfer. Caller must already hold tx_in_flight == false and be
 * calling from ISR context (uses the FromISR queue API).
 * @param higher_priority_task_woken  [out] set to pdTRUE if unblocking a
 *                                    higher-priority task; the caller must
 *                                    portYIELD_FROM_ISR on it afterwards
 */
static void stm32_start_next_tx_from_isr(BaseType_t *higher_priority_task_woken)
{
    if (xQueueReceiveFromISR(pending_tx_queue_handle, &current_tx, higher_priority_task_woken)
        == pdTRUE) {
        tx_in_flight = true;
        (void)HAL_UART_Transmit_DMA(&huart2, current_tx.data, (uint16_t)current_tx.size);
    }
}

/**
 * @brief HAL callback: fires once the current transfer has fully left the
 * wire. Reports completion to that buffer's own on_complete (exactly
 * once, per the uart_if.h contract) and immediately starts the next queued
 * transfer, if any.
 * @param huart  handle that completed; ignored unless it is USART2
 */
/* cppcheck-suppress constParameterPointer ; signature fixed by stm32f4xx_hal_uart.h */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    BaseType_t higher_priority_task_woken = pdFALSE;
    PendingTx  completed;

    if (huart->Instance != USART2) {
        return;
    }

    completed    = current_tx;
    tx_in_flight = false;

    stm32_start_next_tx_from_isr(&higher_priority_task_woken);

    if (completed.cb != NULL) {
        completed.cb(completed.ctx, completed.size);
    }

    portYIELD_FROM_ISR(higher_priority_task_woken);
}

/**
 * @brief Start (or restart) circular DMA reception with idle-line events.
 * @return true if the HAL accepted it
 */
static bool stm32_arm_rx(void)
{
    rx_pos = 0U;
    return HAL_UARTEx_ReceiveToIdle_DMA(&huart2, rx_buffer, (uint16_t)sizeof(rx_buffer)) == HAL_OK;
}

/**
 * @brief Hand rx_buffer[from, to) to the receive callback, if there is one
 * and the range is not empty.
 * @param from  first index to deliver
 * @param to    one past the last index to deliver
 */
static void stm32_deliver_rx(size_t from, size_t to)
{
    if ((rx_cb != NULL) && (to > from)) {
        rx_cb(rx_ctx, &rx_buffer[from], to - from);
    }
}

/**
 * @brief HAL callback: the DMA write position in rx_buffer moved. Fires when
 * the buffer is half full, when it is full (the DMA then wraps to index 0),
 * and when the line goes idle after a burst. Delivers every byte between
 * the last delivered position and the DMA's current one, exactly once.
 *
 * The position is read from the DMA counter here, not taken from @p size.
 * @p size is the position at the moment the event was raised; if a second
 * event was already pending (the buffer wrapped while the idle interrupt was
 * being served), acting on the older number would deliver bytes of the
 * previous lap a second time.
 * @param huart  handle that received; ignored unless it is USART2
 * @param size   position when the event was raised; unused, see above
 */
/* cppcheck-suppress constParameterPointer ; signature fixed by stm32f4xx_hal_uart.h */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size)
{
    size_t remaining;
    size_t pos;

    (void)size;
    if (huart->Instance != USART2) {
        return;
    }

    /* The counter runs from sizeof(rx_buffer) down to 1 and reloads, so the
     * write position is 0..sizeof(rx_buffer)-1. */
    remaining = (size_t)__HAL_DMA_GET_COUNTER(&hdma_usart2_rx);
    if ((remaining == 0U) || (remaining > sizeof(rx_buffer))) {
        return; /* cannot happen; never index past the end */
    }
    pos = sizeof(rx_buffer) - remaining;

    if (pos < rx_pos) {
        /* The DMA wrapped: finish the old lap, then start the new one. */
        stm32_deliver_rx(rx_pos, sizeof(rx_buffer));
        rx_pos = 0U;
    }
    stm32_deliver_rx(rx_pos, pos);
    rx_pos = pos;
}

/**
 * @brief HAL callback: fires on any UART/DMA error. Counts overrun/framing/
 * noise errors (see usart_stm32_*_error_count()) and recovers both
 * directions, because the HAL stops them: a DMA-side error kills the
 * in-flight transmit, and any error during DMA reception aborts reception.
 * @param huart  handle that faulted; ignored unless it is USART2
 */
/* cppcheck-suppress constParameterPointer ; signature fixed by stm32f4xx_hal_uart.h */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    BaseType_t higher_priority_task_woken = pdFALSE;

    if (huart->Instance != USART2) {
        return;
    }

    if ((huart->ErrorCode & HAL_UART_ERROR_ORE) != 0U) {
        overrun_error_count++;
    }
    if ((huart->ErrorCode & HAL_UART_ERROR_FE) != 0U) {
        framing_error_count++;
    }
    if ((huart->ErrorCode & HAL_UART_ERROR_NE) != 0U) {
        noise_error_count++;
    }

    /* A DMA-side error aborts the in-flight transfer: it will not
     * complete on its own, so recover by handing the peripheral the next
     * queued buffer (if any) rather than leaving TX wedged. The caller of
     * the aborted transmit() never gets its on_complete -- it already
     * knows nothing was guaranteed once IF_OK was returned, same as any
     * other DMA transfer, but this is the one path where "exactly once"
     * becomes "zero times" instead; a reset is not required to recover. */
    if ((huart->ErrorCode & HAL_UART_ERROR_DMA) != 0U) {
        tx_in_flight = false;
        stm32_start_next_tx_from_isr(&higher_priority_task_woken);
    }

    /* The HAL has aborted reception by the time this runs. Bytes still in
     * rx_buffer are dropped with it: after a line error they are suspect. */
    if ((rx_cb != NULL) && (huart->RxState == HAL_UART_STATE_READY)) {
        (void)stm32_arm_rx();
    }

    portYIELD_FROM_ISR(higher_priority_task_woken);
}

/**
 * @brief Translate the contract's UartParity into the HAL's
 * UART_PARITY_* register-bit constant.
 * @param parity  parity mode requested via UartConfig
 */
static uint32_t hal_parity(UartParity parity)
{
    switch (parity) {
        case UART_IF_PARITY_EVEN:
            return UART_PARITY_EVEN;
        case UART_IF_PARITY_ODD:
            return UART_PARITY_ODD;
        case UART_IF_PARITY_NONE:
        default:
            return UART_PARITY_NONE;
    }
}

/**
 * @brief Translate the contract's UartStopBits into the HAL's
 * UART_STOPBITS_* register-bit constant.
 * @param stop_bits  stop-bit count requested via UartConfig
 */
static uint32_t hal_stop_bits(UartStopBits stop_bits)
{
    return (stop_bits == UART_STOP_BITS_2) ? UART_STOPBITS_2 : UART_STOPBITS_1;
}

/**
 * @brief Translate the contract's parity-exclusive data_bits into the HAL's
 * UART_WORDLENGTH_*, which counts the parity bit as part of the frame: 8
 * data bits plus a parity bit is a 9-bit HAL word.
 * @param data_bits  bits per frame requested via UartConfig, excluding parity
 * @param parity     parity mode requested via UartConfig
 */
static uint32_t hal_word_length(uint8_t data_bits, UartParity parity)
{
    const uint32_t frame_bits = (uint32_t)data_bits + ((parity == UART_IF_PARITY_NONE) ? 0U : 1U);

    return (frame_bits >= 9U) ? UART_WORDLENGTH_9B : UART_WORDLENGTH_8B;
}

/**
 * @brief Read back the BRR the HAL actually programmed and decode the baud
 * rate it produces, rather than re-deriving ST's rounding independently.
 * init() selects 16x oversampling (OVER8=0), where BRR holds 16 * USARTDIV
 * as a 12-bit mantissa and a 4-bit fraction: read as one integer it is the
 * clock divider itself, so baud = pclk / BRR.
 * Stores the result in baud_error_permille for usart_stm32_baud_error_permille().
 * @param configured_baud_bps  the baud rate that was requested via init()
 */
static void update_baud_error(uint32_t configured_baud_bps)
{
    uint32_t pclk_hz = HAL_RCC_GetPCLK1Freq();
    uint32_t divider = huart2.Instance->BRR & 0xFFFFU;

    if (divider == 0U || configured_baud_bps == 0U) {
        baud_error_permille = 0;
        return;
    }

    uint32_t actual_baud_bps = pclk_hz / divider;
    int64_t  error_x1000     = ((int64_t)actual_baud_bps - (int64_t)configured_baud_bps) * 1000;
    baud_error_permille      = (int32_t)(error_x1000 / (int64_t)configured_baud_bps);
}

/**
 * @brief uart_if::init -- program USART2 for the requested frame format and
 * baud rate, record the resulting baud error, and allocate the pending-TX
 * queue. Reception stays off until start_rx().
 * @param config  frame format and baud rate; NULL or a zero baud rate fails
 * @return #IF_OK on success, #IF_HW_FAULT if @p config is invalid or the
 *         HAL rejected the setup
 */
static IfStatus stm32_init(const UartConfig *config)
{
    if (config == NULL || config->baud_rate_bps == 0U) {
        return IF_HW_FAULT;
    }

    /* A re-init stops a running reception before the callback is cleared. */
    if (initialized) {
        (void)HAL_UART_AbortReceive(&huart2);
    }
    initialized = false;
    rx_cb       = NULL;
    rx_ctx      = NULL;

    huart2.Instance          = USART2;
    huart2.Init.BaudRate     = config->baud_rate_bps;
    huart2.Init.WordLength   = hal_word_length(config->data_bits, config->parity);
    huart2.Init.StopBits     = hal_stop_bits(config->stop_bits);
    huart2.Init.Parity       = hal_parity(config->parity);
    huart2.Init.Mode         = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&huart2) != HAL_OK) {
        return IF_HW_FAULT;
    }
    update_baud_error(config->baud_rate_bps);

    pending_tx_queue_handle = xQueueCreateStatic(USART_STM32_TX_PENDING_DEPTH, sizeof(PendingTx),
                                                 pending_tx_queue_storage,
                                                 &pending_tx_queue_struct);
    if (pending_tx_queue_handle == NULL) {
        return IF_HW_FAULT;
    }

    tx_in_flight        = false;
    overrun_error_count = 0U;
    framing_error_count = 0U;
    noise_error_count   = 0U;
    initialized         = true;

    return IF_OK;
}

/**
 * @brief uart_if::transmit -- queue @p data for DMA transmission without
 * blocking. Starts the DMA transfer immediately if the peripheral is idle,
 * otherwise enqueues the buffer for stm32_start_next_tx_from_isr() to pick
 * up once the current transfer completes.
 * @param data         bytes to transmit; must stay valid and unmodified
 *                     until @p on_complete fires
 * @param size         number of bytes in @p data
 * @param on_complete  called from ISR context when @p data has been fully
 *                     sent; may be NULL to ignore completion
 * @param ctx          opaque pointer passed back to @p on_complete
 * @return #IF_OK if the transfer started or was queued, #IF_BUSY if the
 *         pending queue is full, #IF_HW_FAULT on bad arguments, before
 *         init(), or if the HAL rejected starting the DMA transfer
 */
static IfStatus
stm32_transmit(const uint8_t *data, size_t size, UartTxCompleteCb on_complete, void *ctx)
{
    PendingTx pending           = {.data = data, .size = size, .cb = on_complete, .ctx = ctx};
    bool      start_immediately = false;

    /* The HAL takes a 16-bit length; refuse what it cannot send whole. */
    if (!initialized || data == NULL || size == 0U || size > UINT16_MAX) {
        return IF_HW_FAULT;
    }

    taskENTER_CRITICAL();
    if (!tx_in_flight) {
        tx_in_flight      = true;
        current_tx        = pending;
        start_immediately = true;
    }
    taskEXIT_CRITICAL();

    if (start_immediately) {
        if (HAL_UART_Transmit_DMA(&huart2, current_tx.data, (uint16_t)current_tx.size) != HAL_OK) {
            taskENTER_CRITICAL();
            tx_in_flight = false;
            taskEXIT_CRITICAL();
            return IF_HW_FAULT;
        }
        return IF_OK;
    }

    if (xQueueSend(pending_tx_queue_handle, &pending, 0) != pdTRUE) {
        return IF_BUSY;
    }
    return IF_OK;
}

/**
 * @brief uart_if::start_rx -- start circular DMA reception with idle-line
 * detection. Every chunk of received bytes is handed to @p on_rx from ISR
 * context (HAL_UARTEx_RxEventCallback) until the next init().
 * @param on_rx  receives each chunk; must not be NULL
 * @param ctx    opaque pointer passed back to @p on_rx
 * @return #IF_OK if reception is running, #IF_HW_FAULT on a NULL @p on_rx,
 *         before init(), or if the HAL rejected starting the DMA
 */
static IfStatus stm32_start_rx(UartRxCb on_rx, void *ctx)
{
    if (!initialized || on_rx == NULL) {
        return IF_HW_FAULT;
    }

    /* A second start_rx() replaces the callback; stop the running reception
     * first so the callback never changes under the receive interrupt. */
    if (rx_cb != NULL) {
        (void)HAL_UART_AbortReceive(&huart2);
    }
    rx_ctx = ctx;
    rx_cb  = on_rx;

    if (!stm32_arm_rx()) {
        rx_cb = NULL;
        return IF_HW_FAULT;
    }
    return IF_OK;
}

const UartIf usart_stm32 = {
    .init     = stm32_init,
    .transmit = stm32_transmit,
    .start_rx = stm32_start_rx,
};

/**
 * @brief Baud error from the most recent init(), in parts-per-thousand:
 * (actual - configured) * 1000 / configured. See update_baud_error().
 */
int32_t usart_stm32_baud_error_permille(void)
{
    return baud_error_permille;
}

/**
 * @brief Number of USART overrun errors (ORE) seen since the most recent
 * init(). See HAL_UART_ErrorCallback().
 */
uint32_t usart_stm32_overrun_error_count(void)
{
    return overrun_error_count;
}

/**
 * @brief Number of USART framing errors (FE) seen since the most recent
 * init(). See HAL_UART_ErrorCallback().
 */
uint32_t usart_stm32_framing_error_count(void)
{
    return framing_error_count;
}

/**
 * @brief Number of USART noise errors (NE) seen since the most recent
 * init(). See HAL_UART_ErrorCallback().
 */
uint32_t usart_stm32_noise_error_count(void)
{
    return noise_error_count;
}
