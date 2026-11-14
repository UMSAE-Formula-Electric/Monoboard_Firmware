/**
 * @file test_cli_service.c
 * @brief Exercises the CLI service (production/services/cli_service.h) end to end
 * against usart_fake: bytes in through the receive callback, text out
 * through transmit().
 *
 * usart_fake captures at most USART_FAKE_TX_BYTES_DEPTH (256) bytes, so
 * these tests only use commands with short replies.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "cli_service.h"
#include "test_framework.h"
#include "usart_fake.h"

static bool never_safe(void)
{
    return false;
}

static const CliPorts ports = {
    .is_safe_state = never_safe,
    .gpio          = NULL,
};

static char captured[USART_FAKE_TX_BYTES_DEPTH + 1U];

static bool start(const char *boot_report)
{
    const UartConfig config = {
        .baud_rate_bps = 115200U,
        .data_bits     = 8U,
        .parity        = UART_IF_PARITY_NONE,
        .stop_bits     = UART_STOP_BITS_1,
    };

    usart_fake_reset();
    (void)usart_fake.init(&config);
    cli_set_boot_report(boot_report);
    return cli_init(&usart_fake, &ports) == IF_OK;
}

static void type(const char *text)
{
    (void)usart_fake_inject_rx((const uint8_t *)text, strlen(text));
}

/* Everything transmitted since the last call, as a C string. */
static const char *sent(void)
{
    const size_t len = usart_fake_pop_tx((uint8_t *)captured, USART_FAKE_TX_BYTES_DEPTH);

    captured[len] = '\0';
    return captured;
}

/* Let the fake finish every transmit, as the DMA interrupt would. */
static void finish_tx(void)
{
    while (usart_fake_complete_next_tx()) {
    }
}

TEST(cli_service, init_rejects_missing_arguments)
{
    const CliPorts no_check = {.is_safe_state = NULL, .gpio = NULL};

    CHECK(cli_init(NULL, &ports) == IF_HW_FAULT);
    CHECK(cli_init(&usart_fake, NULL) == IF_HW_FAULT);
    CHECK(cli_init(&usart_fake, &no_check) == IF_HW_FAULT);
}

TEST(cli_service, first_step_sends_boot_report_and_prompt)
{
    CHECK(start("reset=POR\ncrash=none\n"));
    vCliServiceStep(NULL);
    CHECK(strcmp(sent(), "reset=POR\r\ncrash=none\r\nmb> ") == 0);

    /* Nothing typed: nothing more is sent. */
    finish_tx();
    vCliServiceStep(NULL);
    CHECK(usart_fake_tx_byte_count() == 0U);
}

TEST(cli_service, without_boot_report_first_step_sends_only_the_prompt)
{
    CHECK(start(NULL));
    vCliServiceStep(NULL);
    CHECK(strcmp(sent(), "mb> ") == 0);
}

TEST(cli_service, typed_command_is_echoed_and_answered)
{
    CHECK(start(NULL));
    vCliServiceStep(NULL);
    finish_tx();
    (void)sent();

    type("upt");
    vCliServiceStep(NULL);
    finish_tx();
    CHECK(strcmp(sent(), "upt") == 0);

    type("ime\r");
    vCliServiceStep(NULL);
    finish_tx();
    CHECK(strncmp(sent(), "ime\r\nuptime_ms=", 15U) == 0);
    CHECK(strstr(captured, "\r\nOK\r\nmb> ") != NULL);
}

TEST(cli_service, one_command_line_per_step)
{
    CHECK(start(NULL));
    vCliServiceStep(NULL);
    finish_tx();
    (void)sent();

    type("state\rcal\r");
    vCliServiceStep(NULL);
    finish_tx();
    CHECK(strcmp(sent(), "state\r\nERR unavailable: needs #25\r\nmb> ") == 0);
    vCliServiceStep(NULL);
    finish_tx();
    CHECK(strcmp(sent(), "cal\r\nERR unavailable: needs #61\r\nmb> ") == 0);
}

TEST(cli_service, waits_while_both_tx_buffers_are_with_the_driver)
{
    CHECK(start(NULL));
    vCliServiceStep(NULL); /* prompt: buffer 0 busy */
    type("state\r");
    vCliServiceStep(NULL); /* reply: buffer 1 busy */
    (void)sent();

    type("cal\r");
    vCliServiceStep(NULL); /* no free buffer: nothing sent, nothing lost */
    CHECK(usart_fake_tx_byte_count() == 0U);

    finish_tx();
    vCliServiceStep(NULL);
    CHECK(strcmp(sent(), "cal\r\nERR unavailable: needs #61\r\nmb> ") == 0);
}

TEST(cli_service, refused_transmit_is_counted_and_the_buffer_is_released)
{
    CHECK(start(NULL));
    vCliServiceStep(NULL);
    finish_tx();
    (void)sent();

    usart_fake_force_busy();
    type("state\r");
    vCliServiceStep(NULL); /* reply dropped */
    CHECK(usart_fake_tx_byte_count() == 0U);

    type("health\r");
    vCliServiceStep(NULL);
    finish_tx();
    CHECK(strstr(sent(), "tx_dropped=1") != NULL);
}

TEST(cli_service, receive_overflow_is_counted_not_fatal)
{
    uint8_t noise[64];

    (void)memset(noise, 'a', sizeof(noise));
    CHECK(start(NULL));
    vCliServiceStep(NULL);
    finish_tx();
    (void)sent();

    /* 640 bytes into a 512-byte stream buffer with no step in between. */
    for (size_t count = 0U; count < 10U; count++) {
        (void)usart_fake_inject_rx(noise, sizeof(noise));
    }
    /* Drain: the overlong line is discarded, then the CLI answers again. */
    for (size_t count = 0U; count < 40U; count++) {
        vCliServiceStep(NULL);
        finish_tx();
        (void)sent();
    }
    type("\rhealth\r");
    vCliServiceStep(NULL);
    finish_tx();
    (void)sent();
    vCliServiceStep(NULL);
    finish_tx();
    CHECK(strstr(sent(), "rx_dropped=128") != NULL);
}

TEST(cli_service, service_init_brings_up_the_uart_and_starts_the_cli)
{
    static CliService svc;

    CHECK(cli_service_init(NULL) == IF_HW_FAULT);

    usart_fake_reset();
    svc.uart                      = &usart_fake;
    svc.uart_config.baud_rate_bps = 115200U;
    svc.uart_config.data_bits     = 8U;
    svc.uart_config.parity        = UART_IF_PARITY_NONE;
    svc.uart_config.stop_bits     = UART_STOP_BITS_1;
    svc.ports                     = ports;
    cli_set_boot_report(NULL);
    CHECK(cli_service_init(&svc) == IF_OK);

    vCliServiceStep(NULL);
    CHECK(strcmp(sent(), "mb> ") == 0);

    /* A UART that will not come up is reported, not hidden. */
    svc.uart_config.baud_rate_bps = 0U;
    CHECK(cli_service_init(&svc) == IF_HW_FAULT);

    svc.uart = NULL;
    CHECK(cli_service_init(&svc) == IF_HW_FAULT);
}
