/*
 * REBOOT and BOOT_JUMP. These can't return, so each gets its own test binary.
 */
#include <zephyr/ztest.h>
#include <zephyr/sys/reboot.h>

#include <dp/drivers/port_fake.h>

#include "ecp_test.h"

#define COMMAND CONFIG_TEST_ECP_REBOOT_COMMAND

static struct ecp_sim_response resp;

static K_SEM_DEFINE(rebooted, 0, 1);
static int reboot_type = -1;

/* Port calls made before the reboot */
static unsigned int pauses_before_reboot;
static unsigned int sensor_power_offs_before_reboot;

/* Replaces the real sys_reboot(). The calling thread never returns, as if the ECP had
 * restarted.
 */
FUNC_NORETURN void sys_reboot(int type)
{
	reboot_type = type;
	pauses_before_reboot = port_fake_pause_fake.call_count;
	for (unsigned int i = 0; i < port_fake_set_sensor_power_fake.call_count &&
				 i < FFF_ARG_HISTORY_LEN;
	     i++) {
		if (port_fake_set_sensor_power_fake.arg2_history[i] == 0) {
			sensor_power_offs_before_reboot++;
		}
	}
	k_sem_give(&rebooted);

	while (true) {
		k_sleep(K_FOREVER);
	}
}

ZTEST(ecp_reboot, test_1_reboots)
{
	BUILD_ASSERT(COMMAND == ECP_CMD_REBOOT || COMMAND == ECP_CMD_BOOT_JUMP);

	if (IS_ENABLED(CONFIG_TEST_ECP_REBOOT_WHEN_ACTIVE)) {
		struct ecp_request_enum q = {.ecp_type = ECP_TEST_ENUM_TYPE, .ecp_revision = 2};

		ecp_test_expect(ECP_CMD_ENUM, 1, &q, sizeof(q), ECP_RES_SUCCESS, &resp);
		port_fake_reset();
	}

	ecp_test_expect(COMMAND, 1, NULL, 0, ECP_RES_IN_PROGRESS, &resp);

	zassert_ok(k_sem_take(&rebooted, K_SECONDS(1)), "Did not reboot");
	zassert_equal(reboot_type, SYS_REBOOT_COLD);

	/* Everything ESTOP does happens first: each channel paused, and its sensor power off */
	if (IS_ENABLED(CONFIG_TEST_ECP_REBOOT_WHEN_ACTIVE)) {
		zassert_equal(pauses_before_reboot, ECP_TEST_NUM_CHANNELS);
		zassert_equal(sensor_power_offs_before_reboot, ECP_TEST_NUM_CHANNELS);
	} else {
		zassert_equal(pauses_before_reboot, 0);
	}
}

/* Spec: BUSY if a conflicting command is still pending. Here, the reboot never
 * finishes, so another long-running command can't start.
 */
ZTEST(ecp_reboot, test_2_estop_while_pending)
{
	ecp_test_expect(ECP_CMD_ESTOP, 1, NULL, 0, ECP_RES_BUSY, &resp);
}

/* REBOOT and BOOT_JUMP are available in both states. The suite runs in the unconfigured
 * phase, and the .active scenarios send ENUM first.
 */
ZTEST_SUITE(ecp_reboot, ecp_phase_unconfigured, NULL, NULL, NULL, NULL);
