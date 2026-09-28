/*
 * End-to-end tests: the ECP host driver (as used by the main MCU) sends commands to the
 * ECP application over a pair of emulated UARTs. The ports are fakes.
 */
#include <string.h>

#include <zephyr/ztest.h>
#include <zephyr/fff.h>
#include <zephyr/app_version.h>
#include <zephyr/drivers/serial/uart_emul.h>
#include <zephyr/sys/byteorder.h>

#include <dp/drivers/ecp.h>
#include <dp/drivers/port.h>
#include <dp/drivers/port_fake.h>
#include <dp/ecp/protocol.h>

DEFINE_FFF_GLOBALS;


#define HOST_UART DEVICE_DT_GET(DT_NODELABEL(uart_host))
#define ECP_UART  DEVICE_DT_GET(DT_NODELABEL(uart_ecp))
#define ECP       DEVICE_DT_GET(DT_NODELABEL(ecp))
#define PORT      DEVICE_DT_GET(DT_NODELABEL(port1_5a))

/* When non-zero, the Nth chunk of data sent in that direction (counting from 1) has its
 * first byte flipped. The host sends a request's header, data and data CRC as separate
 * chunks.
 */
static atomic_t corrupt_to_ecp;
static atomic_t corrupt_to_host;

static uint8_t resp[ECP_MAX_PAYLOAD_BYTES];
static size_t resp_len;

/* Connects the transmit side of one UART to the receive side of the other */
static void wire(const struct device *dev, size_t size, void *user_data)
{
	const struct device *peer = user_data;
	atomic_t *corrupt = (peer == ECP_UART) ? &corrupt_to_ecp : &corrupt_to_host;
	uint8_t buf[256];
	uint32_t n;

	while ((n = uart_emul_get_tx_data(dev, buf, sizeof(buf))) > 0) {
		if (atomic_get(corrupt) > 0 && atomic_dec(corrupt) == 1) {
			buf[0] ^= 0xFF;
		}
		uart_emul_put_rx_data(peer, buf, n);
	}
}

static int command(uint16_t cmd, uint8_t ver, const void *req, size_t req_len)
{
	resp_len = sizeof(resp);
	return ecp_command(ECP, ECP_COMMAND_VER(cmd, ver), (uint8_t *)req, req_len, resp,
			   &resp_len);
}

static struct ecp_response_comm_stats comm_stats(void)
{
	struct ecp_response_comm_stats stats;

	zassert_ok(command(ECP_CMD_COMM_STATS, 1, NULL, 0));
	zassert_equal(resp_len, sizeof(stats));
	memcpy(&stats, resp, sizeof(stats));
	return stats;
}

ZTEST(ecp_host, test_1_hello)
{
	struct ecp_request_hello q = {.in_data = 0x10203040};

	zassert_ok(command(ECP_CMD_HELLO, 1, &q, sizeof(q)));
	zassert_equal(resp_len, sizeof(struct ecp_response_hello));
	zassert_equal(sys_get_le32(resp), 0x11223344);
}

ZTEST(ecp_host, test_1_ident)
{
	struct ecp_request_ident q = {.major = 1};

	zassert_ok(command(ECP_CMD_IDENT, 1, &q, sizeof(q)));

	struct ecp_response_ident r;
	zassert_equal(resp_len, sizeof(r));
	memcpy(&r, resp, sizeof(r));
	zassert_equal(r.major, APP_VERSION_MAJOR);
	zassert_equal(r.minor, APP_VERSION_MINOR);
	zassert_equal(r.patch, APP_PATCHLEVEL);
}

ZTEST(ecp_host, test_1_proto_version)
{
	zassert_ok(command(ECP_CMD_PROTO_VERSION, 2, NULL, 0));

	struct ecp_response_proto_version r;
	zassert_equal(resp_len, sizeof(r));
	memcpy(&r, resp, sizeof(r));
	zassert_equal(r.versions, BIT(ECP_PROTO_VERSION));
}

ZTEST(ecp_host, test_1_echo)
{
	static uint8_t data[ECP_MAX_PAYLOAD_BYTES];
	const size_t lengths[] = {0, 1, 100, 255, 256, ECP_MAX_PAYLOAD_BYTES};

	for (size_t i = 0; i < sizeof(data); i++) {
		data[i] = i * 13;
	}
	ARRAY_FOR_EACH(lengths, i) {
		zassert_ok(command(ECP_CMD_ECHO, 1, data, lengths[i]), "length %zu", lengths[i]);
		zassert_equal(resp_len, lengths[i]);
		zassert_mem_equal(resp, data, lengths[i], "length %zu", lengths[i]);
	}
}

