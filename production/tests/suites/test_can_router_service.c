/**
 * @file test_can_router_service.c
 * @brief Exercises the CAN router (production/services/can_router_service.h)
 * through main_can_fake and sensor_can_fake: inject frames, step, check what
 * reached the route handlers and what went out on each bus.
 *
 * can_fake holds CAN_FAKE_QUEUE_DEPTH (16) frames per direction, fewer
 * than the router's TX queue, so a full fake stands in for "every hardware
 * mailbox busy".
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "can_fake.h"
#include "can_heartbeat.h"
#include "can_router_service.h"
#include "test_framework.h"

/* What one route handler has seen. */
typedef struct {
    uint32_t calls;
    CanFrame last;
} Sink;

static void record(const CanFrame *frame, void *ctx)
{
    Sink *sink = (Sink *)ctx;

    sink->calls++;
    sink->last = *frame;
}

static Sink motor_sink;  /* main bus, standard ID 0x100 exactly */
static Sink bms_sink;    /* main bus, standard IDs 0x200..0x20F */
static Sink sensor_sink; /* sensor bus, standard ID 0x100 exactly */
static Sink ext_sink;    /* main bus, extended ID 0x18FF0001 */
static Sink logger_sink; /* main bus, every standard ID 0x2xx */

static const CanRoute routes[] = {
    {CAN_ROUTER_BUS_MAIN, false, 0x100U, 0x7FFU, record, &motor_sink},
    {CAN_ROUTER_BUS_MAIN, false, 0x200U, 0x7F0U, record, &bms_sink},
    {CAN_ROUTER_BUS_SENSOR, false, 0x100U, 0x7FFU, record, &sensor_sink},
    {CAN_ROUTER_BUS_MAIN, true, 0x18FF0001U, 0x1FFFFFFFU, record, &ext_sink},
    {CAN_ROUTER_BUS_MAIN, false, 0x200U, 0x700U, record, &logger_sink},
};

static CanRouterService svc;

static void wire(void)
{
    const CanConfig config = {.bitrate_bps = 500000U, .loopback = false};

    (void)memset(&svc, 0, sizeof(svc));
    svc.buses[CAN_ROUTER_BUS_MAIN].can      = &main_can_fake;
    svc.buses[CAN_ROUTER_BUS_MAIN].config   = config;
    svc.buses[CAN_ROUTER_BUS_SENSOR].can    = &sensor_can_fake;
    svc.buses[CAN_ROUTER_BUS_SENSOR].config = config;
    svc.routes                              = routes;
    svc.route_count                         = sizeof(routes) / sizeof(routes[0]);
}

/* Fresh fakes, fresh sinks, router up with the table above. */
static bool start(void)
{
    can_fake_reset(&main_can_fake);
    can_fake_reset(&sensor_can_fake);
    (void)memset(&motor_sink, 0, sizeof(motor_sink));
    (void)memset(&bms_sink, 0, sizeof(bms_sink));
    (void)memset(&sensor_sink, 0, sizeof(sensor_sink));
    (void)memset(&ext_sink, 0, sizeof(ext_sink));
    (void)memset(&logger_sink, 0, sizeof(logger_sink));
    wire();
    return can_router_service_init(&svc) == IF_OK;
}

static CanFrame std_frame(uint32_t id, uint8_t first_byte)
{
    CanFrame frame = {0};

    frame.id      = id;
    frame.dlc     = 1U;
    frame.data[0] = first_byte;
    return frame;
}

static CanRouterStats stats(CanRouterBus bus)
{
    CanRouterStats out;

    (void)can_router_get_stats(bus, &out);
    return out;
}

/* --- init ------------------------------------------------------------- */

TEST(can_router_service, init_rejects_bad_context)
{
    static const CanRoute no_handler[] = {
        {CAN_ROUTER_BUS_MAIN, false, 0x1U, 0x7FFU, NULL, NULL},
    };
    static const CanRoute bad_bus[] = {
        {CAN_ROUTER_BUS_COUNT, false, 0x1U, 0x7FFU, record, &motor_sink},
    };

    CHECK(can_router_service_init(NULL) == IF_HW_FAULT);

    wire();
    svc.buses[CAN_ROUTER_BUS_SENSOR].can = NULL;
    CHECK(can_router_service_init(&svc) == IF_HW_FAULT);

    wire();
    svc.routes = NULL;
    CHECK(can_router_service_init(&svc) == IF_HW_FAULT);

    wire();
    svc.routes      = no_handler;
    svc.route_count = 1U;
    CHECK(can_router_service_init(&svc) == IF_HW_FAULT);

    wire();
    svc.routes      = bad_bus;
    svc.route_count = 1U;
    CHECK(can_router_service_init(&svc) == IF_HW_FAULT);
}

