/**
 * @file can_router_service.h
 * @brief CAN router service (issue #37): the single owner of both CAN buses.
 *
 * No other service touches a CanIf. Received frames are dispatched to
 * subscribers through a routing table; frames to transmit are handed to the
 * router with can_router_send() and go out from the router's own task.
 *
 * A periodic service like the others in rtos_system.c: a context struct, an
 * init hook that brings up the buses it owns, and a step function. There is
 * one router, so the context only carries the wiring and the state (TX
 * queues, counters) lives in can_router_service.c.
 *
 * RX path. The CAN driver's ISR copies each frame off the peripheral into
 * the driver's own RX queue -- that queue is the router's inbox, so the ISR
 * does no parsing and only calls FromISR APIs. Each step drains every bus's
 * inbox (up to #CAN_ROUTER_MAX_RX_PER_STEP frames per bus, so a flooded bus
 * cannot starve the other) and calls the handler of every route that matches
 * the frame. A frame no route matches is counted in
 * CanRouterStats::rx_unmatched, not dropped silently: an unexpected ID on
 * the bus is diagnostic information.
 *
 * TX path. can_router_send() copies the frame onto that bus's TX queue and
 * returns at once; no service ever blocks on a mailbox. Each step moves
 * frames from the TX queue into the hardware mailboxes until the driver
 * reports IF_BUSY (all three full); the rest wait for the next step, in
 * order.
 *
 * Drop policy (decided here, not at the call site): when a bus's TX queue
 * is full, the frame being sent is dropped, counted in
 * CanRouterStats::tx_dropped, and can_router_send() returns #IF_BUSY. Frames
 * already queued are kept, so older frames are never displaced by newer
 * ones. Callers must not retry in a loop.
 *
 * Heartbeat. Optionally (CanRouterService::heartbeat, on in Debug builds
 * only) the router queues its own heartbeat frame (can_heartbeat.h) every
 * N steps, through can_router_send() like any producer. On a bench, a
 * steady heartbeat with a gap-free sequence on the analyser proves the
 * router task transmits; the `can` CLI command shows the counters.
 *
 * SEAM: a TX drop and the bus error states (error-warning, error-passive,
 * bus-off) become fault sources once the fault service exists (#59). CanIf
 * has no error-state query yet; bus-off recovery (a backoff state machine,
 * not a blind re-init loop) arrives with it, and its counters join
 * CanRouterStats for Healthrun.
 */
#ifndef CAN_ROUTER_SERVICE_H
#define CAN_ROUTER_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "can_if.h"
#include "if_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Depth of each bus's TX queue, in frames. Exposed so tests can flood it
 *  without hardcoding the number. PLACEHOLDER until #37's burst
 *  measurement sets it. */
#define CAN_ROUTER_TX_QUEUE_DEPTH 32U

/** Most frames taken off one bus's inbox in a single step. */
#define CAN_ROUTER_MAX_RX_PER_STEP 32U

/** The buses the router owns. Indexes CanRouterService::buses. */
typedef enum {
    CAN_ROUTER_BUS_MAIN = 0, /**< main vehicle bus (CAN1) */
    CAN_ROUTER_BUS_SENSOR,   /**< sensor bus (CAN2) */
    CAN_ROUTER_BUS_COUNT,
} CanRouterBus;

/**
 * Called for a received frame that matches a route. Runs in the router
 * task, so it must not block and must be short: typically it copies the
 * frame onto its service's own queue with a zero timeout.
 * @param frame  the received frame; valid only for the duration of the call
 * @param ctx    the route's @c ctx
 */
typedef void (*CanRouteHandler)(const CanFrame *frame, void *ctx);

/**
 * One entry of the routing table. A frame matches when it arrived on
 * @c bus, its ID kind equals @c extended_id, and
 * `(frame->id & mask) == (id & mask)`. A mask of 0 matches every ID of that
 * kind (e.g. for a logger).
 */
