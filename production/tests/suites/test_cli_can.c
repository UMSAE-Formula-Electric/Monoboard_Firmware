/**
 * @file test_cli_can.c
 * @brief Exercises the CLI's CAN commands end to end: `can` (router
 * counters, every build) and `cantx` (Debug only), through cli_core and the
 * CAN router to sensor_can_fake.
 *
 * The unit tests build as Debug, so `cantx` is linked in; the tests still
 * skip it when cli_debug_command_count is 0, as the `set` tests do.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "can_fake.h"
#include "can_router_service.h"
#include "cli_core.h"
#include "test_framework.h"

#define OUT_SIZE 1024U

static CliCore          core;
static CliOut           out;
static char             out_buf[OUT_SIZE + 1U];
static CanRouterService router;

/* Fresh CLI (no ports: every write is refused) and a running router. */
static bool start(void)
{
    const CanConfig config = {.bitrate_bps = 500000U, .loopback = false};

    cli_core_init(&core);
    (void)memset(&router, 0, sizeof(router));
    router.buses[CAN_ROUTER_BUS_MAIN].can      = &main_can_fake;
    router.buses[CAN_ROUTER_BUS_MAIN].config   = config;
    router.buses[CAN_ROUTER_BUS_SENSOR].can    = &sensor_can_fake;
    router.buses[CAN_ROUTER_BUS_SENSOR].config = config;
    return can_router_service_init(&router) == IF_OK;
}

/* Run one line on a fresh output buffer; returns the output text. */
static const char *run(const char *line)
{
    cli_out_init(&out, out_buf, OUT_SIZE);
    (void)cli_core_feed(&core, (const uint8_t *)line, strlen(line), &out);
    out_buf[out.len] = '\0';
    return out_buf;
}

static bool ends_with(const char *text, const char *tail)
{
    const size_t text_len = strlen(text);
    const size_t tail_len = strlen(tail);

    return (text_len >= tail_len) && (strcmp(&text[text_len - tail_len], tail) == 0);
}

/* --- can ---------------------------------------------------------------- */

TEST(cli_can, can_prints_both_buses_counters)
{
    const CanFrame frame = {.id = 0x7FFU, .dlc = 0U};

    CHECK(start());
    can_fake_inject_rx(&main_can_fake, &frame); /* no routes: unmatched */
    CHECK(can_router_send(CAN_ROUTER_BUS_SENSOR, &frame) == IF_OK);
    vCANRouterServiceStep(&router);

    CHECK(strcmp(run("can\r"),
                 "can\r\n"
                 "bus=main rx_frames=1 rx_unmatched=1 tx_frames=0 tx_dropped=0 tx_errors=0\r\n"
                 "bus=sensor rx_frames=0 rx_unmatched=0 tx_frames=1 tx_dropped=0 tx_errors=0\r\n"
                 "OK\r\nmb> ")
          == 0);
}

TEST(cli_can, can_is_not_a_write)
{
    /* No ports, so any write would be refused. `can` must still answer. */
    CHECK(start());
    CHECK(ends_with(run("can\r"), "OK\r\nmb> "));
}

/* --- cantx -------------------------------------------------------------- */

TEST(cli_can, cantx_sends_on_the_sensor_bus_only)
{
    CanFrame frame;

    CHECK(start());
    if (cli_debug_command_count > 0U) {
        CHECK(strcmp(run("cantx 7F1 DEADBEEF\r"),
                     "cantx 7F1 DEADBEEF\r\nqueued bus=sensor id=0x7F1 dlc=4\r\nOK\r\nmb> ")
              == 0);
        vCANRouterServiceStep(&router);

        CHECK(can_fake_tx_count(&main_can_fake) == 0U);
        CHECK(can_fake_pop_tx(&sensor_can_fake, &frame));
        CHECK(frame.id == 0x7F1U);
        CHECK(!frame.extended_id);
        CHECK(frame.dlc == 4U);
        CHECK(frame.data[0] == 0xDEU && frame.data[1] == 0xADU);
        CHECK(frame.data[2] == 0xBEU && frame.data[3] == 0xEFU);
    }
}

TEST(cli_can, cantx_accepts_0x_prefix_and_no_data)
{
    CanFrame frame;

    CHECK(start());
    if (cli_debug_command_count > 0U) {
        CHECK(ends_with(run("cantx 0x12\r"), "queued bus=sensor id=0x012 dlc=0\r\nOK\r\nmb> "));
        vCANRouterServiceStep(&router);
        CHECK(can_fake_pop_tx(&sensor_can_fake, &frame));
        CHECK(frame.id == 0x12U && frame.dlc == 0U);
    }
}

TEST(cli_can, cantx_works_without_a_safe_state)
{
    /* Not a write: the fail-closed safe-state check must not block it. */
    CHECK(start());
    if (cli_debug_command_count > 0U) {
        CHECK(ends_with(run("cantx 100\r"), "OK\r\nmb> "));
    }
}

TEST(cli_can, cantx_rejects_bad_arguments)
{
    CHECK(start());
    if (cli_debug_command_count > 0U) {
        CHECK(ends_with(run("cantx\r"), "ERR usage: cantx <id> [hexdata]\r\nmb> "));
        CHECK(ends_with(run("cantx 800\r"), "ERR id must be hex, at most 7FF\r\nmb> "));
        CHECK(ends_with(run("cantx 1234\r"), "ERR id must be hex, at most 7FF\r\nmb> "));
        CHECK(ends_with(run("cantx 0x\r"), "ERR id must be hex, at most 7FF\r\nmb> "));
        CHECK(ends_with(run("cantx 7G\r"), "ERR id must be hex, at most 7FF\r\nmb> "));
        CHECK(ends_with(run("cantx 100 ABC\r"), "ERR data must be 0-8 bytes of hex pairs\r\nmb> "));
        CHECK(ends_with(run("cantx 100 ZZ\r"), "ERR data must be 0-8 bytes of hex pairs\r\nmb> "));
        CHECK(ends_with(run("cantx 100 001122334455667788\r"),
                        "ERR data must be 0-8 bytes of hex pairs\r\nmb> "));
        vCANRouterServiceStep(&router);
        CHECK(can_fake_tx_count(&sensor_can_fake) == 0U); /* nothing was queued */
    }
}

TEST(cli_can, cantx_reports_a_full_queue)
{
    const CanFrame frame = {.id = 0x1U, .dlc = 0U};

    CHECK(start());
    if (cli_debug_command_count > 0U) {
        uint32_t i;

        for (i = 0U; i < CAN_ROUTER_TX_QUEUE_DEPTH; i++) {
            CHECK(can_router_send(CAN_ROUTER_BUS_SENSOR, &frame) == IF_OK);
        }
        CHECK(ends_with(run("cantx 100\r"), "ERR router tx queue full, frame dropped\r\nmb> "));
    }
}

TEST(cli_can, cantx_reports_router_not_running)
{
    CHECK(start());
    if (cli_debug_command_count > 0U) {
        CHECK(can_router_service_init(NULL) == IF_HW_FAULT); /* router now down */
        CHECK(ends_with(run("cantx 100\r"), "ERR unavailable: can router not running\r\nmb> "));
    }
}
