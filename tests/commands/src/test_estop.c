/*
 * ESTOP in the active state. Runs last, because ESTOP returns the ECP to the
 * unconfigured state.
 */
#include <zephyr/ztest.h>

#include <dp/drivers/port.h>

#include "ecp_test.h"

static struct ecp_sim_response resp;

ZTEST(ecp_estop, test_estop_with_data)
{
	const uint8_t data = 0;

	ecp_test_expect(ECP_CMD_ESTOP, 1, &data, sizeof(data), ECP_RES_INVALID_COMMAND, &resp);
}

ZTEST(ecp_estop, test_estop_active)
{
	ecp_test_expect(ECP_CMD_ESTOP, 1, NULL, 0, ECP_RES_IN_PROGRESS, &resp);
	ecp_test_expect_in_progress_result(ECP_RES_SUCCESS);
}

/* Spec: ESTOP returns all I/O to a safe state and enters the unconfigured state */
ZTEST(ecp_estop, test_estop_enters_unconfigured)
{
	ECP_KNOWN_DEVIATION("ESTOP is not implemented");

	struct ecp_request_io_read q = {.ch = PORT_CH_A};

	ecp_test_expect(ECP_CMD_IO_READ, 2, &q, sizeof(q), ECP_RES_NOT_ALLOWED, &resp);
}

ZTEST_SUITE(ecp_estop, ecp_phase_estop, NULL, NULL, NULL, NULL);