TEST(can_router_service, init_accepts_empty_routing_table)
{
    wire();
    svc.routes      = NULL;
    svc.route_count = 0U;
    CHECK(can_router_service_init(&svc) == IF_OK);
}

TEST(can_router_service, init_brings_up_both_buses)
{
    const CanFrame frame = std_frame(0x1U, 0U);
    CanFrame       out;

    CHECK(start());
    /* A fake that was never init()ed rejects send with IF_HW_FAULT. */
    CHECK(main_can_fake.send(&frame) == IF_OK);
    CHECK(sensor_can_fake.send(&frame) == IF_OK);
    CHECK(can_fake_pop_tx(&main_can_fake, &out));
    CHECK(can_fake_pop_tx(&sensor_can_fake, &out));
}

/* --- RX dispatch ------------------------------------------------------- */

TEST(can_router_service, exact_id_reaches_its_route_only)
{
    const CanFrame frame = std_frame(0x100U, 0xAAU);

    CHECK(start());
    can_fake_inject_rx(&main_can_fake, &frame);
    vCANRouterServiceStep(&svc);

    CHECK(motor_sink.calls == 1U);
    CHECK(motor_sink.last.data[0] == 0xAAU);
    CHECK(sensor_sink.calls == 0U); /* same ID, other bus */
    CHECK(bms_sink.calls == 0U);
    CHECK(stats(CAN_ROUTER_BUS_MAIN).rx_frames == 1U);
    CHECK(stats(CAN_ROUTER_BUS_MAIN).rx_unmatched == 0U);
}

TEST(can_router_service, routes_are_per_bus)
{
    const CanFrame frame = std_frame(0x100U, 0x55U);

    CHECK(start());
    can_fake_inject_rx(&sensor_can_fake, &frame);
    vCANRouterServiceStep(&svc);

    CHECK(sensor_sink.calls == 1U);
    CHECK(motor_sink.calls == 0U);
    CHECK(stats(CAN_ROUTER_BUS_SENSOR).rx_frames == 1U);
    CHECK(stats(CAN_ROUTER_BUS_MAIN).rx_frames == 0U);
}

TEST(can_router_service, mask_matches_a_range_and_every_match_is_called)
{
    const CanFrame in_range  = std_frame(0x20AU, 1U);
    const CanFrame out_range = std_frame(0x210U, 2U); /* logger only */

    CHECK(start());
    can_fake_inject_rx(&main_can_fake, &in_range);
    can_fake_inject_rx(&main_can_fake, &out_range);
    vCANRouterServiceStep(&svc);

    CHECK(bms_sink.calls == 1U);
    CHECK(bms_sink.last.id == 0x20AU);
    CHECK(logger_sink.calls == 2U);
    CHECK(logger_sink.last.id == 0x210U);
}

TEST(can_router_service, standard_and_extended_ids_do_not_mix)
{
    CanFrame ext = std_frame(0x18FF0001U, 0U);
    CanFrame std = std_frame(0x100U, 0U);

    ext.extended_id = true;
    std.extended_id = true; /* 0x100 as a 29-bit ID: no route */

    CHECK(start());
    can_fake_inject_rx(&main_can_fake, &ext);
    can_fake_inject_rx(&main_can_fake, &std);
    vCANRouterServiceStep(&svc);

    CHECK(ext_sink.calls == 1U);
    CHECK(motor_sink.calls == 0U);
    CHECK(stats(CAN_ROUTER_BUS_MAIN).rx_unmatched == 1U);
}

TEST(can_router_service, unknown_id_is_counted)
{
    const CanFrame frame = std_frame(0x7FFU, 0U);

    CHECK(start());
    can_fake_inject_rx(&main_can_fake, &frame);
    can_fake_inject_rx(&sensor_can_fake, &frame);
    vCANRouterServiceStep(&svc);

    CHECK(stats(CAN_ROUTER_BUS_MAIN).rx_unmatched == 1U);
    CHECK(stats(CAN_ROUTER_BUS_SENSOR).rx_unmatched == 1U);
    CHECK(stats(CAN_ROUTER_BUS_MAIN).rx_frames == 1U);
}

