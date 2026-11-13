/**
 * @file usart_stm32.c
 * @brief uart_if implementation for ST HAL's USART2 peripheral.
 *
 * Wiring: USART2 in asynchronous mode, TX on SERIAL_TX (PA2) and RX on
 * SERIAL_RX (PA3), AF7 -- the pads come from the .ioc via main.h, so moving
 * them in CubeMX and regenerating follows through here. TX DMA is on DMA1
 * Stream6 Channel4 (the F446's fixed DMA request map). Frame format and
 * baud rate come from UartConfig at init time.
 *
 * TX: non-blocking, DMA-driven, with a software queue of pending buffers
 * (issue #16). transmit() either kicks a DMA transfer immediately (nothing
 * in flight) or enqueues the buffer; HAL_UART_TxCpltCallback (ISR context)
 * fires the caller's completion callback for the buffer that just finished
 * and starts the next queued one, so tx-complete fires exactly once per
 * accepted transmit() call. A full queue returns IF_BUSY without blocking
 * or dropping, per the contract.
 *
 * RX is not implemented -- deferred per issue #16 until the debug CLI needs
 * it, at which point it should be DMA + idle-line detection, not byte
 * interrupts. The peripheral already runs in TX_RX mode so adding RX does
 * not change init(). HAL_UART only enables the error interrupts while a
 * receive is running, so ORE/FE/NE are counted (see
 * usart_stm32_*_error_count()) once RX exists; until then the counters
 * stay at zero.
 */
#include "usart_stm32.h"

#include <stdbool.h>

#include "FreeRTOS.h"
#include "queue.h"

#include "main.h" /* CubeMX-generated per-pin macros: SERIAL_TX_Pin, ... */
#include "stm32f4xx_hal.h"

#define USART_STM32_TX_PENDING_DEPTH 8U

typedef struct {
    const uint8_t   *data;
    size_t           size;
    UartTxCompleteCb cb;
    void            *ctx;
} PendingTx;

static UART_HandleTypeDef huart2;
static DMA_HandleTypeDef  hdma_usart2_tx;

static StaticQueue_t pending_tx_queue_struct;
static uint8_t       pending_tx_queue_storage[USART_STM32_TX_PENDING_DEPTH * sizeof(PendingTx)];
static QueueHandle_t pending_tx_queue_handle;

/* The transfer currently owned by the DMA/peripheral, if any; only ever
 * touched with interrupts masked (taskENTER_CRITICAL / from ISR context). */
static volatile bool tx_in_flight;
static PendingTx     current_tx;

static volatile uint32_t overrun_error_count;
static volatile uint32_t framing_error_count;
static volatile uint32_t noise_error_count;
static int32_t           baud_error_permille;

/**
 * @brief Enable the clocks USART2/GPIOA/DMA1 need and configure the TX/RX
 * pads for USART2's alternate function (AF7).
 */
static void stm32_configure_gpio_and_clock(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();

    gpio_init.Pin       = SERIAL_TX_Pin | SERIAL_RX_Pin;
    gpio_init.Mode      = GPIO_MODE_AF_PP;
    gpio_init.Pull      = GPIO_NOPULL;
    gpio_init.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio_init.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(SERIAL_TX_GPIO_Port, &gpio_init);
}

/**
 * @brief HAL MSP hook: called by HAL_UART_Init() to bring up everything the
 * peripheral itself doesn't own -- GPIO alternate function, the TX DMA
 * stream, and NVIC priority/enable for both the USART and DMA IRQs.
 * @param huart   handle being initialised; ignored unless it is USART2
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

    /* Priorities must be numerically >= configLIBRARY_MAX_SYSCALL_INTERRUPT_
     * PRIORITY so these ISRs are allowed to call FromISR FreeRTOS APIs. */
    HAL_NVIC_SetPriority(DMA1_Stream6_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream6_IRQn);
    HAL_NVIC_SetPriority(USART2_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
}

/**
 * @brief HAL MSP hook: called by HAL_UART_DeInit() to undo everything
 * HAL_UART_MspInit() set up -- NVIC, DMA, GPIO, and the peripheral clock.
 * @param huart   handle being de-initialised; ignored unless it is USART2
 */
/* cppcheck-suppress constParameterPointer ; signature fixed by stm32f4xx_hal_uart.h */
void HAL_UART_MspDeInit(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART2) {
        return;
    }

    HAL_NVIC_DisableIRQ(USART2_IRQn);
    HAL_NVIC_DisableIRQ(DMA1_Stream6_IRQn);
    (void)HAL_DMA_DeInit(&hdma_usart2_tx);
    HAL_GPIO_DeInit(SERIAL_TX_GPIO_Port, SERIAL_TX_Pin | SERIAL_RX_Pin);
    __HAL_RCC_USART2_CLK_DISABLE();
}

