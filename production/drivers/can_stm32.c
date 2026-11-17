/**
 * @file can_stm32.c
 * @brief CanIf implementations for ST HAL's CAN1 and CAN2 (bxCAN peripherals).
 *
 * Two buses, one driver body: each CanIf table (main_can_stm32,
 * sensor_can_stm32) is a set of thin wrappers that hand a per-bus
 * CanStm32Bus to the shared implementation.
 *
 * Wiring (this board's schematic; change here, and only here, if it moves):
 *   - main bus:   CAN1_RX -> PA11, CAN1_TX -> PA12, AF9
 *   - sensor bus: CAN2_RX -> PB5,  CAN2_TX -> PB6,  AF9
 *
 * Bit timing is derived from CanConfig::bitrate_bps at init time using a
 * fixed 16-time-quantum, 87.5% sample point layout, which is the
 * conventional choice for CAN and keeps the interface free of
 * register-level detail. Both peripherals sit on APB1.
 *
 * CAN2 is a slave of CAN1 on the F4: it has no filter banks of its own
 * (all 28 live in CAN1, split at CAN_STM32_SLAVE_START_BANK) and needs
 * CAN1's clock running to work at all. So CAN1's clock is reference-
 * counted across both buses, and each bus owns one filter bank on its
 * side of the split.
 *
 * RX follows the pattern mandated by ARCHITECTURE.md: the ISR only
 * copies the frame off the peripheral and hands it to a task via a
 * FreeRTOS queue (FromISR); receive() blocks on that queue from task
 * context. Each bus has its own queue. TX has no software queue --
 * bxCAN's 3 hardware mailboxes are the buffer, so a full-mailbox
 * condition is reported to the caller immediately as IF_BUSY rather
 * than retried here.
 */
#include "can_stm32.h"

#include "FreeRTOS.h"
#include "queue.h"

#include "stm32f4xx_hal.h"

#define CAN_STM32_RX_QUEUE_DEPTH   16U
#define CAN_STM32_TIME_QUANTA      16U /* 1 (sync) + BS1 + BS2 */
#define CAN_STM32_SLAVE_START_BANK 14U /* banks 0..13 -> CAN1, 14..27 -> CAN2 */

typedef struct {
    /* Fixed board wiring. */
    CAN_TypeDef  *instance;
    GPIO_TypeDef *gpio_port;
    uint16_t      gpio_pins;
    uint8_t       gpio_af;
    IRQn_Type     rx0_irq;
    uint32_t      filter_bank;

    /* Runtime state. */
    CAN_HandleTypeDef hcan;
    StaticQueue_t     rx_queue_struct;
    uint8_t           rx_queue_storage[CAN_STM32_RX_QUEUE_DEPTH * sizeof(CanFrame)];
    QueueHandle_t     rx_queue_handle;
    bool              clock_held; /* this bus holds a CAN1 clock reference */
} CanStm32Bus;

static CanStm32Bus main_bus = {
    .instance    = CAN1,
    .gpio_port   = GPIOA,
    .gpio_pins   = GPIO_PIN_11 | GPIO_PIN_12,
    .gpio_af     = GPIO_AF9_CAN1,
    .rx0_irq     = CAN1_RX0_IRQn,
    .filter_bank = 0U,
};

static CanStm32Bus sensor_bus = {
    .instance    = CAN2,
    .gpio_port   = GPIOB,
    .gpio_pins   = GPIO_PIN_5 | GPIO_PIN_6,
    .gpio_af     = GPIO_AF9_CAN2,
    .rx0_irq     = CAN2_RX0_IRQn,
    .filter_bank = CAN_STM32_SLAVE_START_BANK,
};

/* Number of buses currently holding CAN1's clock (CAN2 needs it too). */
static uint8_t can1_clock_users;

static CanStm32Bus *bus_from_handle(const CAN_HandleTypeDef *hcan)
{
    if (hcan->Instance == CAN1) {
        return &main_bus;
    }
    if (hcan->Instance == CAN2) {
        return &sensor_bus;
    }
    return NULL;
}

static void can1_clock_acquire(CanStm32Bus *bus)
{
    if (bus->clock_held) {
        return;
    }
    if (can1_clock_users == 0U) {
        __HAL_RCC_CAN1_CLK_ENABLE();
    }
    can1_clock_users++;
    bus->clock_held = true;
}

static void can1_clock_release(CanStm32Bus *bus)
{
    if (!bus->clock_held) {
        return;
    }
    bus->clock_held = false;
    can1_clock_users--;
    if (can1_clock_users == 0U) {
        __HAL_RCC_CAN1_CLK_DISABLE();
    }
}

