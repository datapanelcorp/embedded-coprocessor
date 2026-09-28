/*
 * IO commands, which are only available in the active state
 */
#include <limits.h>
#include <string.h>

#include <zephyr/ztest.h>

#include <dp/drivers/port.h>
#include <dp/drivers/port_fake.h>

#include "ecp_test.h"

static struct ecp_sim_response resp;

/* Channel-specific values returned by the fake port */
#define MODE(ch)        (3 + (ch))
#define RAW(ch)         (1000 + (ch))
#define SCALED(ch)      (-2000 - (ch))
#define VOLTAGE(ch)     (12000 + (ch))
#define CURRENT(ch)     (500 + (ch))
#define TEMPERATURE(ch) (25000 + (ch))
#define FAULT(ch)       ((ch) == PORT_CH_A ? PORT_CH_OK : PORT_CH_FAULT_TEMPERATURE)
#define STATUS(ch)      ((ch) == PORT_CH_A ? PORT_CH_STATUS_ACTIVE : PORT_CH_STATUS_ERROR)
#define SPWR(ch)        ((ch) == PORT_CH_A ? 1 : 0)
#define SOFT_SWITCH(ch) ((ch) == PORT_CH_A ? 0 : 2)

static bool paused[ECP_TEST_NUM_CHANNELS];

static int get_attribute(const struct device *dev, enum port_channel_id ch,
			 enum port_channel_attribute_id attr, uintptr_t *value)
{
	*value = (attr == PORT_CH_ATTRIB_MODE) ? MODE(ch) : 0;
	return 0;
}

#define DEFINE_GETTER(_name, _type, _value)                                                        \
	static int _name(const struct device *dev, enum port_channel_id ch, _type *value)          \
	{                                                                                          \
		*value = _value;                                                                   \
		return 0;                                                                          \
	}

DEFINE_GETTER(get_raw_value, int, RAW(ch))
DEFINE_GETTER(get_value, int, SCALED(ch))
DEFINE_GETTER(get_analog_value, int, VOLTAGE(ch))
DEFINE_GETTER(get_current, int, CURRENT(ch))
DEFINE_GETTER(get_temperature, int, TEMPERATURE(ch))
DEFINE_GETTER(get_fault_status, enum port_channel_fault_status, FAULT(ch))
DEFINE_GETTER(get_status, enum port_channel_status, STATUS(ch))
DEFINE_GETTER(get_sensor_power, int, SPWR(ch))
DEFINE_GETTER(get_soft_switch_status, int, SOFT_SWITCH(ch))

static int pause(const struct device *dev, enum port_channel_id ch)
{
	paused[ch] = true;
	return 0;
}

static int resume(const struct device *dev, enum port_channel_id ch)
{
	paused[ch] = false;
	return 0;
}

static int is_paused(const struct device *dev, enum port_channel_id ch)
{
	return paused[ch] ? 1 : 0;
}

static void io_before(void *fixture)
{
	ARG_UNUSED(fixture);

	/* Fakes are reset before each test by the fake port driver */
	port_fake_get_attribute_fake.custom_fake = get_attribute;
	port_fake_get_raw_value_fake.custom_fake = get_raw_value;
	port_fake_get_value_fake.custom_fake = get_value;
	port_fake_get_analog_value_fake.custom_fake = get_analog_value;
	port_fake_get_current_fake.custom_fake = get_current;
	port_fake_get_temperature_fake.custom_fake = get_temperature;
	port_fake_get_fault_status_fake.custom_fake = get_fault_status;
	port_fake_get_status_fake.custom_fake = get_status;
	port_fake_get_sensor_power_fake.custom_fake = get_sensor_power;
	port_fake_get_soft_switch_status_fake.custom_fake = get_soft_switch_status;
	port_fake_pause_fake.custom_fake = pause;
	port_fake_resume_fake.custom_fake = resume;
	port_fake_paused_fake.custom_fake = is_paused;

	memset(paused, 0, sizeof(paused));
}