/**
 * @brief USART2 interrupt vector: hands off to the HAL, which decodes the
 * cause and dispatches to the matching HAL_UART_*Callback().
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
        (void)HAL_UART_Transmit_DMA(&huart2, (uint8_t *)current_tx.data, (uint16_t)current_tx.size);
    }
}

/**
 * @brief HAL callback: fires once the DMA has fully clocked out the current
 * transfer. Reports completion to that buffer's own on_complete (exactly
 * once, per the uart_if.h contract) and immediately starts the next queued
 * transfer, if any.
 * @param huart   handle that completed; ignored unless it is USART2
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
 * @brief HAL callback: fires on any USART/DMA error. Counts overrun/
 * framing/noise errors (see usart_stm32_*_error_count()), and if the fault
 * was on the DMA side -- meaning the in-flight transfer is dead and will
 * never complete on its own -- recovers by starting the next queued
 * transfer instead of leaving TX wedged.
 * @param huart   handle that faulted; ignored unless it is USART2
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
        case UART_PARITY_MODE_EVEN:
            return UART_PARITY_EVEN;
        case UART_PARITY_MODE_ODD:
            return UART_PARITY_ODD;
        case UART_PARITY_MODE_NONE:
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
 * UART_WORDLENGTH_*, which counts the parity bit as part of the frame --
 * so 8 data bits with parity enabled needs a 9-bit HAL word length.
 * @param data_bits  bits per frame requested via UartConfig, excluding parity
 */
static uint32_t hal_word_length(uint8_t data_bits)
{
    return (data_bits >= 9U) ? UART_WORDLENGTH_9B : UART_WORDLENGTH_8B;
}

/**
 * @brief Read back the BRR the HAL actually programmed and decode the baud
 * rate it produces, rather than re-deriving ST's rounding independently.
 * init() selects 16x oversampling (OVER8=0), so BRR holds 16*USARTDIV
 * directly (mantissa in bits[15:4], 4-bit fraction in bits[3:0]) and
 * baud = pclk / BRR.
 * Stores the result in baud_error_permille for usart_stm32_baud_error_permille().
 * @param configured_baud_bps  the baud rate that was requested via init()
 */
static void update_baud_error(uint32_t configured_baud_bps)
{
    uint32_t pclk_hz      = HAL_RCC_GetPCLK1Freq();
    uint32_t usartdiv_x16 = huart2.Instance->BRR & 0xFFFFU;

    if (usartdiv_x16 == 0U || configured_baud_bps == 0U) {
        baud_error_permille = 0;
        return;
    }

    uint32_t actual_baud_bps = pclk_hz / usartdiv_x16;
    int64_t  error_x1000     = ((int64_t)actual_baud_bps - (int64_t)configured_baud_bps) * 1000;
    baud_error_permille      = (int32_t)(error_x1000 / (int64_t)configured_baud_bps);
}

/**
 * @brief uart_if::init -- program USART2 for the requested frame format and
 * baud rate, record the resulting baud error, and allocate the pending-TX
 * queue.
 * @param config  frame format and baud rate; NULL or a zero baud rate fails
 * @return #IF_OK on success, #IF_HW_FAULT if @p config is invalid or the
 *         HAL rejected the setup
 */
static IfStatus stm32_init(const UartConfig *config)
{
    if (config == NULL || config->baud_rate_bps == 0U) {
        return IF_HW_FAULT;
    }

    huart2.Instance          = USART2;
    huart2.Init.BaudRate     = config->baud_rate_bps;
    huart2.Init.WordLength   = hal_word_length(config->data_bits);
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
 *         pending queue is full, #IF_HW_FAULT on bad arguments or if the
 *         HAL rejected starting the DMA transfer
 */
static IfStatus
stm32_transmit(const uint8_t *data, size_t size, UartTxCompleteCb on_complete, void *ctx)
{
    PendingTx pending           = {.data = data, .size = size, .cb = on_complete, .ctx = ctx};
    bool      start_immediately = false;

    if (data == NULL || size == 0U) {
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
        if (HAL_UART_Transmit_DMA(&huart2, (uint8_t *)current_tx.data, (uint16_t)current_tx.size)
            != HAL_OK) {
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

const UartIf usart_stm32 = {
    .init     = stm32_init,
    .transmit = stm32_transmit,
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