static void enable_gpio_port_clock(const GPIO_TypeDef *port)
{
    if (port == GPIOA) {
        __HAL_RCC_GPIOA_CLK_ENABLE();
    } else if (port == GPIOB) {
        __HAL_RCC_GPIOB_CLK_ENABLE();
    } else {
        /* Only ports used by the wiring table above are handled. */
    }
}

/* cppcheck-suppress constParameterPointer ; signature fixed by stm32f4xx_hal_can.h */
void HAL_CAN_MspInit(CAN_HandleTypeDef *hcan)
{
    CanStm32Bus     *bus       = bus_from_handle(hcan);
    GPIO_InitTypeDef gpio_init = {0};

    if (bus == NULL) {
        return;
    }

    can1_clock_acquire(bus);
    if (bus->instance == CAN2) {
        __HAL_RCC_CAN2_CLK_ENABLE();
    }
    enable_gpio_port_clock(bus->gpio_port);

    gpio_init.Pin       = bus->gpio_pins;
    gpio_init.Mode      = GPIO_MODE_AF_PP;
    gpio_init.Pull      = GPIO_NOPULL;
    gpio_init.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio_init.Alternate = bus->gpio_af;
    HAL_GPIO_Init(bus->gpio_port, &gpio_init);

    /* Priority must be numerically >= configLIBRARY_MAX_SYSCALL_INTERRUPT_
     * PRIORITY so the ISR is allowed to call FromISR FreeRTOS APIs. */
    HAL_NVIC_SetPriority(bus->rx0_irq, 5, 0);
    HAL_NVIC_EnableIRQ(bus->rx0_irq);
}

/* cppcheck-suppress constParameterPointer ; signature fixed by stm32f4xx_hal_can.h */
void HAL_CAN_MspDeInit(CAN_HandleTypeDef *hcan)
{
    CanStm32Bus *bus = bus_from_handle(hcan);

    if (bus == NULL) {
        return;
    }

    HAL_NVIC_DisableIRQ(bus->rx0_irq);
    HAL_GPIO_DeInit(bus->gpio_port, bus->gpio_pins);
    if (bus->instance == CAN2) {
        __HAL_RCC_CAN2_CLK_DISABLE();
    }
    can1_clock_release(bus);
}

void CAN1_RX0_IRQHandler(void)
{
    HAL_CAN_IRQHandler(&main_bus.hcan);
}

