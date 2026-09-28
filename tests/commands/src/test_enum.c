/*
 * ENUM: the transition from the unconfigured to the active state
 */
#include <zephyr/ztest.h>

#include <dp/drivers/port.h>
#include <dp/drivers/port_fake.h>

#include "ecp_test.h"

static struct ecp_sim_response resp;

ZTEST(ecp_enum, test_enum)
{
	struct ecp_request_enum q = {
		.offset = 0,
		.ecp_type = ECP_TEST_ENUM_TYPE,
		.ecp_revision = 2,
	};

	ecp_test_expect(ECP_CMD_ENUM, 1, &q, sizeof(q), ECP_RES_SUCCESS, &resp);
	zassert_equal(resp.data_len, 0);

	zassert_true(device_is_ready(DEVICE_DT_GET(DT_NODELABEL(port))),
		     "Port controller not initialized");
	zassert_true(device_is_ready(ECP_TEST_PORT), "Port not initialized");
	zassert_false(device_is_ready(ECP_TEST_OTHER_PORT), "Wrong port initialized");
	zassert_equal(port_channel_count(PORT_1), ECP_TEST_NUM_CHANNELS);

	/* The port handler applies the default configuration, then polls every channel */
	k_msleep(2 * CONFIG_DP_PORTS_POLL_PERIOD);
	zassert_true(port_fake_set_attribute_fake.call_count > 0, "Port not configured");
	zassert_true(port_fake_poll_fake.call_count >= ECP_TEST_NUM_CHANNELS, "Port not polled");
	zassert_equal(port_fake_poll_fake.arg0_val, ECP_TEST_PORT);
}

ZTEST_SUITE(ecp_enum, ecp_phase_enum, NULL, NULL, NULL, NULL);

/* Spec: ENUM is available in the active state too. A host that restarted while the ECP
 * kept running enumerates again, and the channels are left as they are.
 */
ZTEST(ecp_active, test_enum_again_while_active)
{
	struct ecp_request_enum q = {.ecp_type = ECP_TEST_ENUM_TYPE, .ecp_revision = 2};

	ecp_test_expect(ECP_CMD_ENUM, 1, &q, sizeof(q), ECP_RES_SUCCESS, &resp);

	/* Still active, with the same port */
	zassert_true(device_is_ready(ECP_TEST_PORT));
	zassert_equal(port_channel_count(PORT_1), ECP_TEST_NUM_CHANNELS);
	zassert_equal(port_fake_set_value_fake.call_count, 0, "Outputs changed");
	zassert_equal(port_fake_pause_fake.call_count, 0, "Channel paused");
	zassert_equal(port_fake_resume_fake.call_count, 0, "Channel resumed");
}

/* The port set up by the first ENUM can't change */
ZTEST(ecp_active, test_enum_other_type_while_active)
{
	struct ecp_request_enum q = {
		.ecp_type = (ECP_TEST_ENUM_TYPE == ECP_TYPE_DO_DI_5A) ? ECP_TYPE_DO_DI_13A
								       : ECP_TYPE_DO_DI_5A,
		.ecp_revision = 2,
	};
	struct ecp_request_io_read r = {.ch = PORT_CH_A};

	ecp_test_expect(ECP_CMD_ENUM, 1, &q, sizeof(q), ECP_RES_INVALID_PARAM, &resp);

	/* Still active */
	ecp_test_expect(ECP_CMD_IO_READ, 2, &r, sizeof(r), ECP_RES_SUCCESS, &resp);
}

ZTEST_SUITE(ecp_active, ecp_phase_active, NULL, NULL, NULL, NULL);
