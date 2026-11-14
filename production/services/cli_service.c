/**
 * @file cli_service.c
 * @brief Debug CLI service (issue #51). See cli_service.h.
 */
#include "cli_service.h"

#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "stream_buffer.h"

/* At 115200 baud at most 230 bytes arrive per 20 ms step, so 512 holds two
 * full periods before anything is dropped. */
#define CLI_RX_STREAM_BYTES 512U

/* `help` and `log` print about a dozen lines each: more than 256 bytes. */
#define CLI_TX_BUFFER_BYTES 1024U
#define CLI_TX_BUFFER_COUNT 2U

/* Bytes taken out of the stream buffer at a time. */
#define CLI_RX_CHUNK_BYTES 32U

static const UartIf *cli_uart;
static const char   *cli_boot_report; /* survives cli_init() on purpose */
static CliCore       cli_core;

static StreamBufferHandle_t cli_rx_stream;
static StaticStreamBuffer_t cli_rx_stream_control;
static uint8_t              cli_rx_stream_storage[CLI_RX_STREAM_BYTES + 1U];

/* Written by the receive interrupt, read by the step. */
static volatile uint32_t cli_rx_dropped;

/* A transmit buffer belongs to the driver from transmit() until its
 * completion callback clears the flag. */
static char          cli_tx_buffer[CLI_TX_BUFFER_COUNT][CLI_TX_BUFFER_BYTES];
static volatile bool cli_tx_busy[CLI_TX_BUFFER_COUNT];

/* Received bytes taken out of the stream buffer but not fed yet. */
static uint8_t cli_chunk[CLI_RX_CHUNK_BYTES];
static size_t  cli_chunk_len;
static size_t  cli_chunk_pos;

static bool cli_banner_sent;

/* Interrupt context: copy and return. */
static void cli_on_rx(void *ctx, const uint8_t *data, size_t len)
{
    size_t stored;

    (void)ctx;
    stored = xStreamBufferSendFromISR(cli_rx_stream, data, len, NULL);
    if (stored < len) {
        cli_rx_dropped += (uint32_t)(len - stored);
    }
}

/* Interrupt context: the driver is done with this buffer. */
static void cli_on_tx_done(void *ctx, size_t bytes_transmitted)
{
    (void)bytes_transmitted;
    *(volatile bool *)ctx = false;
}

/* The boot report (LF line ends) and the first prompt, as CR LF text. */
static void cli_write_banner(CliOut *out)
{
    const char *text = cli_boot_report;

    if (text != NULL) {
        size_t line_start = 0U;
        size_t pos;

        for (pos = 0U; text[pos] != '\0'; pos++) {
            if (text[pos] == '\n') {
                cli_out_printf(out, "%.*s" CLI_EOL, (int)(pos - line_start), &text[line_start]);
                line_start = pos + 1U;
            }
        }
        if (pos > line_start) {
            cli_out_printf(out, "%.*s" CLI_EOL, (int)(pos - line_start), &text[line_start]);
        }
    }
    cli_out_printf(out, "%s", CLI_PROMPT);
}

/* Feed received bytes until one line is answered, the input runs out, or
 * the output has no room left. Unfed bytes stay in cli_chunk. */
static void cli_feed_received(CliOut *out)
{
    while (!out->reply_ready) {
        size_t consumed;

        if (cli_chunk_pos == cli_chunk_len) {
            cli_chunk_len = xStreamBufferReceive(cli_rx_stream, cli_chunk, sizeof(cli_chunk), 0U);
            cli_chunk_pos = 0U;
            if (cli_chunk_len == 0U) {
                break;
            }
        }
        consumed = cli_core_feed(&cli_core, &cli_chunk[cli_chunk_pos],
                                 cli_chunk_len - cli_chunk_pos, out);
        cli_chunk_pos += consumed;
        if (consumed == 0U) {
            break; /* output full: send it, continue next step */
        }
    }
}

/* cppcheck-suppress constParameterPointer ; signature fixed by PeriodicTask.init */
IfStatus cli_service_init(void *ctx)
{
    const CliService *svc = ctx;
    IfStatus          status;

    if ((svc == NULL) || (svc->uart == NULL)) {
        return IF_HW_FAULT;
    }

    status = svc->uart->init(&svc->uart_config);
    if (status != IF_OK) {
        return status;
    }
    return cli_init(svc->uart, &svc->ports);
}

IfStatus cli_init(const UartIf *uart, const CliPorts *ports)
{
    if ((uart == NULL) || (ports == NULL) || (ports->is_safe_state == NULL)) {
        return IF_HW_FAULT;
    }

    cli_uart = uart;
    cli_core_init(&cli_core);
    cli_core_set_ports(&cli_core, ports);
    cli_core_set_boot_report(&cli_core, cli_boot_report);

    cli_rx_dropped  = 0U;
    cli_chunk_len   = 0U;
    cli_chunk_pos   = 0U;
    cli_banner_sent = false;
    for (size_t slot = 0U; slot < CLI_TX_BUFFER_COUNT; slot++) {
        cli_tx_busy[slot] = false;
    }

    cli_rx_stream = xStreamBufferCreateStatic(sizeof(cli_rx_stream_storage), 1U,
                                              cli_rx_stream_storage, &cli_rx_stream_control);
    if (cli_rx_stream == NULL) {
        return IF_HW_FAULT;
    }

    /* A driver without receive (start_rx NULL or failing) leaves the CLI
     * print-only; that is visible as "no reaction to typing", not a fault. */
    if (uart->start_rx != NULL) {
        (void)uart->start_rx(cli_on_rx, NULL);
    }
    return IF_OK;
}

void cli_set_boot_report(const char *text)
{
    cli_boot_report = text;
    cli_core_set_boot_report(&cli_core, text);
}

/* cppcheck-suppress constParameterPointer ; signature fixed by PeriodicTask.step */
void vCliServiceStep(void *ctx)
{
    CliOut out;
    size_t slot = 0U;

    (void)ctx;

    if (cli_uart == NULL) {
        return; /* cli_init() has not run */
    }

    while ((slot < CLI_TX_BUFFER_COUNT) && cli_tx_busy[slot]) {
        slot++;
    }
    if (slot == CLI_TX_BUFFER_COUNT) {
        return; /* both buffers still with the driver: bytes wait in the stream */
    }

    cli_out_init(&out, cli_tx_buffer[slot], CLI_TX_BUFFER_BYTES);
    if (!cli_banner_sent) {
        cli_write_banner(&out);
        cli_banner_sent = true;
    } else {
        cli_feed_received(&out);
    }
    cli_core.rx_dropped = cli_rx_dropped;

    if (out.len > 0U) {
        cli_tx_busy[slot] = true;
        if (cli_uart->transmit((const uint8_t *)cli_tx_buffer[slot], out.len, cli_on_tx_done,
                               (void *)&cli_tx_busy[slot])
            != IF_OK) {
            cli_tx_busy[slot] = false;
            cli_core.tx_dropped++;
        }
    }
}
