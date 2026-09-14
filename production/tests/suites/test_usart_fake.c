/**
 * @file test_usart_fake.c
 * @brief Exercises the UartIf contract (production/interfaces/uart_if.h)
 * through usart_fake, the dependency-free double for usart_stm32.
 */
#include <stddef.h>
#include <stdint.h>

#include "test_framework.h"
#include "usart_fake.h"

typedef struct {
    int    call_count;
    void  *last_ctx;
    size_t last_bytes;
} CompletionRecord;

static CompletionRecord completion;

static void on_complete(void *ctx, size_t bytes_transmitted)
{
    completion.call_count++;
    completion.last_ctx   = ctx;
    completion.last_bytes = bytes_transmitted;
}

static void completion_reset(void)
{
    completion.call_count = 0;
    completion.last_ctx   = NULL;
    completion.last_bytes = 0U;
}

static UartConfig make_config(void)
{
    UartConfig config = {
        .baud_rate_bps = 115200,
        .data_bits     = 8U,
        .parity        = UART_PARITY_NONE,
        .stop_bits     = UART_STOP_BITS_1,
    };
    return config;
}

TEST(usart_fake, transmit_before_init_fails)
{
    const uint8_t data[] = {1, 2, 3};

    usart_fake_reset();
    completion_reset();
    CHECK(usart_fake.transmit(data, sizeof(data), on_complete, NULL) == IF_HW_FAULT);
    CHECK(usart_fake_call_count() == 0U);
}

TEST(usart_fake, init_rejects_null_or_zero_baud)
{
    UartConfig zero_baud    = make_config();
    zero_baud.baud_rate_bps = 0U;

    usart_fake_reset();
    CHECK(usart_fake.init(NULL) == IF_HW_FAULT);
    CHECK(usart_fake.init(&zero_baud) == IF_HW_FAULT);
}

TEST(usart_fake, init_then_transmit_is_captured)
{
    UartConfig    config               = make_config();
    const uint8_t data[]               = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t       popped[sizeof(data)] = {0};
    UsartFakeCall call;

    usart_fake_reset();
    completion_reset();
    CHECK(usart_fake.init(&config) == IF_OK);

    CHECK(usart_fake.transmit(data, sizeof(data), on_complete, (void *)0x1234) == IF_OK);

    CHECK(usart_fake_call_count() == 1U);
    CHECK(usart_fake_pop_call(&call) == true);
    CHECK(call.size == sizeof(data));
    CHECK(call.ctx == (void *)0x1234);
    CHECK(usart_fake_call_count() == 0U);

    CHECK(usart_fake_tx_byte_count() == sizeof(data));
    CHECK(usart_fake_pop_tx(popped, sizeof(popped)) == sizeof(data));
    CHECK(popped[0] == 0xDE && popped[1] == 0xAD && popped[2] == 0xBE && popped[3] == 0xEF);
    CHECK(usart_fake_tx_byte_count() == 0U);

    /* Accepted, but nothing fires until the test says so. */
    CHECK(completion.call_count == 0);
}

TEST(usart_fake, transmit_defers_completion_until_test_fires_it)
{
    UartConfig    config = make_config();
    const uint8_t data[] = {7, 8, 9};

    usart_fake_reset();
    completion_reset();
    CHECK(usart_fake.init(&config) == IF_OK);

    CHECK(usart_fake.transmit(data, sizeof(data), on_complete, (void *)0xABCD) == IF_OK);
    CHECK(usart_fake_pending_tx_count() == 1U);
    CHECK(completion.call_count == 0);

    CHECK(usart_fake_complete_next_tx() == true);
    CHECK(completion.call_count == 1);
    CHECK(completion.last_ctx == (void *)0xABCD);
    CHECK(completion.last_bytes == sizeof(data));
    CHECK(usart_fake_pending_tx_count() == 0U);

    /* Nothing left to complete. */
    CHECK(usart_fake_complete_next_tx() == false);
}

TEST(usart_fake, completions_fire_in_fifo_order)
{
    UartConfig    config   = make_config();
    const uint8_t first[]  = {1};
    const uint8_t second[] = {2};

    usart_fake_reset();
    completion_reset();
    CHECK(usart_fake.init(&config) == IF_OK);

    CHECK(usart_fake.transmit(first, sizeof(first), on_complete, (void *)1) == IF_OK);
    CHECK(usart_fake.transmit(second, sizeof(second), on_complete, (void *)2) == IF_OK);

    CHECK(usart_fake_complete_next_tx() == true);
    CHECK(completion.last_ctx == (void *)1);

    CHECK(usart_fake_complete_next_tx() == true);
    CHECK(completion.last_ctx == (void *)2);
}

TEST(usart_fake, transmit_returns_busy_when_pending_queue_full)
{
    UartConfig    config = make_config();
    const uint8_t data[] = {0xFF};
    size_t        i;

    usart_fake_reset();
    completion_reset();
    CHECK(usart_fake.init(&config) == IF_OK);

    for (i = 0; i < USART_FAKE_PENDING_DEPTH; i++) {
        CHECK(usart_fake.transmit(data, sizeof(data), NULL, NULL) == IF_OK);
    }
    CHECK(usart_fake.transmit(data, sizeof(data), NULL, NULL) == IF_BUSY);
}

TEST(usart_fake, force_busy_applies_once)
{
    UartConfig    config = make_config();
    const uint8_t data[] = {0x01};

    usart_fake_reset();
    CHECK(usart_fake.init(&config) == IF_OK);

    usart_fake_force_busy();
    CHECK(usart_fake.transmit(data, sizeof(data), NULL, NULL) == IF_BUSY);
    CHECK(usart_fake.transmit(data, sizeof(data), NULL, NULL) == IF_OK);
}

TEST(usart_fake, force_hw_fault_applies_once)
{
    UartConfig    config = make_config();
    const uint8_t data[] = {0x01};

    usart_fake_reset();
    CHECK(usart_fake.init(&config) == IF_OK);

    usart_fake_force_hw_fault();
    CHECK(usart_fake.transmit(data, sizeof(data), NULL, NULL) == IF_HW_FAULT);
    CHECK(usart_fake.transmit(data, sizeof(data), NULL, NULL) == IF_OK);
}

TEST(usart_fake, transmit_rejects_null_data_or_zero_size)
{
    UartConfig    config = make_config();
    const uint8_t data[] = {0x01};

    usart_fake_reset();
    CHECK(usart_fake.init(&config) == IF_OK);

    CHECK(usart_fake.transmit(NULL, sizeof(data), NULL, NULL) == IF_HW_FAULT);
    CHECK(usart_fake.transmit(data, 0U, NULL, NULL) == IF_HW_FAULT);
}

TEST(usart_fake, reset_clears_init_call_log_and_pending_state)
{
    UartConfig    config = make_config();
    const uint8_t data[] = {0x01, 0x02};

    usart_fake_reset();
    CHECK(usart_fake.init(&config) == IF_OK);
    CHECK(usart_fake.transmit(data, sizeof(data), NULL, NULL) == IF_OK);

    usart_fake_reset();

    CHECK(usart_fake_call_count() == 0U);
    CHECK(usart_fake_tx_byte_count() == 0U);
    CHECK(usart_fake_pending_tx_count() == 0U);
    CHECK(usart_fake.transmit(data, sizeof(data), NULL, NULL) == IF_HW_FAULT);
}