static void check_io_read(const uint8_t *data, enum port_channel_id ch)
{
	struct ecp_response_io_read r;
	memcpy(&r, data, sizeof(r));

	zassert_equal(r.ch, ch);
	zassert_equal(r.mode, MODE(ch));
	zassert_equal(r.raw, RAW(ch));
	zassert_equal((int32_t)r.scaled, SCALED(ch));
	zassert_equal(r.voltage, VOLTAGE(ch));
	zassert_equal(r.current, CURRENT(ch));
	zassert_equal(r.temperature, TEMPERATURE(ch));
	zassert_equal(r.fault_status, FAULT(ch));
	zassert_equal(r.hb_direction, PORT_HBRIDGE_DIR_INVALID);
	zassert_equal(FIELD_GET(ECP_IO_F_CH_STATUS_MASK, r.flags), STATUS(ch));
	zassert_equal(FIELD_GET(ECP_IO_F_SPWR_STATUS_MASK, r.flags), SPWR(ch));
	zassert_equal(FIELD_GET(ECP_IO_F_SOFT_SWITCH_STATUS_MASK, r.flags), SOFT_SWITCH(ch));
	zassert_equal(FIELD_GET(ECP_IO_F_PAUSED, r.flags), paused[ch]);
	zassert_equal(r.flags & BIT(0), 0, "Reserved flag set");
}

ZTEST(ecp_io, test_io_read)
{
	paused[PORT_CH_B] = true;

	for (enum port_channel_id ch = PORT_CH_A; ch < ECP_TEST_NUM_CHANNELS; ch++) {
		struct ecp_request_io_read q = {.ch = ch};

		ecp_test_expect(ECP_CMD_IO_READ, 2, &q, sizeof(q), ECP_RES_SUCCESS, &resp);
		zassert_equal(resp.data_len, sizeof(struct ecp_response_io_read));
		check_io_read(resp.data, ch);
		zassert_equal(port_fake_get_value_fake.arg0_val, ECP_TEST_PORT);
	}
}

ZTEST(ecp_io, test_io_read_all_channels)
{
	paused[PORT_CH_A] = true;

	ecp_test_expect(ECP_CMD_IO_READ, 3, NULL, 0, ECP_RES_SUCCESS, &resp);
	zassert_equal(resp.data_len, 1 + ECP_TEST_NUM_CHANNELS * sizeof(struct ecp_response_io_read));
	zassert_equal(resp.data[0], ECP_TEST_NUM_CHANNELS);

	for (enum port_channel_id ch = PORT_CH_A; ch < ECP_TEST_NUM_CHANNELS; ch++) {
		check_io_read(&resp.data[1 + ch * sizeof(struct ecp_response_io_read)], ch);
	}
}

ZTEST(ecp_io, test_io_read_invalid_request)
{
	struct ecp_request_io_read q = {.ch = PORT_CH_A};
	uint8_t two[2] = {0};

	ecp_test_expect(ECP_CMD_IO_READ, 2, NULL, 0, ECP_RES_INVALID_PARAM, &resp);
	ecp_test_expect(ECP_CMD_IO_READ, 2, two, sizeof(two), ECP_RES_INVALID_PARAM, &resp);
	ecp_test_expect(ECP_CMD_IO_READ, 3, &q, sizeof(q), ECP_RES_INVALID_PARAM, &resp);
	ecp_test_expect(ECP_CMD_IO_READ, 1, &q, sizeof(q), ECP_RES_UNSUPPORTED_CMD_VERSION, &resp);
}

ZTEST(ecp_io, test_io_read_mode_error)
{
	struct ecp_request_io_read q = {.ch = PORT_CH_A};

	port_fake_get_attribute_fake.custom_fake = NULL;
	port_fake_get_attribute_fake.return_val = -EIO;

	ecp_test_expect(ECP_CMD_IO_READ, 2, &q, sizeof(q), ECP_RES_ERROR, &resp);
	ecp_test_expect(ECP_CMD_IO_READ, 3, NULL, 0, ECP_RES_ERROR, &resp);
}