ZTEST(ecp_host, test_1_errors)
{
	/* INVALID_COMMAND and UNSUPPORTED_CMD_VERSION */
	zassert_equal(command(ECP_CMD_RESERVED, 1, NULL, 0), -EPROTO);
	zassert_equal(command(ECP_CMD_FEATURES, 9, NULL, 0), -EPROTO);
}

ZTEST(ecp_host, test_2_enum_and_io)
{
	struct ecp_request_enum q_enum = {.ecp_type = ECP_TYPE_DO_DI_5A, .ecp_revision = 2};

	zassert_ok(command(ECP_CMD_ENUM, 1, &q_enum, sizeof(q_enum)));
	zassert_true(device_is_ready(PORT));

	struct ecp_request_io_write q = {.ch = PORT_CH_B, .value = 7500};

	zassert_ok(command(ECP_CMD_IO_WRITE, 1, &q, sizeof(q)));
	zassert_equal(port_fake_set_value_fake.call_count, 1);
	zassert_equal(port_fake_set_value_fake.arg1_val, PORT_CH_B);
	zassert_equal(port_fake_set_value_fake.arg2_val, 7500);

	port_fake_set_value_fake.return_val = -EIO;
	zassert_equal(command(ECP_CMD_IO_WRITE, 1, &q, sizeof(q)), -EIO);
}

/* The ECP can't tell how long a request with a corrupted header is, so it drops it
 * without responding, and waits for the UART to go idle. The host times out and sends
 * it again (spec figure 9).
 */
ZTEST(ecp_host, test_3_corrupted_request_header_retried)
{
	struct ecp_request_hello q = {.in_data = 1};
	int64_t start = k_uptime_get();

	atomic_set(&corrupt_to_ecp, 1);
	int ret = command(ECP_CMD_HELLO, 1, &q, sizeof(q));
	zassert_ok(ret, "Command failed: %d", ret);
	zassert_equal(sys_get_le32(resp), 1 + 0x01020304);

	/* The ECP never sees the bad request, so check that it was corrupted, and that the
	 * host waited for a response before sending it again.
	 */
	zassert_equal(atomic_get(&corrupt_to_ecp), 0, "Request wasn't corrupted");
	zassert_true(k_uptime_get() - start >= 50, "Host didn't time out and retry");
}

/* The host rejects a corrupted response and asks for it again. The ECP must resend it,
 * not run the command a second time.
 */
/* The ECP responds to a request with corrupted data with INVALID_DATA_CRC, and the host
 * sends it again. The ECP never ran the original request, so it must run this one
 * (spec figure 10).
 */
ZTEST(ecp_host, test_3_corrupted_request_data_retried)
{
	struct ecp_request_hello q = {.in_data = 2};
	struct ecp_response_comm_stats before = comm_stats();

	/* Chunk 2 is the data */
	atomic_set(&corrupt_to_ecp, 2);
	int ret = command(ECP_CMD_HELLO, 1, &q, sizeof(q));
	zassert_ok(ret, "Command failed: %d", ret);
	zassert_equal(sys_get_le32(resp), 2 + 0x01020304);

	struct ecp_response_comm_stats after = comm_stats();
	zassert_true(after.error_count > before.error_count, "ECP didn't see the corruption");
}

ZTEST(ecp_host, test_3_corrupted_response_retried)
{
	struct ecp_request_io_write q = {.ch = PORT_CH_A, .value = 1234};

	atomic_set(&corrupt_to_host, 1);
	zassert_ok(command(ECP_CMD_IO_WRITE, 1, &q, sizeof(q)));

	zassert_equal(port_fake_set_value_fake.call_count, 1, "Command ran %u times",
		      port_fake_set_value_fake.call_count);
}

static void *ecp_host_setup(void)
{
	zassert_true(device_is_ready(ECP), "ECP host driver not ready");

	uart_emul_callback_tx_data_ready_set(HOST_UART, wire, (void *)ECP_UART);
	uart_emul_callback_tx_data_ready_set(ECP_UART, wire, (void *)HOST_UART);
	return NULL;
}

ZTEST_SUITE(ecp_host, NULL, ecp_host_setup, NULL, NULL, NULL);
