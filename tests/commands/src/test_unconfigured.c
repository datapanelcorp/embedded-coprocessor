/*
 * Behavior before ENUM, in the unconfigured state
 */
#include <zephyr/ztest.h>

#include <dp/drivers/port.h>
#include <dp/drivers/port_fake.h>

#include "ecp_test.h"

static struct ecp_sim_response resp;

struct io_request {
	uint16_t command;
	uint8_t version;
	const void *data;
	uint16_t data_len;
};

static const struct ecp_request_io_read read_q = {.ch = PORT_CH_A};
static const struct ecp_request_io_write write_q = {.ch = PORT_CH_A, .value = 1};
static const struct ecp_request_io_get_attrib get_q = {.ch = PORT_CH_A,
						       .attrib_id = PORT_CH_ATTRIB_MODE};
static const struct ecp_request_io_set_attrib set_q = {.ch = PORT_CH_A,
						       .attrib_id = PORT_CH_ATTRIB_MODE};
static const struct ecp_request_io_pause pause_q = {.pause = BIT(0), .ch_mask = BIT(0)};
static const struct ecp_request_io_clear_fault clear_q = {.ch = PORT_CH_A};

static const struct io_request io_requests[] = {
	{ECP_CMD_IO_READ, 2, &read_q, sizeof(read_q)},
	{ECP_CMD_IO_READ, 3, NULL, 0},
	{ECP_CMD_IO_WRITE, 1, &write_q, sizeof(write_q)},
	{ECP_CMD_IO_GET_ATTRIB, 1, &get_q, sizeof(get_q)},
	{ECP_CMD_IO_SET_ATTRIB, 1, &set_q, sizeof(set_q)},
	{ECP_CMD_IO_PAUSE, 1, &pause_q, sizeof(pause_q)},
	{ECP_CMD_IO_CLEAR_FAULT, 1, &clear_q, sizeof(clear_q)},
};

/* Whatever the result code, an IO command must not succeed or reach a port */
ZTEST(ecp_unconfigured, test_io_before_enum_fails)
{
	ARRAY_FOR_EACH_PTR(io_requests, q) {
		enum ecp_result_code result =
			ecp_sim_host_transact(q->command, q->version, q->data, q->data_len, &resp);
		zassert_not_equal(result, ECP_RES_SUCCESS, "Command 0x%04x v%u succeeded",
				  q->command, q->version);
		zassert_equal(resp.data_len, 0);
	}

	zassert_false(device_is_ready(ECP_TEST_PORT));
	zassert_equal(port_fake_get_attribute_fake.call_count, 0);
	zassert_equal(port_fake_set_value_fake.call_count, 0);
	zassert_equal(port_fake_set_attribute_fake.call_count, 0);
	zassert_equal(port_fake_pause_fake.call_count, 0);
	zassert_equal(port_fake_clear_fault_fake.call_count, 0);
}

ZTEST(ecp_unconfigured, test_io_before_enum_not_allowed)
{
	ARRAY_FOR_EACH_PTR(io_requests, q) {
		ecp_test_expect(q->command, q->version, q->data, q->data_len, ECP_RES_NOT_ALLOWED,
				&resp);
	}
}

ZTEST(ecp_unconfigured, test_enum_invalid_type_or_revision)
{
	const struct ecp_request_enum invalid[] = {
		{.ecp_type = ECP_TYPE_INVALID, .ecp_revision = 2},
		{.ecp_type = ECP_TYPE_DO_DI_5A, .ecp_revision = 1},
		{.ecp_type = ECP_TYPE_DO_DI_5A, .ecp_revision = 3},
		{.ecp_type = ECP_TYPE_DO_DI_13A, .ecp_revision = 0},
		{.ecp_type = ECP_TYPE_DOPLUS_DI, .ecp_revision = 2},
		{.ecp_type = 0xFF, .ecp_revision = 2},
	};

	ARRAY_FOR_EACH_PTR(invalid, q) {
		ecp_test_expect(ECP_CMD_ENUM, 1, q, sizeof(*q), ECP_RES_INVALID_PARAM, &resp);
	}

	/* Still unconfigured */
	zassert_false(device_is_ready(DEVICE_DT_GET(DT_NODELABEL(port1_5a))));
	zassert_false(device_is_ready(DEVICE_DT_GET(DT_NODELABEL(port1_13a))));
}

ZTEST(ecp_unconfigured, test_enum_request_truncated)
{
	struct ecp_request_enum q = {.ecp_type = ECP_TEST_ENUM_TYPE, .ecp_revision = 2};

	ecp_test_expect(ECP_CMD_ENUM, 1, &q, sizeof(q) - 1, ECP_RES_REQUEST_TRUNCATED, &resp);
	zassert_false(device_is_ready(ECP_TEST_PORT));
}

/* These must be rejected before any reboot happens */
ZTEST(ecp_unconfigured, test_reboot_commands_with_data)
{
	const uint8_t data = 0;

	ecp_test_expect(ECP_CMD_REBOOT, 1, &data, sizeof(data), ECP_RES_INVALID_COMMAND, &resp);
	ecp_test_expect(ECP_CMD_BOOT_JUMP, 1, &data, sizeof(data), ECP_RES_INVALID_COMMAND,
			&resp);
}

/* Spec: ESTOP has no effect in the unconfigured state */
ZTEST(ecp_unconfigured, test_estop_unconfigured)
{
	ecp_test_expect(ECP_CMD_ESTOP, 1, NULL, 0, ECP_RES_IN_PROGRESS, &resp);
	ecp_test_expect_in_progress_result(ECP_RES_SUCCESS);

	zassert_false(device_is_ready(ECP_TEST_PORT));
}

ZTEST_SUITE(ecp_unconfigured, ecp_phase_unconfigured, NULL, NULL, NULL, NULL);