/* A port that only reports its mode */
static void only_mode_supported(void)
{
	port_fake_reset();
	port_fake_get_attribute_fake.custom_fake = get_attribute;
	port_fake_get_raw_value_fake.return_val = -ENOTSUP;
	port_fake_get_value_fake.return_val = -ENOTSUP;
	port_fake_get_analog_value_fake.return_val = -ENOTSUP;
	port_fake_get_current_fake.return_val = -ENOTSUP;
	port_fake_get_temperature_fake.return_val = -ENOTSUP;
	port_fake_get_fault_status_fake.return_val = -ENOTSUP;
	port_fake_get_status_fake.return_val = -ENOTSUP;
	port_fake_get_sensor_power_fake.return_val = -ENOTSUP;
	port_fake_get_soft_switch_status_fake.return_val = -ENOTSUP;
	port_fake_paused_fake.return_val = -ENOTSUP;
}

ZTEST(ecp_io, test_io_read_unsupported_status)
{
	struct ecp_request_io_read q = {.ch = PORT_CH_A};

	only_mode_supported();
	ecp_test_expect(ECP_CMD_IO_READ, 2, &q, sizeof(q), ECP_RES_SUCCESS, &resp);

	struct ecp_response_io_read r;
	memcpy(&r, resp.data, sizeof(r));
	zassert_equal(r.mode, MODE(PORT_CH_A));
	zassert_equal(FIELD_GET(ECP_IO_F_CH_STATUS_MASK, r.flags), 3, "Not 'not available'");
	zassert_equal(FIELD_GET(ECP_IO_F_SPWR_STATUS_MASK, r.flags), 3, "Not 'not available'");
	zassert_equal(FIELD_GET(ECP_IO_F_SOFT_SWITCH_STATUS_MASK, r.flags), 3,
		      "Not 'not available'");
	zassert_equal(FIELD_GET(ECP_IO_F_PAUSED, r.flags), 0);
}

/* Spec: values the port can't provide are -1 */
ZTEST(ecp_io, test_io_read_unsupported_values)
{
	struct ecp_request_io_read q = {.ch = PORT_CH_A};

	only_mode_supported();
	ecp_test_expect(ECP_CMD_IO_READ, 2, &q, sizeof(q), ECP_RES_SUCCESS, &resp);

	struct ecp_response_io_read r;
	memcpy(&r, resp.data, sizeof(r));
	zassert_equal(r.raw, UINT32_MAX);
	zassert_equal((int32_t)r.scaled, -1);
	zassert_equal(r.voltage, -1);
	zassert_equal(r.current, -1);
	zassert_equal(r.temperature, -1);
}

ZTEST(ecp_io, test_io_write)
{
	struct ecp_request_io_write q = {.ch = PORT_CH_B, .value = -12345};

	ecp_test_expect(ECP_CMD_IO_WRITE, 1, &q, sizeof(q), ECP_RES_SUCCESS, &resp);
	zassert_equal(port_fake_set_value_fake.call_count, 1);
	zassert_equal(port_fake_set_value_fake.arg0_val, ECP_TEST_PORT);
	zassert_equal(port_fake_set_value_fake.arg1_val, PORT_CH_B);
	zassert_equal(port_fake_set_value_fake.arg2_val, -12345);

	port_fake_set_value_fake.return_val = -EINVAL;
	ecp_test_expect(ECP_CMD_IO_WRITE, 1, &q, sizeof(q), ECP_RES_ERROR, &resp);

	ecp_test_expect(ECP_CMD_IO_WRITE, 1, &q, sizeof(q) - 1, ECP_RES_INVALID_PARAM, &resp);
}

