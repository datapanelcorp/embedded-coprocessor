#ifndef ECP_TEST_H_
#define ECP_TEST_H_

#include <zephyr/device.h>
#include <zephyr/ztest.h>

#include <dp/ecp/protocol.h>

#include "ecp_sim_host.h"

/*
 * The ECP starts unconfigured and becomes active after ENUM, which can't be undone
 * without a reboot. test_main() runs the suites in phases, and each suite's predicate
 * selects the phases it runs in.
 */
enum ecp_test_phase {
	ECP_PHASE_UNCONFIGURED,
	ECP_PHASE_ENUM,
	ECP_PHASE_ACTIVE,
	/* Last, because ESTOP returns the ECP to the unconfigured state */
	ECP_PHASE_ESTOP,
};

struct ecp_test_state {
	enum ecp_test_phase phase;
};

bool ecp_phase_unconfigured(const void *state);
bool ecp_phase_enum(const void *state);
bool ecp_phase_active(const void *state);
bool ecp_phase_estop(const void *state);
/* Both unconfigured and active, for commands available in either state */
bool ecp_phase_any(const void *state);

/* The port that ENUM activates */
#if defined(CONFIG_TEST_ECP_ENUM_13A)
#define ECP_TEST_ENUM_TYPE ECP_TYPE_DO_DI_13A
#define ECP_TEST_PORT      DEVICE_DT_GET(DT_NODELABEL(port1_13a))
#define ECP_TEST_OTHER_PORT DEVICE_DT_GET(DT_NODELABEL(port1_5a))
#else
#define ECP_TEST_ENUM_TYPE ECP_TYPE_DO_DI_5A
#define ECP_TEST_PORT      DEVICE_DT_GET(DT_NODELABEL(port1_5a))
#define ECP_TEST_OTHER_PORT DEVICE_DT_GET(DT_NODELABEL(port1_13a))
#endif

#define ECP_TEST_NUM_CHANNELS 2

/**
 * Skip the rest of the test unless CONFIG_TEST_ECP_KNOWN_DEVIATIONS is set. Use at the
 * start of a test that checks behavior required by the spec but not yet implemented.
 */
#define ECP_KNOWN_DEVIATION(_why)                                                                  \
	do {                                                                                       \
		if (!IS_ENABLED(CONFIG_TEST_ECP_KNOWN_DEVIATIONS)) {                               \
			TC_PRINT("Known deviation from spec: %s\n", _why);                         \
			ztest_test_skip();                                                         \
		}                                                                                  \
	} while (0)

/**
 * Send a request and check the response's result code
 */
void ecp_test_expect(uint16_t command, uint8_t version, const void *data, uint16_t data_len,
		     enum ecp_result_code expected, struct ecp_sim_response *resp);

/**
 * Wait for the command that returned ECP_RES_IN_PROGRESS to finish, and check its
 * final result
 */
void ecp_test_expect_in_progress_result(enum ecp_result_code expected);

#endif /* ECP_TEST_H_ */