void CAN2_RX0_IRQHandler(void)
{
    HAL_CAN_IRQHandler(&sensor_bus.hcan);
}

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    const CanStm32Bus  *bus = bus_from_handle(hcan);
    CAN_RxHeaderTypeDef rx_header;
    CanFrame            frame                      = {0};
    BaseType_t          higher_priority_task_woken = pdFALSE;

    if (bus == NULL || bus->rx_queue_handle == NULL) {
        return;
    }

    if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &rx_header, frame.data) != HAL_OK) {
        return;
    }

    frame.extended_id = (rx_header.IDE == CAN_ID_EXT);
    frame.id          = frame.extended_id ? rx_header.ExtId : rx_header.StdId;
    frame.rtr         = (rx_header.RTR == CAN_RTR_REMOTE);
    frame.dlc         = (uint8_t)rx_header.DLC;

    (void)xQueueSendFromISR(bus->rx_queue_handle, &frame, &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

static IfStatus bus_init(CanStm32Bus *bus, const CanConfig *config)
{
    CAN_HandleTypeDef *hcan   = &bus->hcan;
    CAN_FilterTypeDef  filter = {0};
    uint32_t           pclk1_hz;
    uint32_t           prescaler;

    if (config == NULL || config->bitrate_bps == 0U) {
        return IF_HW_FAULT;
    }

    pclk1_hz  = HAL_RCC_GetPCLK1Freq();
    prescaler = pclk1_hz / (config->bitrate_bps * CAN_STM32_TIME_QUANTA);
    if (prescaler == 0U || prescaler > 1024U) {
        return IF_HW_FAULT; /* bitrate not achievable from this clock */
    }

    hcan->Instance                  = bus->instance;
    hcan->Init.Prescaler            = prescaler;
    hcan->Init.Mode                 = config->loopback ? CAN_MODE_LOOPBACK : CAN_MODE_NORMAL;
    hcan->Init.SyncJumpWidth        = CAN_SJW_1TQ;
    hcan->Init.TimeSeg1             = CAN_BS1_13TQ;
    hcan->Init.TimeSeg2             = CAN_BS2_2TQ; /* 1 + 13 + 2 = 16 TQ, 87.5% sample point */
    hcan->Init.TimeTriggeredMode    = DISABLE;
    hcan->Init.AutoBusOff           = ENABLE;
    hcan->Init.AutoWakeUp           = DISABLE;
    hcan->Init.AutoRetransmission   = ENABLE;
    hcan->Init.ReceiveFifoLocked    = DISABLE;
    hcan->Init.TransmitFifoPriority = DISABLE;

    if (HAL_CAN_Init(hcan) != HAL_OK) {
        return IF_HW_FAULT;
    }

    /* Accept everything onto FIFO0: a 32-bit mask filter with mask=0
     * matches every ID. Per-ID filtering can be layered in later
     * without changing the CanIf contract. The banks are shared with
     * the other bus (they live in CAN1), so each bus touches only its
     * own bank, and both write the same slave start bank. */
    filter.FilterBank           = bus->filter_bank;
    filter.FilterMode           = CAN_FILTERMODE_IDMASK;
    filter.FilterScale          = CAN_FILTERSCALE_32BIT;
    filter.FilterIdHigh         = 0x0000;
    filter.FilterIdLow          = 0x0000;
    filter.FilterMaskIdHigh     = 0x0000;
    filter.FilterMaskIdLow      = 0x0000;
    filter.FilterFIFOAssignment = CAN_FILTER_FIFO0;
    filter.FilterActivation     = CAN_FILTER_ENABLE;
    filter.SlaveStartFilterBank = CAN_STM32_SLAVE_START_BANK;

    if (HAL_CAN_ConfigFilter(hcan, &filter) != HAL_OK) {
        return IF_HW_FAULT;
    }

    if (bus->rx_queue_handle == NULL) {
        bus->rx_queue_handle = xQueueCreateStatic(CAN_STM32_RX_QUEUE_DEPTH, sizeof(CanFrame),
                                                  bus->rx_queue_storage, &bus->rx_queue_struct);
        if (bus->rx_queue_handle == NULL) {
            return IF_HW_FAULT;
        }
    }

    if (HAL_CAN_ActivateNotification(hcan, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK) {
        return IF_HW_FAULT;
    }

    if (HAL_CAN_Start(hcan) != HAL_OK) {
        return IF_HW_FAULT;
    }

    return IF_OK;
}

static IfStatus bus_send(CanStm32Bus *bus, const CanFrame *frame)
{
    CAN_TxHeaderTypeDef tx_header = {0};
    uint32_t            tx_mailbox;

    /* Also covers "never initialised": the handle's Instance is still
     * NULL then, and the HAL would dereference it. */
    if (bus->hcan.State != HAL_CAN_STATE_LISTENING) {
        return IF_HW_FAULT;
    }

    if (HAL_CAN_GetTxMailboxesFreeLevel(&bus->hcan) == 0U) {
        return IF_BUSY;
    }

    tx_header.IDE = frame->extended_id ? CAN_ID_EXT : CAN_ID_STD;
    if (frame->extended_id) {
        tx_header.ExtId = frame->id;
    } else {
        tx_header.StdId = frame->id;
    }
    tx_header.RTR = frame->rtr ? CAN_RTR_REMOTE : CAN_RTR_DATA;
    tx_header.DLC = frame->dlc;

    if (HAL_CAN_AddTxMessage(&bus->hcan, &tx_header, (uint8_t *)frame->data, &tx_mailbox)
        != HAL_OK) {
        return IF_HW_FAULT;
    }

    return IF_OK;
}

static IfStatus bus_receive(const CanStm32Bus *bus, CanFrame *frame, uint32_t timeout_ms)
{
    if (bus->rx_queue_handle == NULL) {
        return IF_TIMEOUT; /* not initialised: nothing can ever arrive */
    }
    if (xQueueReceive(bus->rx_queue_handle, frame, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return IF_TIMEOUT;
    }
    return IF_OK;
}

/* --- main bus (CAN1) --- */

static IfStatus main_init(const CanConfig *config)
{
    return bus_init(&main_bus, config);
}

static IfStatus main_send(const CanFrame *frame)
{
    return bus_send(&main_bus, frame);
}

static IfStatus main_receive(CanFrame *frame, uint32_t timeout_ms)
{
    return bus_receive(&main_bus, frame, timeout_ms);
}

const CanIf main_can_stm32 = {
    .init    = main_init,
    .send    = main_send,
    .receive = main_receive,
};

/* --- sensor bus (CAN2) --- */

static IfStatus sensor_init(const CanConfig *config)
{
    return bus_init(&sensor_bus, config);
}

static IfStatus sensor_send(const CanFrame *frame)
{
    return bus_send(&sensor_bus, frame);
}

static IfStatus sensor_receive(CanFrame *frame, uint32_t timeout_ms)
{
    return bus_receive(&sensor_bus, frame, timeout_ms);
}

const CanIf sensor_can_stm32 = {
    .init    = sensor_init,
    .send    = sensor_send,
    .receive = sensor_receive,
};