ZTEST(ecp_io, test_io_get_attrib)
{
	struct ecp_request_io_get_attrib q = {.ch = PORT_CH_B, .attrib_id = PORT_CH_ATTRIB_MODE};

	ecp_test_expect(ECP_CMD_IO_GET_ATTRIB, 1, &q, sizeof(q), ECP_RES_SUCCESS, &resp);
	zassert_equal(resp.data_len, sizeof(struct ecp_response_io_get_attrib));
	zassert_equal(port_fake_get_attribute_fake.arg0_val, ECP_TEST_PORT);
	zassert_equal(port_fake_get_attribute_fake.arg1_val, PORT_CH_B);
	zassert_equal(port_fake_get_attribute_fake.arg2_val, PORT_CH_ATTRIB_MODE);

	struct ecp_response_io_get_attrib r;
	memcpy(&r, resp.data, sizeof(r));
	zassert_equal(r.ch, PORT_CH_B);
	zassert_equal(r.attrib_id, PORT_CH_ATTRIB_MODE);
	zassert_equal(r.value, MODE(PORT_CH_B));

	port_fake_get_attribute_fake.custom_fake = NULL;
	port_fake_get_attribute_fake.return_val = -ENOTSUP;
	ecp_test_expect(ECP_CMD_IO_GET_ATTRIB, 1, &q, sizeof(q), ECP_RES_NOT_SUPPORTED, &resp);

	port_fake_get_attribute_fake.return_val = -EIO;
	ecp_test_expect(ECP_CMD_IO_GET_ATTRIB, 1, &q, sizeof(q), ECP_RES_ERROR, &resp);
}

ZTEST(ecp_io, test_io_set_attrib)
{
	struct ecp_request_io_set_attrib q = {
		.ch = PORT_CH_A,
		.attrib_id = PORT_CH_ATTRIB_PWM_FREQ,
		.value = 0xDEADBEEF,
	};

	ecp_test_expect(ECP_CMD_IO_SET_ATTRIB, 1, &q, sizeof(q), ECP_RES_SUCCESS, &resp);
	zassert_equal(port_fake_set_attribute_fake.arg0_val, ECP_TEST_PORT);
	zassert_equal(port_fake_set_attribute_fake.arg1_val, PORT_CH_A);
	zassert_equal(port_fake_set_attribute_fake.arg2_val, PORT_CH_ATTRIB_PWM_FREQ);
	zassert_equal(port_fake_set_attribute_fake.arg3_val, 0xDEADBEEF);

	port_fake_set_attribute_fake.return_val = -ENOTSUP;
	ecp_test_expect(ECP_CMD_IO_SET_ATTRIB, 1, &q, sizeof(q), ECP_RES_NOT_SUPPORTED, &resp);

	port_fake_set_attribute_fake.return_val = -EINVAL;
	ecp_test_expect(ECP_CMD_IO_SET_ATTRIB, 1, &q, sizeof(q), ECP_RES_ERROR, &resp);
}

static void io_pause(uint8_t pause_mask, uint8_t ch_mask, uint8_t expected_paused)
{
	struct ecp_request_io_pause q = {.pause = pause_mask, .ch_mask = ch_mask};

	ecp_test_expect(ECP_CMD_IO_PAUSE, 1, &q, sizeof(q), ECP_RES_SUCCESS, &resp);
	zassert_equal(resp.data_len, sizeof(struct ecp_response_io_pause));
	zassert_equal(resp.data[0], expected_paused, "pause=0x%02x ch_mask=0x%02x: paused=0x%02x",
		      pause_mask, ch_mask, resp.data[0]);
}

ZTEST(ecp_io, test_io_pause)
{
	io_pause(BIT(PORT_CH_A), BIT(PORT_CH_A), BIT(PORT_CH_A));
	zassert_equal(port_fake_pause_fake.arg1_val, PORT_CH_A);

	/* B is paused, and A is left alone */
	io_pause(BIT(PORT_CH_B), BIT(PORT_CH_B), BIT(PORT_CH_A) | BIT(PORT_CH_B));

	/* Resume A only */
	io_pause(0, BIT(PORT_CH_A), BIT(PORT_CH_B));

	/* Pause A and resume B together */
	io_pause(BIT(PORT_CH_A), BIT(PORT_CH_A) | BIT(PORT_CH_B), BIT(PORT_CH_A));

	/* An empty mask just reports the state */
	io_pause(0xFF, 0, BIT(PORT_CH_A));
}