typedef struct {
    CanRouterBus    bus;
    bool            extended_id; /**< true: 29-bit ID, false: 11-bit */
    uint32_t        id;
    uint32_t        mask;
    CanRouteHandler handler; /**< must not be NULL */
    void           *ctx;     /**< passed to @c handler */
} CanRoute;

/** Wiring for one bus. */
typedef struct {
    const CanIf *can;    /**< driver for the bus; must outlive the router */
    CanConfig    config; /**< bit rate and loopback selection for it */
} CanRouterBusConfig;

/** Optional heartbeat the router sends itself (see the file comment).
 *  All zero means off. */
typedef struct {
    bool         enabled;     /**< false: no heartbeat, the rest is ignored */
    CanRouterBus bus;         /**< bus to send it on */
    uint32_t     can_id;      /**< standard 11-bit ID, from can_ids.h */
    uint32_t     every_steps; /**< router steps between frames; not 0 */
} CanRouterHeartbeat;

/** CAN router context. The composition root owns the storage and fills in
 *  every field before the task that runs can_router_service_init() is
 *  created. */
typedef struct {
    CanRouterBusConfig buses[CAN_ROUTER_BUS_COUNT]; /**< indexed by CanRouterBus */
    const CanRoute    *routes;      /**< routing table, typically const in flash; may be
                                         NULL if @c route_count is 0 */
    size_t             route_count; /**< entries in @c routes */
    CanRouterHeartbeat heartbeat;   /**< off unless @c heartbeat.enabled */
} CanRouterService;

/** Per-bus counters. They only grow, and wrap at UINT32_MAX. */
typedef struct {
    uint32_t rx_frames;    /**< frames taken off the inbox */
    uint32_t rx_unmatched; /**< received frames no route matched */
    uint32_t tx_frames;    /**< frames handed to the hardware mailboxes */
    uint32_t tx_dropped;   /**< frames dropped because the TX queue was full */
    uint32_t tx_errors;    /**< frames the driver rejected with #IF_HW_FAULT */
} CanRouterStats;

/**
 * Init hook for rtos_system.c's PeriodicTask: bring up every bus, create
 * the TX queues and zero the counters. Runs once, in the router task,
 * before the first step.
 * @param ctx  CanRouterService with every field set
 * @return #IF_OK, the CAN driver's init() error, or #IF_HW_FAULT if
 *         @p ctx, any bus's @c can, or a route's @c handler is NULL, a route
 *         names a bus outside CanRouterBus, @c routes is NULL with a
 *         non-zero @c route_count, an enabled heartbeat has a bad bus, an
 *         ID that does not fit in 11 bits or @c every_steps of 0, or a TX
 *         queue could not be created
 */
IfStatus can_router_service_init(void *ctx);

/**
 * Queue a frame for transmission on @p bus. Non-blocking; task context
 * only (not from an ISR). The frame is copied, so @p frame may be reused at
 * once.
 * @param bus    bus to send on
 * @param frame  frame to send
 * @return #IF_OK if queued, #IF_BUSY if that bus's TX queue was full (the
 *         frame was dropped and counted -- see the drop policy above), or
 *         #IF_HW_FAULT if @p frame is NULL, @p bus is out of range, or the
 *         router has not been initialised
 */
IfStatus can_router_send(CanRouterBus bus, const CanFrame *frame);

/**
 * Copy one bus's counters.
 * @param bus  bus to read
 * @param out  [out] the counters; zeroed if @p bus is out of range
 * @return true if @p bus is valid and @p out is not NULL
 */
bool can_router_get_stats(CanRouterBus bus, CanRouterStats *out);

/** One period of the router: drain each bus's inbox and dispatch, queue
 *  the heartbeat if one is due, then drain each TX queue into the
 *  mailboxes. Run from the periodic task in
 *  rtos_system.c.
 *  @param ctx  the CanRouterService passed to can_router_service_init() */
void vCANRouterServiceStep(void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* CAN_ROUTER_SERVICE_H */