TEST(can_router_service, step_drains_a_full_inbox)
{
    const CanFrame frame = std_frame(0x100U, 0U);
    uint32_t       i;

    CHECK(start());
    for (i = 0U; i < CAN_FAKE_QUEUE_DEPTH; i++) {
        can_fake_inject_rx(&main_can_fake, &frame);
    }
    vCANRouterServiceStep(&svc);
    CHECK(motor_sink.calls == CAN_FAKE_QUEUE_DEPTH);
}

/* --- TX ---------------------------------------------------------------- */

TEST(can_router_service, send_rejects_bad_arguments_and_before_init)
{
    const CanFrame frame = std_frame(0x1U, 0U);

    /* A failed init leaves the router not ready. */
    CHECK(can_router_service_init(NULL) == IF_HW_FAULT);
    CHECK(can_router_send(CAN_ROUTER_BUS_MAIN, &frame) == IF_HW_FAULT);

    CHECK(start());
    CHECK(can_router_send(CAN_ROUTER_BUS_MAIN, NULL) == IF_HW_FAULT);
    CHECK(can_router_send(CAN_ROUTER_BUS_COUNT, &frame) == IF_HW_FAULT);
}

TEST(can_router_service, send_goes_out_on_its_bus_in_order)
{
    const CanFrame first  = std_frame(0x301U, 1U);
    const CanFrame second = std_frame(0x302U, 2U);
    const CanFrame sensor = std_frame(0x400U, 3U);
    CanFrame       out;

    CHECK(start());
    CHECK(can_router_send(CAN_ROUTER_BUS_MAIN, &first) == IF_OK);
    CHECK(can_router_send(CAN_ROUTER_BUS_MAIN, &second) == IF_OK);
    CHECK(can_router_send(CAN_ROUTER_BUS_SENSOR, &sensor) == IF_OK);

    /* Nothing reaches the bus until the router's task runs. */
    CHECK(can_fake_tx_count(&main_can_fake) == 0U);
    vCANRouterServiceStep(&svc);

    CHECK(can_fake_tx_count(&main_can_fake) == 2U);
    CHECK(can_fake_pop_tx(&main_can_fake, &out) && out.id == 0x301U);
    CHECK(can_fake_pop_tx(&main_can_fake, &out) && out.id == 0x302U);
    CHECK(can_fake_pop_tx(&sensor_can_fake, &out) && out.id == 0x400U);
    CHECK(stats(CAN_ROUTER_BUS_MAIN).tx_frames == 2U);
    CHECK(stats(CAN_ROUTER_BUS_SENSOR).tx_frames == 1U);
}

TEST(can_router_service, full_tx_queue_drops_new_frame_and_keeps_queued_ones)
{
    CanFrame frame;
    CanFrame out;
    uint32_t i;

    CHECK(start());
    for (i = 0U; i < CAN_ROUTER_TX_QUEUE_DEPTH; i++) {
        frame = std_frame(0x500U + i, 0U);
        CHECK(can_router_send(CAN_ROUTER_BUS_MAIN, &frame) == IF_OK);
    }
    frame = std_frame(0x7FFU, 0U);
    CHECK(can_router_send(CAN_ROUTER_BUS_MAIN, &frame) == IF_BUSY);
    CHECK(stats(CAN_ROUTER_BUS_MAIN).tx_dropped == 1U);
    CHECK(stats(CAN_ROUTER_BUS_SENSOR).tx_dropped == 0U);

    /* Drain everything (the fake takes 16 per step) and check the order:
     * every queued frame went out, the dropped one never did. */
    for (i = 0U; i < CAN_ROUTER_TX_QUEUE_DEPTH; i++) {
        if (can_fake_tx_count(&main_can_fake) == 0U) {
            vCANRouterServiceStep(&svc);
        }
        CHECK(can_fake_pop_tx(&main_can_fake, &out));
        CHECK(out.id == 0x500U + i);
    }
    vCANRouterServiceStep(&svc);
    CHECK(can_fake_tx_count(&main_can_fake) == 0U);
    CHECK(stats(CAN_ROUTER_BUS_MAIN).tx_frames == CAN_ROUTER_TX_QUEUE_DEPTH);
}

