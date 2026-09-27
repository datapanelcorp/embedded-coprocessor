/*
 * REBOOT and BOOT_JUMP. These can't return, so each gets its own test binary.
 */
#include <zephyr/ztest.h>
#include <zephyr/sys/reboot.h>

#include "ecp_test.h"

#define COMMAND CONFIG_TEST_ECP_REBOOT_COMMAND

static struct ecp_sim_response resp;

static K_SEM_DEFINE(rebooted, 0, 1);
static int reboot_type = -1;

/* Replaces the real sys_reboot(). The calling thread never returns, as if the ECP had
 * restarted.
 */
FUNC_NORETURN void sys_reboot(int type)
{
	reboot_type = type;
	k_sem_give(&rebooted);

	while (true) {
		k_sleep(K_FOREVER);
	}
}

ZTEST(ecp_reboot, test_1_reboots)
{
	BUILD_ASSERT(COMMAND == ECP_CMD_REBOOT || COMMAND == ECP_CMD_BOOT_JUMP);

	ecp_test_expect(COMMAND, 1, NULL, 0, ECP_RES_IN_PROGRESS, &resp);

	zassert_ok(k_sem_take(&rebooted, K_SECONDS(1)), "Did not reboot");
	zassert_equal(reboot_type, SYS_REBOOT_COLD);
}

/* Spec: BUSY if a conflicting command is still pending. Here, the reboot never
 * finishes, so another long-running command can't start.
 */
ZTEST(ecp_reboot, test_2_estop_while_pending)
{
	ecp_test_expect(ECP_CMD_ESTOP, 1, NULL, 0, ECP_RES_BUSY, &resp);
}

/* Spec: REBOOT and BOOT_JUMP are available in the unconfigured state */
ZTEST_SUITE(ecp_reboot, ecp_phase_unconfigured, NULL, NULL, NULL, NULL);
