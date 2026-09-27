/*
 * libFuzzer target for the ECP device command handler and the application's commands.
 *
 * Each fuzz input is a sequence of frames, sent to the ECP one at a time through the
 * simulator backend. Each frame is encoded as:
 *
 *   uint8_t flags;    // FRAME_FIX_CRCS: fill in valid header and data CRCs
 *   uint16_t length;  // little endian
 *   uint8_t data[length];
 *
 * so the fuzzer can reach the command handlers without having to find valid CRCs.
 *
 * After each frame, the ECP must send a correctly framed response. If it doesn't, or a
 * sanitizer or assertion fails, the input is reported as a crash.
 */
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/sys/byteorder.h>
#include <irq_ctrl.h>
#include <nsi_cpu_if.h>
#include <nsi_main_semipublic.h>

#include <dp/ecp/protocol.h>
#include <dp/ecp/ecp_device_cmd/backend.h>
#include <dp/ecp/ecp_device_cmd/simulator.h>

#define FRAME_FIX_CRCS BIT(0)

/* Upper bound on simulated time to handle one input, to detect hangs */
#define INPUT_TIMEOUT_MS 1000

static const uint8_t *fuzz_data;
static size_t fuzz_len;
static volatile bool input_done;

static K_SEM_DEFINE(input_ready, 0, 1);
static K_SEM_DEFINE(response_ready, 0, 1);
static struct ecp_device_cmd_response_ctx *rctx;
static uint8_t frame[CONFIG_ECP_DEVICE_CMD_HANDLER_RX_BUFFER_SIZE + 1];

static void fail(const char *why)
{
	printk("ECP fuzz failure: %s\n", why);
	abort();
}

static int host_receive(const struct ecp_device_cmd_backend *backend)
{
	ARG_UNUSED(backend);

	k_sem_give(&response_ready);
	return 0;
}

static void check_response(void)
{
	const struct ecp_response_header *header = (void *)rctx->buf;

	if (rctx->len < ECP_RESPONSE_HEADER_SIZE || rctx->len > rctx->len_max) {
		fail("response length out of range");
	}
	if (ecp_crc8(header, ECP_RESPONSE_HEADER_SIZE) != 0) {
		fail("bad response header CRC");
	}
	if (FIELD_GET(ECP_STRUCT_VERSION_MASK, header->flags) != ECP_PROTO_VERSION ||
	    !FIELD_GET(ECP_IS_RESPONSE_MASK, header->flags) || header->_unused != 0) {
		fail("bad response header flags");
	}

	uint16_t data_len = FIELD_GET(ECP_DATA_LENGTH_MASK, header->data_len);
	size_t expected = ECP_RESPONSE_HEADER_SIZE + (data_len ? data_len + ECP_DATA_CRC_SIZE : 0);

	if (rctx->len != expected) {
		fail("response length doesn't match header");
	}
	if (data_len > 0 &&
	    ecp_crc16(rctx->buf + ECP_RESPONSE_HEADER_SIZE, data_len + ECP_DATA_CRC_SIZE) != 0) {
		fail("bad response data CRC");
	}
	if (data_len > 0 && header->result != ECP_RES_SUCCESS &&
	    header->result != ECP_RES_IN_PROGRESS) {
		fail("error response has data");
	}
}

static void fix_crcs(uint8_t *buf, size_t len)
{
	struct ecp_request_header *header = (void *)buf;

	if (len < ECP_REQUEST_HEADER_SIZE) {
		return;
	}
	header->header_crc = ecp_crc8(header, ECP_REQUEST_HEADER_SIZE_NO_CRC);

	uint16_t data_len = FIELD_GET(ECP_DATA_LENGTH_MASK, header->data_len);
	if (data_len > 0 && len >= ECP_REQUEST_HEADER_SIZE + data_len + ECP_DATA_CRC_SIZE) {
		uint8_t *data = buf + ECP_REQUEST_HEADER_SIZE;
		sys_put_be16(ecp_crc16(data, data_len), data + data_len);
	}
}

