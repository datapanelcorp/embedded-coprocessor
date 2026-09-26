/*
 * Tests for the ECP command handlers in src/commands.c. Requests go through the real
 * device command handler via the simulator backend; the ports are fakes.
 */
#include <zephyr/ztest.h>
#include <zephyr/fff.h>

#include <dp/ecp/ecp_device_cmd/ecp_device_cmd.h>

#include "ecp_test.h"

DEFINE_FFF_GLOBALS;

static bool in_phase(const void *state, enum ecp_test_phase phase)
{
	const struct ecp_test_state *s = state;

	return s != NULL && s->phase == phase;
}

bool ecp_phase_unconfigured(const void *state)
{
	return in_phase(state, ECP_PHASE_UNCONFIGURED);
}

bool ecp_phase_enum(const void *state)
{
	return in_phase(state, ECP_PHASE_ENUM);
}

bool ecp_phase_active(const void *state)
{
	return in_phase(state, ECP_PHASE_ACTIVE);
}

bool ecp_phase_estop(const void *state)
{
	return in_phase(state, ECP_PHASE_ESTOP);
}

bool ecp_phase_any(const void *state)
{
	return ecp_phase_unconfigured(state) || ecp_phase_active(state);
}

void ecp_test_expect(uint16_t command, uint8_t version, const void *data, uint16_t data_len,
		     enum ecp_result_code expected, struct ecp_sim_response *resp)
{
	enum ecp_result_code result =
		ecp_sim_host_transact(command, version, data, data_len, resp);

	zassert_equal(result, expected, "Command 0x%04x v%u: expected result %d, got %d",
		      command, version, expected, result);
	if (result != ECP_RES_SUCCESS && result != ECP_RES_IN_PROGRESS) {
		zassert_equal(resp->data_len, 0, "Error response has %u data bytes",
			      resp->data_len);
	}
}

void ecp_test_expect_in_progress_result(enum ecp_result_code expected)
{
	for (int i = 0; i < 100 && !ecp_device_cmd_send_in_progress_ended(); i++) {
		k_msleep(1);
	}
	zassert_true(ecp_device_cmd_send_in_progress_ended(), "Command did not finish");

	enum ecp_result_code result = ecp_device_cmd_send_in_progress_status();
	zassert_equal(result, expected, "Expected final result %d, got %d", expected, result);
}

static void *ecp_host_setup(void)
{
	ecp_sim_host_init();
	return NULL;
}

void test_main(void)
{
	struct ecp_test_state state;

	ecp_host_setup();

	for (enum ecp_test_phase phase = ECP_PHASE_UNCONFIGURED; phase <= ECP_PHASE_ESTOP;
	     phase++) {
		state.phase = phase;
		ztest_run_all(&state, false, 1, 1);
	}

	ztest_verify_all_test_suites_ran();
}
