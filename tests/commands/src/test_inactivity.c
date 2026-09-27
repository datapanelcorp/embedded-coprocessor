/*
 * Host inactivity timeout (SRS-354): with no valid request for
 * CONFIG_APP_ECP_HOST_TIMEOUT_MS, the ECP switches off every output and sensor power. They
 * stay off until the host commands them again.
 */
#include <zephyr/ztest.h>

#include <dp/drivers/port.h>
#include <dp/drivers/port_fake.h>

#include "ecp_test.h"

#define TIMEOUT_MS CONFIG_APP_ECP_HOST_TIMEOUT_MS

static struct ecp_sim_response resp;
static uint8_t frame[CONFIG_ECP_DEVICE_CMD_HANDLER_RX_BUFFER_SIZE];

static void io_write(enum port_channel_id ch, int32_t value)
{
	struct ecp_request_io_write q = {.ch = ch, .value = value};

	ecp_test_expect(ECP_CMD_IO_WRITE, 1, &q, sizeof(q), ECP_RES_SUCCESS, &resp);
}

static void io_read_all(void)
{
	ecp_test_expect(ECP_CMD_IO_READ, 3, NULL, 0, ECP_RES_SUCCESS, &resp);
}

/* How many times the channel's output was set to @a value */
static int writes_of(enum port_channel_id ch, int value)
{
	unsigned int n = MIN(port_fake_set_value_fake.call_count, FFF_ARG_HISTORY_LEN);
	int count = 0;

	for (unsigned int i = 0; i < n; i++) {
		if (port_fake_set_value_fake.arg1_history[i] == ch &&
		    port_fake_set_value_fake.arg2_history[i] == value) {
			count++;
		}
	}
	return count;
}

/* How many times the channel's sensor power was switched off */
static int sensor_power_offs(enum port_channel_id ch)
{
	unsigned int n = MIN(port_fake_set_sensor_power_fake.call_count, FFF_ARG_HISTORY_LEN);
	int count = 0;

	for (unsigned int i = 0; i < n; i++) {
		if (port_fake_set_sensor_power_fake.arg1_history[i] == ch &&
		    port_fake_set_sensor_power_fake.arg2_history[i] == 0) {
			count++;
		}
	}
	return count;
}

static void assert_outputs_switched_off(int times)
{
	for (enum port_channel_id ch = PORT_CH_A; ch < ECP_TEST_NUM_CHANNELS; ch++) {
		zassert_equal(writes_of(ch, 0), times, "Channel %d output off %d times, not %d",
			      ch, writes_of(ch, 0), times);
		zassert_equal(sensor_power_offs(ch), times,
			      "Channel %d sensor power off %d times, not %d", ch,
			      sensor_power_offs(ch), times);
	}
}

/* A host that keeps talking keeps the outputs as commanded */
ZTEST(ecp_inactivity, test_requests_keep_outputs)
{
	io_write(PORT_CH_A, 500);

	for (int i = 0; i < 6; i++) {
		k_msleep(TIMEOUT_MS / 2);
		io_read_all();
	}

	assert_outputs_switched_off(0);
	zassert_equal(port_fake_set_value_fake.call_count, 1);
}

/* Silence switches every output and sensor power off, once */
ZTEST(ecp_inactivity, test_silence_switches_outputs_off)
{
	io_write(PORT_CH_A, 500);
	io_write(PORT_CH_B, 300);

	k_msleep(TIMEOUT_MS - 10);
	assert_outputs_switched_off(0);

	k_msleep(20);
	assert_outputs_switched_off(1);

	/* They stay off, without being switched off again */
	k_msleep(3 * TIMEOUT_MS);
	assert_outputs_switched_off(1);
}

/* After the timeout, the host's next command applies as usual */
ZTEST(ecp_inactivity, test_command_after_timeout_applies)
{
	io_write(PORT_CH_A, 500);
	k_msleep(TIMEOUT_MS + 10);
	assert_outputs_switched_off(1);

	io_write(PORT_CH_A, 700);
	zassert_equal(port_fake_set_value_fake.arg1_val, PORT_CH_A);
	zassert_equal(port_fake_set_value_fake.arg2_val, 700);

	k_msleep(TIMEOUT_MS / 2);
	io_read_all();
	k_msleep(TIMEOUT_MS / 2);
	assert_outputs_switched_off(1);
}

/* Corrupted frames aren't valid requests, so they don't keep the outputs on */
ZTEST(ecp_inactivity, test_corrupted_frames_dont_count)
{
	io_write(PORT_CH_A, 500);

	for (int i = 0; i < 3; i++) {
		k_msleep(TIMEOUT_MS / 2);

		size_t len = ecp_sim_host_build_request(frame, sizeof(frame), ECP_CMD_IO_READ, 3,
							 0, false, NULL, 0);
		struct ecp_request_header *header = (void *)frame;

		header->header_crc ^= 0xFF;
		ecp_sim_host_send_raw(frame, len, &resp);
		zassert_equal(resp.result, ECP_RES_INVALID_HEADER_CRC);
	}

	assert_outputs_switched_off(1);
}

ZTEST_SUITE(ecp_inactivity, ecp_phase_active, NULL, NULL, NULL, NULL);