ZTEST(ecp_io, test_io_pause_error)
{
	struct ecp_request_io_pause q = {.pause = BIT(PORT_CH_A), .ch_mask = BIT(PORT_CH_A)};

	port_fake_pause_fake.custom_fake = NULL;
	port_fake_pause_fake.return_val = -EIO;

	enum ecp_result_code result =
		ecp_sim_host_transact(ECP_CMD_IO_PAUSE, 1, &q, sizeof(q), &resp);
	zassert_not_equal(result, ECP_RES_SUCCESS);
}

ZTEST(ecp_io, test_io_clear_fault)
{
	struct ecp_request_io_clear_fault q = {.ch = PORT_CH_B};

	ecp_test_expect(ECP_CMD_IO_CLEAR_FAULT, 1, &q, sizeof(q), ECP_RES_SUCCESS, &resp);
	zassert_equal(port_fake_clear_fault_fake.call_count, 1);
	zassert_equal(port_fake_clear_fault_fake.arg0_val, ECP_TEST_PORT);
	zassert_equal(port_fake_clear_fault_fake.arg1_val, PORT_CH_B);

	port_fake_clear_fault_fake.return_val = -EIO;
	ecp_test_expect(ECP_CMD_IO_CLEAR_FAULT, 1, &q, sizeof(q), ECP_RES_ERROR, &resp);
}

/* Spec: INVALID_PARAM for out of range parameters. The channel should be checked before
 * it reaches the port driver.
 */
ZTEST(ecp_io, test_io_channel_out_of_range)
{
	const uint8_t ch = ECP_TEST_NUM_CHANNELS;
	struct ecp_request_io_read read_q = {.ch = ch};
	struct ecp_request_io_write write_q = {.ch = ch};
	struct ecp_request_io_get_attrib get_q = {.ch = ch};
	struct ecp_request_io_set_attrib set_q = {.ch = ch};
	struct ecp_request_io_pause pause_q = {.pause = BIT(ch), .ch_mask = BIT(ch)};
	struct ecp_request_io_clear_fault clear_q = {.ch = ch};

	ecp_test_expect(ECP_CMD_IO_READ, 2, &read_q, sizeof(read_q), ECP_RES_INVALID_PARAM, &resp);
	ecp_test_expect(ECP_CMD_IO_WRITE, 1, &write_q, sizeof(write_q), ECP_RES_INVALID_PARAM,
			&resp);
	ecp_test_expect(ECP_CMD_IO_GET_ATTRIB, 1, &get_q, sizeof(get_q), ECP_RES_INVALID_PARAM,
			&resp);
	ecp_test_expect(ECP_CMD_IO_SET_ATTRIB, 1, &set_q, sizeof(set_q), ECP_RES_INVALID_PARAM,
			&resp);
	ecp_test_expect(ECP_CMD_IO_PAUSE, 1, &pause_q, sizeof(pause_q), ECP_RES_INVALID_PARAM,
			&resp);
	ecp_test_expect(ECP_CMD_IO_CLEAR_FAULT, 1, &clear_q, sizeof(clear_q),
			ECP_RES_INVALID_PARAM, &resp);

	/* Nothing reached the port driver */
	zassert_equal(port_fake_get_attribute_fake.call_count, 0);
	zassert_equal(port_fake_set_value_fake.call_count, 0);
	zassert_equal(port_fake_set_attribute_fake.call_count, 0);
	zassert_equal(port_fake_pause_fake.call_count, 0);
	zassert_equal(port_fake_resume_fake.call_count, 0);
	zassert_equal(port_fake_clear_fault_fake.call_count, 0);
}

/* An invalid channel in the mask means no channel is paused or resumed */
ZTEST(ecp_io, test_io_pause_partly_out_of_range)
{
	struct ecp_request_io_pause q = {
		.pause = BIT(PORT_CH_A),
		.ch_mask = BIT(PORT_CH_A) | BIT(ECP_TEST_NUM_CHANNELS),
	};

	ecp_test_expect(ECP_CMD_IO_PAUSE, 1, &q, sizeof(q), ECP_RES_INVALID_PARAM, &resp);
	zassert_equal(port_fake_pause_fake.call_count, 0);
	zassert_false(paused[PORT_CH_A]);
}

ZTEST_SUITE(ecp_io, ecp_phase_active, NULL, io_before, NULL, NULL);
