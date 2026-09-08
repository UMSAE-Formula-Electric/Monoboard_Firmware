/**
 * @file can_if.h
 * @brief CAN bus contract shared by every CAN driver (real or fake).
 *
 * Header-only, no vendor/HAL/RTOS types allowed here -- see
 * ARCHITECTURE.md, Interface Layer rules. A CanFrame is a plain,
 * chip-agnostic view of a classic (non-FD) CAN frame; the concrete
 * driver behind a CanIf is responsible for translating to/from
 * whatever register or HAL struct its silicon actually wants.
 */
#ifndef CAN_IF_H
#define CAN_IF_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAN_MAX_DLC 8U

typedef enum {
    CAN_OK = 0,
    CAN_ERR_TIMEOUT, /* receive: no frame arrived within timeout_ms */
    CAN_ERR_FULL,    /* send: no free hardware mailbox */
    CAN_ERR_HAL,     /* underlying peripheral/HAL reported a failure */
} CanStatus;

typedef struct {
    uint32_t id;          /* 11-bit standard or 29-bit extended identifier */
    bool     extended_id; /* true: 29-bit ID, false: 11-bit */
    bool     rtr;         /* remote transmission request frame */
    uint8_t  dlc;         /* data length, 0-8 */
    uint8_t  data[CAN_MAX_DLC];
} CanFrame;

typedef struct {
    uint32_t bitrate_bps; /* nominal bit rate, e.g. 500000 for 500 kbit/s */
    bool     loopback;    /* self-test loopback mode, no bus required */
} CanConfig;

/**
 * CAN bus driver contract: the function-pointer table a concrete driver
 * (@c can_stm32, @c can_fake) exports and a composition root injects into
 * the services that need the bus. Every driver, real or fake, honours the
 * semantics below.
 */
typedef struct {
    /**
     * Bring the CAN peripheral up and make it ready to send/receive.
     * @param config  nominal bit rate and loopback selection
     * @return #CAN_OK on success, or #CAN_ERR_HAL if @p config is invalid
     *         (NULL, zero bit rate, rate unreachable from the peripheral
     *         clock) or the underlying peripheral/HAL rejected the setup
     */
    CanStatus (*init)(const CanConfig *config);

    /**
     * Hand one frame to the transmitter. Non-blocking: the call returns as
     * soon as the frame is queued in hardware, not when it reaches the bus.
     * @param frame  frame to transmit; the first @c dlc bytes of @c data are sent
     * @return #CAN_OK if the frame was accepted, #CAN_ERR_FULL if every
     *         hardware mailbox is busy, #CAN_ERR_HAL on a peripheral fault
     *         or if called before init()
     */
    CanStatus (*send)(const CanFrame *frame);

    /**
     * Take the oldest received frame, waiting up to @p timeout_ms for one
     * to arrive.
     * @param      frame       [out] populated with the received frame on #CAN_OK
     * @param      timeout_ms  maximum time to block, in milliseconds (0 = poll)
     * @return #CAN_OK if a frame was written to @p frame, or #CAN_ERR_TIMEOUT
     *         if none arrived within @p timeout_ms
     */
    CanStatus (*receive)(CanFrame *frame, uint32_t timeout_ms);
} CanIf;

#ifdef __cplusplus
}
#endif

#endif /* CAN_IF_H */