TEST(can_router_service, busy_mailboxes_hold_frames_for_next_step)
{
    CanFrame frame;
    CanFrame out;
    uint32_t i;

    CHECK(start());
    for (i = 0U; i < CAN_FAKE_QUEUE_DEPTH + 2U; i++) {
        frame = std_frame(0x600U + i, 0U);
        CHECK(can_router_send(CAN_ROUTER_BUS_MAIN, &frame) == IF_OK);
    }

    vCANRouterServiceStep(&svc); /* fills the fake, then it reports busy */
    CHECK(can_fake_tx_count(&main_can_fake) == CAN_FAKE_QUEUE_DEPTH);
    CHECK(stats(CAN_ROUTER_BUS_MAIN).tx_errors == 0U);

    (void)can_fake_pop_tx(&main_can_fake, &out); /* the "bus" frees one mailbox... */
    vCANRouterServiceStep(&svc);
    CHECK(can_fake_tx_count(&main_can_fake) == CAN_FAKE_QUEUE_DEPTH);

    /* ...and the next frame in line, not a later one, took it. */
    for (i = 1U; i < CAN_FAKE_QUEUE_DEPTH + 1U; i++) {
        CHECK(can_fake_pop_tx(&main_can_fake, &out) && out.id == 0x600U + i);
    }
}

TEST(can_router_service, rejected_frame_is_counted_and_dropped)
{
    const CanFrame frame = std_frame(0x700U, 0U);

    CHECK(start());
    can_fake_reset(&main_can_fake); /* un-initialises it: send -> IF_HW_FAULT */
    CHECK(can_router_send(CAN_ROUTER_BUS_MAIN, &frame) == IF_OK);
    vCANRouterServiceStep(&svc);

    CHECK(stats(CAN_ROUTER_BUS_MAIN).tx_errors == 1U);
    CHECK(stats(CAN_ROUTER_BUS_MAIN).tx_frames == 0U);

    /* Dropped, not retried: a recovered bus does not get it later. */
    CHECK(main_can_fake.init(&svc.buses[CAN_ROUTER_BUS_MAIN].config) == IF_OK);
    vCANRouterServiceStep(&svc);
    CHECK(can_fake_tx_count(&main_can_fake) == 0U);
}

/* --- stats and the step's guards --------------------------------------- */

TEST(can_router_service, init_zeroes_counters)
{
    const CanFrame frame = std_frame(0x7FFU, 0U);

    CHECK(start());
    can_fake_inject_rx(&main_can_fake, &frame);
    vCANRouterServiceStep(&svc);
    CHECK(stats(CAN_ROUTER_BUS_MAIN).rx_unmatched == 1U);

    CHECK(can_router_service_init(&svc) == IF_OK);
    CHECK(stats(CAN_ROUTER_BUS_MAIN).rx_unmatched == 0U);
    CHECK(stats(CAN_ROUTER_BUS_MAIN).rx_frames == 0U);
}

TEST(can_router_service, get_stats_rejects_bad_arguments)
{
    CanRouterStats out;

    (void)memset(&out, 0xFF, sizeof(out));
    CHECK(!can_router_get_stats(CAN_ROUTER_BUS_COUNT, &out));
    CHECK(out.rx_frames == 0U && out.tx_dropped == 0U);
    CHECK(!can_router_get_stats(CAN_ROUTER_BUS_MAIN, NULL));
}

TEST(can_router_service, step_without_context_does_nothing)
{
    const CanFrame frame = std_frame(0x100U, 0U);

    CHECK(start());
    can_fake_inject_rx(&main_can_fake, &frame);
    vCANRouterServiceStep(NULL); /* rtos_system.c's unwired task */
    CHECK(motor_sink.calls == 0U);
    vCANRouterServiceStep(&svc);
    CHECK(motor_sink.calls == 1U);
}

/* --- heartbeat --------------------------------------------------------- */

static void enable_heartbeat(uint32_t every_steps)
{
    svc.heartbeat.enabled     = true;
    svc.heartbeat.bus         = CAN_ROUTER_BUS_SENSOR;
    svc.heartbeat.can_id      = 0x7F0U;
    svc.heartbeat.every_steps = every_steps;
}

