/* CanIf implementation for ST HAL's CAN1 (bxCAN peripheral).
 *
 * Wiring: CAN1_RX -> PA11, CAN1_TX -> PA12, AF9 (this board's schematic;
 * change here, and only here, if it moves). Bit timing is derived from
 * CanConfig::bitrate_bps at init time using a fixed 16-time-quantum,
 * 87.5% sample point layout, which is the conventional choice for CAN
 * and keeps the interface free of register-level detail.
 *
 * RX follows the pattern mandated by ARCHITECTURE.md: the ISR only
 * copies the frame off the peripheral and hands it to a task via a
 * FreeRTOS queue (FromISR); stm32_receive() blocks on that queue from
 * task context. TX has no software queue -- bxCAN's 3 hardware
 * mailboxes are the buffer, so a full-mailbox condition is reported to
 * the caller immediately as CAN_ERR_FULL rather than retried here.
 */
#include "can_stm32.h"

#include "FreeRTOS.h"
#include "queue.h"

#include "stm32f4xx_hal.h"

#define CAN_STM32_RX_QUEUE_DEPTH 16U
#define CAN_STM32_TIME_QUANTA    16U /* 1 (sync) + BS1 + BS2 */

static CAN_HandleTypeDef hcan1;

static StaticQueue_t rx_queue_struct;
static uint8_t       rx_queue_storage[CAN_STM32_RX_QUEUE_DEPTH * sizeof(CanFrame)];
static QueueHandle_t rx_queue_handle;

static void stm32_configure_gpio_and_clock(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    __HAL_RCC_CAN1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    gpio_init.Pin       = GPIO_PIN_11 | GPIO_PIN_12;
    gpio_init.Mode      = GPIO_MODE_AF_PP;
    gpio_init.Pull      = GPIO_NOPULL;
    gpio_init.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio_init.Alternate = GPIO_AF9_CAN1;
    HAL_GPIO_Init(GPIOA, &gpio_init);
}

void HAL_CAN_MspInit(CAN_HandleTypeDef *hcan)
{
    if (hcan->Instance != CAN1) {
        return;
    }

    stm32_configure_gpio_and_clock();

    /* Priority must be numerically >= configLIBRARY_MAX_SYSCALL_INTERRUPT_
     * PRIORITY so the ISR is allowed to call FromISR FreeRTOS APIs. */
    HAL_NVIC_SetPriority(CAN1_RX0_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(CAN1_RX0_IRQn);
}

void HAL_CAN_MspDeInit(CAN_HandleTypeDef *hcan)
{
    if (hcan->Instance != CAN1) {
        return;
    }

    HAL_NVIC_DisableIRQ(CAN1_RX0_IRQn);
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_11 | GPIO_PIN_12);
    __HAL_RCC_CAN1_CLK_DISABLE();
}

