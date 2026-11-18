/**
 * @file can_router_service.c
 * @brief CAN router service (issue #37). See can_router_service.h.
 *
 * State is per bus: the driver, a static TX queue and the counters. The
 * router task is the only caller of a bus's CanIf, so the drivers need no
 * locking. can_router_send() runs in the producers' tasks and only touches
 * the TX queue (itself thread safe) and the tx_dropped counter (under a
 * critical section).
 */
#include "can_router_service.h"

#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

typedef struct {
    const CanIf   *can;
    QueueHandle_t  tx_queue;
    StaticQueue_t  tx_queue_struct;
    uint8_t        tx_queue_storage[CAN_ROUTER_TX_QUEUE_DEPTH * sizeof(CanFrame)];
    CanRouterStats stats;
} RouterBus;

static RouterBus router_buses[CAN_ROUTER_BUS_COUNT];

/* Read by producers in other tasks: false until init has finished. */
static volatile bool router_ready;

static bool bus_is_valid(CanRouterBus bus)
{
    return (unsigned int)bus < (unsigned int)CAN_ROUTER_BUS_COUNT;
}

/* Every check init makes before it touches a driver, so a bad context
 * leaves the buses as they were. */
static bool context_is_valid(const CanRouterService *svc)
{
    size_t i;

    for (i = 0U; i < (size_t)CAN_ROUTER_BUS_COUNT; i++) {
        if (svc->buses[i].can == NULL) {
            return false;
        }
    }
    if (svc->routes == NULL && svc->route_count != 0U) {
        return false;
    }
    for (i = 0U; i < svc->route_count; i++) {
        if (svc->routes[i].handler == NULL || !bus_is_valid(svc->routes[i].bus)) {
            return false;
        }
    }
    return true;
}

/* The queue is created once; a later init (tests, or a re-init) empties it
 * instead, since its storage is static. */
static bool prepare_tx_queue(RouterBus *bus)
{
    if (bus->tx_queue == NULL) {
        bus->tx_queue = xQueueCreateStatic(CAN_ROUTER_TX_QUEUE_DEPTH, sizeof(CanFrame),
                                           bus->tx_queue_storage, &bus->tx_queue_struct);
        return bus->tx_queue != NULL;
    }
    (void)xQueueReset(bus->tx_queue);
    return true;
}

/* cppcheck-suppress constParameterPointer ; signature fixed by PeriodicTask.init */
IfStatus can_router_service_init(void *ctx)
{
    const CanRouterService *svc = (const CanRouterService *)ctx;
    size_t                  i;

    router_ready = false;

    if (svc == NULL || !context_is_valid(svc)) {
        return IF_HW_FAULT;
    }

    for (i = 0U; i < (size_t)CAN_ROUTER_BUS_COUNT; i++) {
        RouterBus     *bus    = &router_buses[i];
        const IfStatus status = svc->buses[i].can->init(&svc->buses[i].config);

        if (status != IF_OK) {
            return status;
        }
        if (!prepare_tx_queue(bus)) {
            return IF_HW_FAULT;
        }
        bus->can = svc->buses[i].can;
        (void)memset(&bus->stats, 0, sizeof(bus->stats));
    }

    router_ready = true;
    return IF_OK;
}

IfStatus can_router_send(CanRouterBus bus, const CanFrame *frame)
{
    RouterBus *state;

    if (frame == NULL || !bus_is_valid(bus) || !router_ready) {
        return IF_HW_FAULT;
    }

    state = &router_buses[bus];
    if (xQueueSend(state->tx_queue, frame, 0) != pdTRUE) {
        /* Drop policy: the new frame goes, the queued ones stay. */
        taskENTER_CRITICAL();
        state->stats.tx_dropped++;
        taskEXIT_CRITICAL();
        return IF_BUSY;
    }
    return IF_OK;
}

bool can_router_get_stats(CanRouterBus bus, CanRouterStats *out)
{
    if (out == NULL) {
        return false;
    }
    if (!bus_is_valid(bus)) {
        (void)memset(out, 0, sizeof(*out));
        return false;
    }

    /* A consistent snapshot: tx_dropped is written from other tasks. */
    taskENTER_CRITICAL();
    *out = router_buses[bus].stats;
    taskEXIT_CRITICAL();
    return true;
}

static bool route_matches(const CanRoute *route, CanRouterBus bus, const CanFrame *frame)
{
    return (route->bus == bus) && (route->extended_id == frame->extended_id)
           && ((frame->id & route->mask) == (route->id & route->mask));
}

/* Hand one received frame to every matching route. */
static void dispatch(const CanRouterService *svc, CanRouterBus bus, const CanFrame *frame)
{
    bool   matched = false;
    size_t i;

    for (i = 0U; i < svc->route_count; i++) {
        const CanRoute *route = &svc->routes[i];

        if (route_matches(route, bus, frame)) {
            route->handler(frame, route->ctx);
            matched = true;
        }
    }
    if (!matched) {
        router_buses[bus].stats.rx_unmatched++;
    }
}

static void drain_rx(const CanRouterService *svc, CanRouterBus bus)
{
    RouterBus *state = &router_buses[bus];
    CanFrame   frame;
    uint32_t   taken;

    for (taken = 0U; taken < CAN_ROUTER_MAX_RX_PER_STEP; taken++) {
        if (state->can->receive(&frame, 0U) != IF_OK) {
            return; /* inbox empty */
        }
        state->stats.rx_frames++;
        dispatch(svc, bus, &frame);
    }
}

/* Peek, then remove only once the driver has taken the frame, so a full
 * set of mailboxes leaves it at the head of the queue for the next step. */
static void drain_tx(CanRouterBus bus)
{
    RouterBus *state = &router_buses[bus];
    CanFrame   frame;

    while (xQueuePeek(state->tx_queue, &frame, 0) == pdTRUE) {
        const IfStatus status = state->can->send(&frame);

        if (status == IF_BUSY) {
            return; /* all mailboxes full: retry next step */
        }
        (void)xQueueReceive(state->tx_queue, &frame, 0);
        if (status == IF_OK) {
            state->stats.tx_frames++;
        } else {
            /* A frame the driver rejects outright is dropped, so one bad
             * frame cannot block the queue behind it. */
            state->stats.tx_errors++;
        }
    }
}

/* cppcheck-suppress constParameterPointer ; signature fixed by PeriodicTask.step */
void vCANRouterServiceStep(void *ctx)
{
    const CanRouterService *svc = (const CanRouterService *)ctx;
    unsigned int            bus;

    if (svc == NULL || !router_ready) {
        return; /* not wired yet: see rtos_system.c */
    }

    for (bus = 0U; bus < (unsigned int)CAN_ROUTER_BUS_COUNT; bus++) {
        drain_rx(svc, (CanRouterBus)bus);
    }
    for (bus = 0U; bus < (unsigned int)CAN_ROUTER_BUS_COUNT; bus++) {
        drain_tx((CanRouterBus)bus);
    }
}
