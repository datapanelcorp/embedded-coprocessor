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

/* Spec: ENUM, REBOOT and BOOT_JUMP are not available in the active state */
ZTEST(ecp_active, test_enum_not_allowed)
{
	/* Skip before sending: a second ENUM restarts the port handler thread while it's
	 * running.
	 */
	ECP_KNOWN_DEVIATION("ENUM is accepted in the active state");

	struct ecp_request_enum q = {.ecp_type = ECP_TEST_ENUM_TYPE, .ecp_revision = 2};

	ecp_test_expect(ECP_CMD_ENUM, 1, &q, sizeof(q), ECP_RES_NOT_ALLOWED, &resp);
}

ZTEST(ecp_active, test_reboot_not_allowed)
{
	/* Skip before sending, because the ECP would reboot */
	ECP_KNOWN_DEVIATION("REBOOT is accepted in the active state");

	ecp_test_expect(ECP_CMD_REBOOT, 1, NULL, 0, ECP_RES_NOT_ALLOWED, &resp);
}

ZTEST(ecp_active, test_boot_jump_not_allowed)
{
	/* Skip before sending, because the ECP would reboot */
	ECP_KNOWN_DEVIATION("BOOT_JUMP is accepted in the active state");

	ecp_test_expect(ECP_CMD_BOOT_JUMP, 1, NULL, 0, ECP_RES_NOT_ALLOWED, &resp);
}

ZTEST_SUITE(ecp_active, ecp_phase_active, NULL, NULL, NULL, NULL);