void CAN1_RX0_IRQHandler(void)
{
    HAL_CAN_IRQHandler(&hcan1);
}

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    CAN_RxHeaderTypeDef rx_header;
    CanFrame             frame = {0};
    BaseType_t            higher_priority_task_woken = pdFALSE;

    if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &rx_header, frame.data) != HAL_OK) {
        return;
    }

    frame.extended_id = (rx_header.IDE == CAN_ID_EXT);
    frame.id          = frame.extended_id ? rx_header.ExtId : rx_header.StdId;
    frame.rtr          = (rx_header.RTR == CAN_RTR_REMOTE);
    frame.dlc          = (uint8_t)rx_header.DLC;

    (void)xQueueSendFromISR(rx_queue_handle, &frame, &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

static CanStatus stm32_init(const CanConfig *config)
{
    CAN_FilterTypeDef filter = {0};
    uint32_t           pclk1_hz;
    uint32_t           prescaler;

    if (config == NULL || config->bitrate_bps == 0U) {
        return CAN_ERR_HAL;
    }

    pclk1_hz  = HAL_RCC_GetPCLK1Freq();
    prescaler = pclk1_hz / (config->bitrate_bps * CAN_STM32_TIME_QUANTA);
    if (prescaler == 0U || prescaler > 1024U) {
        return CAN_ERR_HAL; /* bitrate not achievable from this clock */
    }

    hcan1.Instance = CAN1;
    hcan1.Init.Prescaler          = prescaler;
    hcan1.Init.Mode                = config->loopback ? CAN_MODE_LOOPBACK : CAN_MODE_NORMAL;
    hcan1.Init.SyncJumpWidth      = CAN_SJW_1TQ;
    hcan1.Init.TimeSeg1            = CAN_BS1_13TQ;
    hcan1.Init.TimeSeg2            = CAN_BS2_2TQ; /* 1 + 13 + 2 = 16 TQ, 87.5% sample point */
    hcan1.Init.TimeTriggeredMode   = DISABLE;
    hcan1.Init.AutoBusOff           = ENABLE;
    hcan1.Init.AutoWakeUp           = DISABLE;
    hcan1.Init.AutoRetransmission   = ENABLE;
    hcan1.Init.ReceiveFifoLocked    = DISABLE;
    hcan1.Init.TransmitFifoPriority = DISABLE;

    if (HAL_CAN_Init(&hcan1) != HAL_OK) {
        return CAN_ERR_HAL;
    }

    /* Accept everything onto FIFO0: a 32-bit mask filter with mask=0
     * matches every ID. Per-ID filtering can be layered in later
     * without changing the CanIf contract. */
    filter.FilterBank           = 0;
    filter.FilterMode           = CAN_FILTERMODE_IDMASK;
    filter.FilterScale          = CAN_FILTERSCALE_32BIT;
    filter.FilterIdHigh         = 0x0000;
    filter.FilterIdLow          = 0x0000;
    filter.FilterMaskIdHigh     = 0x0000;
    filter.FilterMaskIdLow      = 0x0000;
    filter.FilterFIFOAssignment = CAN_FILTER_FIFO0;
    filter.FilterActivation     = CAN_FILTER_ENABLE;
    filter.SlaveStartFilterBank = 14;

    if (HAL_CAN_ConfigFilter(&hcan1, &filter) != HAL_OK) {
        return CAN_ERR_HAL;
    }

    rx_queue_handle = xQueueCreateStatic(CAN_STM32_RX_QUEUE_DEPTH, sizeof(CanFrame),
                                          rx_queue_storage, &rx_queue_struct);
    if (rx_queue_handle == NULL) {
        return CAN_ERR_HAL;
    }

    if (HAL_CAN_ActivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK) {
        return CAN_ERR_HAL;
    }

    if (HAL_CAN_Start(&hcan1) != HAL_OK) {
        return CAN_ERR_HAL;
    }

    return CAN_OK;
}

static CanStatus stm32_send(const CanFrame *frame)
{
    CAN_TxHeaderTypeDef tx_header = {0};
    uint32_t              tx_mailbox;

    if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan1) == 0U) {
        return CAN_ERR_FULL;
    }

    tx_header.IDE = frame->extended_id ? CAN_ID_EXT : CAN_ID_STD;
    if (frame->extended_id) {
        tx_header.ExtId = frame->id;
    } else {
        tx_header.StdId = frame->id;
    }
    tx_header.RTR = frame->rtr ? CAN_RTR_REMOTE : CAN_RTR_DATA;
    tx_header.DLC = frame->dlc;

    if (HAL_CAN_AddTxMessage(&hcan1, &tx_header, (uint8_t *)frame->data, &tx_mailbox) != HAL_OK) {
        return CAN_ERR_HAL;
    }

    return CAN_OK;
}

static CanStatus stm32_receive(CanFrame *frame, uint32_t timeout_ms)
{
    if (xQueueReceive(rx_queue_handle, frame, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return CAN_ERR_TIMEOUT;
    }
    return CAN_OK;
}

const CanIf can_stm32 = {
    .init    = stm32_init,
    .send    = stm32_send,
    .receive = stm32_receive,
};