/* Commands that would end or wedge the fuzzing process */
static bool skip_frame(const uint8_t *buf, size_t len)
{
	if (len < ECP_REQUEST_HEADER_SIZE) {
		return false;
	}
	uint16_t command = sys_get_le16(&buf[offsetof(struct ecp_request_header, command)]);

	switch (command) {
	case ECP_CMD_REBOOT:
	case ECP_CMD_BOOT_JUMP:
		/* Restart the ECP (and exit the process) */
		return true;
	case ECP_CMD_ENUM:
		/* Sent once at startup. A second ENUM is a known defect. */
		return true;
	default:
		return false;
	}
}

static void send_frame(uint8_t flags, const uint8_t *data, size_t len)
{
	len = MIN(len, sizeof(frame));
	memcpy(frame, data, len);
	if (flags & FRAME_FIX_CRCS) {
		fix_crcs(frame, len);
	}
	if (skip_frame(frame, len)) {
		return;
	}

	k_sem_reset(&response_ready);
	if (ecp_device_cmd_backend_sim_data_received(frame, len) != 0) {
		/* Rejected by the backend (too long), so there's no response */
		if (len <= CONFIG_ECP_DEVICE_CMD_HANDLER_RX_BUFFER_SIZE) {
			fail("backend rejected a frame that fits");
		}
		return;
	}
	if (k_sem_take(&response_ready, K_MSEC(INPUT_TIMEOUT_MS)) != 0) {
		fail("no response");
	}
	check_response();
}

static void run_input(const uint8_t *data, size_t len)
{
	for (int i = 0; i < CONFIG_ECP_FUZZ_MAX_FRAMES && len >= 3; i++) {
		uint8_t flags = data[0];
		size_t frame_len = MIN(sys_get_le16(&data[1]), len - 3);

		send_frame(flags, &data[3], frame_len);
		data += 3 + frame_len;
		len -= 3 + frame_len;
	}
}

static void enumerate(void)
{
	const struct ecp_request_enum q = {.ecp_type = ECP_TYPE_DO_DI_5A, .ecp_revision = 2};
	struct ecp_request_header *header = (void *)frame;

	header->flags = FIELD_PREP(ECP_STRUCT_VERSION_MASK, ECP_PROTO_VERSION);
	header->command = ECP_CMD_ENUM;
	header->data_len = FIELD_PREP(ECP_DATA_LENGTH_MASK, sizeof(q)) |
			   FIELD_PREP(ECP_REQ_COMMAND_VERSION_MASK, 1);
	memcpy(&frame[ECP_REQUEST_HEADER_SIZE], &q, sizeof(q));
	size_t len = ECP_REQUEST_HEADER_SIZE + sizeof(q) + ECP_DATA_CRC_SIZE;
	fix_crcs(frame, len);

	ecp_device_cmd_backend_sim_data_received(frame, len);
	if (k_sem_take(&response_ready, K_MSEC(INPUT_TIMEOUT_MS)) != 0 ||
	    rctx->buf[offsetof(struct ecp_response_header, result)] != ECP_RES_SUCCESS) {
		fail("ENUM failed");
	}
}

static void fuzz_isr(const void *arg)
{
	ARG_UNUSED(arg);

	k_sem_give(&input_ready);
}

int main(void)
{
	ecp_device_cmd_backend_sim_install_send_cb(host_receive, &rctx);

	/* Fuzz the active state, where most commands are available */
	enumerate();

	IRQ_CONNECT(CONFIG_ECP_FUZZ_IRQ, 0, fuzz_isr, NULL, 0);
	irq_enable(CONFIG_ECP_FUZZ_IRQ);

	while (true) {
		k_sem_take(&input_ready, K_FOREVER);
		run_input(fuzz_data, fuzz_len);
		input_done = true;
	}
	return 0;
}

/* Called by libFuzzer, outside of Zephyr, for each input */
NATIVE_SIMULATOR_IF
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t len)
{
	static bool initialized;

	if (!initialized) {
		nsi_init(0, NULL);
		initialized = true;
	}

	fuzz_data = data;
	fuzz_len = len;
	input_done = false;
	hw_irq_ctrl_set_irq(CONFIG_ECP_FUZZ_IRQ);

	/* Run the simulation until the input is handled. libFuzzer frees the input when
	 * this function returns, so it must not be used after that.
	 */
	for (int ms = 0; !input_done; ms++) {
		if (ms > INPUT_TIMEOUT_MS * CONFIG_ECP_FUZZ_MAX_FRAMES) {
			fail("input not handled");
		}
		nsi_exec_for(1000);
	}

	return 0;
}