TEST(can_router_service, heartbeat_is_off_by_default)
{
    uint32_t i;

    CHECK(start());
    for (i = 0U; i < 10U; i++) {
        vCANRouterServiceStep(&svc);
    }
    CHECK(can_fake_tx_count(&sensor_can_fake) == 0U);
    CHECK(can_fake_tx_count(&main_can_fake) == 0U);
}

TEST(can_router_service, init_rejects_bad_heartbeat)
{
    wire();
    enable_heartbeat(0U);
    CHECK(can_router_service_init(&svc) == IF_HW_FAULT);

    wire();
    enable_heartbeat(1U);
    svc.heartbeat.can_id = 0x800U;
    CHECK(can_router_service_init(&svc) == IF_HW_FAULT);

    wire();
    enable_heartbeat(1U);
    svc.heartbeat.bus = CAN_ROUTER_BUS_COUNT;
    CHECK(can_router_service_init(&svc) == IF_HW_FAULT);

    /* Disabled, the other fields are not checked. */
    wire();
    svc.heartbeat.can_id = 0x800U;
    CHECK(can_router_service_init(&svc) == IF_OK);
}

TEST(can_router_service, heartbeat_goes_out_every_n_steps_with_a_sequence)
{
    uint32_t step;
    uint32_t sent = 0U;
    CanFrame out;
    uint8_t  sequence;
    uint32_t uptime;

    CHECK(start());
    enable_heartbeat(4U);
    CHECK(can_router_service_init(&svc) == IF_OK);

    /* Steps 0, 4 and 8 each put one heartbeat on the bus that same step. */
    for (step = 0U; step < 12U; step++) {
        vCANRouterServiceStep(&svc);
        if (step % 4U == 0U) {
            CHECK(can_fake_pop_tx(&sensor_can_fake, &out));
            CHECK(out.id == 0x7F0U);
            CHECK(can_heartbeat_decode(&out, &sequence, &uptime));
            CHECK(sequence == (uint8_t)sent);
            sent++;
        }
        CHECK(can_fake_tx_count(&sensor_can_fake) == 0U);
    }
    CHECK(sent == 3U);
    CHECK(can_fake_tx_count(&main_can_fake) == 0U); /* only its own bus */
    CHECK(stats(CAN_ROUTER_BUS_SENSOR).tx_frames == 3U);
}

TEST(can_router_service, heartbeat_shares_the_queue_with_producers)
{
    const CanFrame frame = std_frame(0x123U, 0U);
    CanFrame       out;

    CHECK(start());
    enable_heartbeat(100U);
    CHECK(can_router_service_init(&svc) == IF_OK);

    /* Queued before the step: it goes out first, then the heartbeat. */
    CHECK(can_router_send(CAN_ROUTER_BUS_SENSOR, &frame) == IF_OK);
    vCANRouterServiceStep(&svc);
    CHECK(can_fake_pop_tx(&sensor_can_fake, &out) && out.id == 0x123U);
    CHECK(can_fake_pop_tx(&sensor_can_fake, &out) && out.id == 0x7F0U);
}

TEST(can_router_service, refused_heartbeat_is_counted_and_leaves_a_gap)
{
    const CanFrame filler = std_frame(0x123U, 0U);
    CanFrame       out;
    bool           seen  = false;
    uint8_t        first = 0U;
    uint32_t       i;

    CHECK(start());
    enable_heartbeat(1U);
    CHECK(can_router_service_init(&svc) == IF_OK);

    /* A full sensor-bus queue refuses heartbeat 0. */
    for (i = 0U; i < CAN_ROUTER_TX_QUEUE_DEPTH; i++) {
        CHECK(can_router_send(CAN_ROUTER_BUS_SENSOR, &filler) == IF_OK);
    }
    vCANRouterServiceStep(&svc);
    CHECK(stats(CAN_ROUTER_BUS_SENSOR).tx_dropped == 1U);

    /* Keep the "bus" draining until a heartbeat comes out: it is number 1,
     * so the refused one shows as a gap. */
    for (i = 0U; i < 8U && !seen; i++) {
        while (can_fake_pop_tx(&sensor_can_fake, &out)) {
            if (!seen && out.id == 0x7F0U) {
                CHECK(can_heartbeat_decode(&out, &first, NULL));
                seen = true;
            }
        }
        vCANRouterServiceStep(&svc);
    }
    CHECK(seen);
    CHECK(first == 1U);
}
