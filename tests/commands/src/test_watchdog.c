/*
 * Task watchdog: the command handler and the port handler keep feeding their channels
 * while the host is silent. If either stopped, the task watchdog would reset the ECP and
 * the test would never finish.
 */
#include <zephyr/ztest.h>

#include <dp/drivers/port.h>

#include "ecp_test.h"

static struct ecp_sim_response resp;

ZTEST(ecp_task_wdt, test_threads_feed_while_host_silent)
{
	k_msleep(5 * MAX(CONFIG_ECP_DEVICE_CMD_TASK_WDT_TIMEOUT_MS,
			 CONFIG_DP_PORTS_TASK_WDT_TIMEOUT_MS));

	struct ecp_request_io_read q = {.ch = PORT_CH_A};

	ecp_test_expect(ECP_CMD_IO_READ, 2, &q, sizeof(q), ECP_RES_SUCCESS, &resp);
}

/* After ENUM, so the port handler is running too */
ZTEST_SUITE(ecp_task_wdt, ecp_phase_active, NULL, NULL, NULL, NULL);
