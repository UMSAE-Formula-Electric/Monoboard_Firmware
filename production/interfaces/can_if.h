/* CAN bus contract shared by every CAN driver (real or fake).
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
    uint32_t id;           /* 11-bit standard or 29-bit extended identifier */
    bool     extended_id;  /* true: 29-bit ID, false: 11-bit */
    bool     rtr;           /* remote transmission request frame */
    uint8_t  dlc;            /* data length, 0-8 */
    uint8_t  data[CAN_MAX_DLC];
} CanFrame;

typedef struct {
    uint32_t bitrate_bps; /* nominal bit rate, e.g. 500000 for 500 kbit/s */
    bool     loopback;    /* self-test loopback mode, no bus required */
} CanConfig;

typedef struct {
    CanStatus (*init)(const CanConfig *config);
    CanStatus (*send)(const CanFrame *frame);
    CanStatus (*receive)(CanFrame *frame, uint32_t timeout_ms);
} CanIf;

#ifdef __cplusplus
}
#endif

#endif /* CAN_IF_H */
