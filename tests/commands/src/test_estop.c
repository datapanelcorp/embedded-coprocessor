/*
 * ESTOP in the active state, and recovering by enumerating again. Runs last, because ESTOP
 * returns the ECP to the unconfigured state. The tests run in order, and each depends on
 * the ones before it.
 */
#include <zephyr/ztest.h>

#include <dp/drivers/port.h>
#include <dp/drivers/port_fake.h>

#include "ecp_test.h"

static struct ecp_sim_response resp;

/* Channel B is paused before ESTOP, so it stays paused after enumerating again */
static bool paused[ECP_TEST_NUM_CHANNELS] = {[PORT_CH_B] = true};
static int sensor_power[ECP_TEST_NUM_CHANNELS] = {[PORT_CH_A] = 1, [PORT_CH_B] = 2};

static int is_paused(const struct device *dev, enum port_channel_id ch)
{
	return paused[ch] ? 1 : 0;
}

static int pause(const struct device *dev, enum port_channel_id ch)
{
	paused[ch] = true;
	return 0;
}

/* Whether the channel's output was set to off before it was resumed */
static bool off_before_resume[ECP_TEST_NUM_CHANNELS];

static int resume(const struct device *dev, enum port_channel_id ch)
{
	unsigned int n = MIN(port_fake_set_value_fake.call_count, FFF_ARG_HISTORY_LEN);

	for (unsigned int i = 0; i < n; i++) {
		if (port_fake_set_value_fake.arg1_history[i] == ch &&
		    port_fake_set_value_fake.arg2_history[i] == 0) {
			off_before_resume[ch] = true;
		}
	}
	paused[ch] = false;
	return 0;
}

static int get_sensor_power(const struct device *dev, enum port_channel_id ch, int *value)
{
	*value = sensor_power[ch];
	return 0;
}

static int set_sensor_power(const struct device *dev, enum port_channel_id ch, int value)
{
	sensor_power[ch] = value;
	return 0;
}

static void estop_before(void *fixture)
{
	ARG_UNUSED(fixture);

	/* Fakes are reset before each test by the fake port driver */
	port_fake_paused_fake.custom_fake = is_paused;
	port_fake_pause_fake.custom_fake = pause;
	port_fake_resume_fake.custom_fake = resume;
	port_fake_get_sensor_power_fake.custom_fake = get_sensor_power;
	port_fake_set_sensor_power_fake.custom_fake = set_sensor_power;
}

static void expect_io(enum ecp_result_code expected)
{
	struct ecp_request_io_read q = {.ch = PORT_CH_A};

	ecp_test_expect(ECP_CMD_IO_READ, 2, &q, sizeof(q), expected, &resp);
}

ZTEST(ecp_estop, test_1_estop_with_data)
{
	const uint8_t data = 0;

	ecp_test_expect(ECP_CMD_ESTOP, 1, &data, sizeof(data), ECP_RES_INVALID_COMMAND, &resp);

	/* Nothing happened */
	zassert_equal(port_fake_pause_fake.call_count, 0);
	expect_io(ECP_RES_SUCCESS);
}

/* Spec: ESTOP returns all I/O to a safe state and enters the unconfigured state */
ZTEST(ecp_estop, test_2_estop)
{
	ecp_test_expect(ECP_CMD_ESTOP, 1, NULL, 0, ECP_RES_IN_PROGRESS, &resp);

	/* The safe state is reached before the response */
	for (int ch = 0; ch < ECP_TEST_NUM_CHANNELS; ch++) {
		zassert_true(paused[ch], "Channel %d not paused", ch);
		zassert_equal(sensor_power[ch], 0, "Channel %d sensor power on", ch);
	}
	ecp_test_expect_in_progress_result(ECP_RES_SUCCESS);

	expect_io(ECP_RES_NOT_ALLOWED);

	/* ESTOP again: still stopped */
	ecp_test_expect(ECP_CMD_ESTOP, 1, NULL, 0, ECP_RES_IN_PROGRESS, &resp);
	ecp_test_expect_in_progress_result(ECP_RES_SUCCESS);
	expect_io(ECP_RES_NOT_ALLOWED);
}

ZTEST(ecp_estop, test_3_enum_other_type)
{
	struct ecp_request_enum q = {
		.ecp_type = (ECP_TEST_ENUM_TYPE == ECP_TYPE_DO_DI_5A) ? ECP_TYPE_DO_DI_13A
								       : ECP_TYPE_DO_DI_5A,
		.ecp_revision = 2,
	};

	ecp_test_expect(ECP_CMD_ENUM, 1, &q, sizeof(q), ECP_RES_INVALID_PARAM, &resp);
	expect_io(ECP_RES_NOT_ALLOWED);
}

/* Enumerating again leaves every output and sensor power off until the host commands them
 * (SRS-352, SRS-354). Channels the host had paused stay paused; the others are resumed,
 * but only after their output is set to off.
 */
ZTEST(ecp_estop, test_4_enum_again)
{
	struct ecp_request_enum q = {.ecp_type = ECP_TEST_ENUM_TYPE, .ecp_revision = 2};

	ecp_test_expect(ECP_CMD_ENUM, 1, &q, sizeof(q), ECP_RES_SUCCESS, &resp);

	zassert_false(paused[PORT_CH_A], "Channel A not resumed");
	zassert_true(off_before_resume[PORT_CH_A], "Channel A resumed before its output was off");
	zassert_true(paused[PORT_CH_B], "Channel B was paused before ESTOP, but was resumed");
	zassert_equal(sensor_power[PORT_CH_A], 0, "Channel A sensor power back on");
	zassert_equal(sensor_power[PORT_CH_B], 0, "Channel B sensor power back on");

	expect_io(ECP_RES_SUCCESS);

	/* Active again, and ENUM is still accepted */
	ecp_test_expect(ECP_CMD_ENUM, 1, &q, sizeof(q), ECP_RES_SUCCESS, &resp);
}

ZTEST_SUITE(ecp_estop, ecp_phase_estop, NULL, estop_before, NULL, NULL);
