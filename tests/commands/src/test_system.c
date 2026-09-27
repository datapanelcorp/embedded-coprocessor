/*
 * Commands available in both the unconfigured and active states
 */
#include <string.h>

#include <zephyr/ztest.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/app_version.h>

#include "ecp_test.h"

static struct ecp_sim_response resp;

ZTEST(ecp_system, test_proto_version)
{
	ecp_test_expect(ECP_CMD_PROTO_VERSION, 2, NULL, 0, ECP_RES_SUCCESS, &resp);
	zassert_equal(resp.data_len, sizeof(struct ecp_response_proto_version));

	struct ecp_response_proto_version r;
	memcpy(&r, resp.data, sizeof(r));
	zassert_equal(r.versions, BIT(ECP_PROTO_VERSION));
	zassert_equal(r.flags, ECP_PROTOCOL_INFO_IN_PROGRESS_SUPPORTED);
}

ZTEST(ecp_system, test_proto_version_unsupported_version)
{
	ecp_test_expect(ECP_CMD_PROTO_VERSION, 1, NULL, 0, ECP_RES_UNSUPPORTED_CMD_VERSION, &resp);
}

/* The host sizes requests using the advertised maximums, so they must actually work */
ZTEST(ecp_system, test_proto_version_max_sizes_usable)
{
	static uint8_t data[ECP_MAX_PAYLOAD_BYTES];
	const size_t overhead = ECP_REQUEST_HEADER_SIZE + ECP_DATA_CRC_SIZE;

	ecp_test_expect(ECP_CMD_PROTO_VERSION, 2, NULL, 0, ECP_RES_SUCCESS, &resp);

	struct ecp_response_proto_version r;
	memcpy(&r, resp.data, sizeof(r));
	zassert_true(r.max_request_packet_size > overhead);
	zassert_true(r.max_response_packet_size > overhead);

	size_t len = MIN(r.max_request_packet_size, r.max_response_packet_size) - overhead;
	zassert_true(len <= sizeof(data), "Advertised size %zu exceeds the protocol limit %u", len,
		     ECP_MAX_PAYLOAD_BYTES);

	for (size_t i = 0; i < len; i++) {
		data[i] = i * 7;
	}
	ecp_test_expect(ECP_CMD_ECHO, 1, data, len, ECP_RES_SUCCESS, &resp);
	zassert_equal(resp.data_len, len);
	zassert_mem_equal(resp.data, data, len);
}

ZTEST(ecp_system, test_features)
{
	ecp_test_expect(ECP_CMD_FEATURES, 1, NULL, 0, ECP_RES_SUCCESS, &resp);
	zassert_equal(resp.data_len, sizeof(struct ecp_response_features));

	uint32_t expected =
		COND_CODE_1(CONFIG_APP_ECP_IO, (ECP_FEATURE_CH_IO), (0)) |
		COND_CODE_1(CONFIG_APP_ECP_PEEK_POKE, (ECP_FEATURE_PEEK_POKE), (0)) |
		COND_CODE_1(CONFIG_APP_ECP_GPIO, (ECP_FEATURE_GPIO), (0)) |
		COND_CODE_1(CONFIG_APP_ECP_DEVICE_EVENTS, (ECP_FEATURE_DEVICE_EVENT), (0)) |
		COND_CODE_1(CONFIG_APP_ECP_COMM_STATS, (ECP_FEATURE_COMM_STATS), (0)) |
		COND_CODE_1(CONFIG_APP_ECP_LED, (ECP_FEATURE_LED), (0));
	zassert_equal(sys_get_le32(resp.data), expected, "flags=0x%08x expected 0x%08x",
		      sys_get_le32(resp.data), expected);
}

ZTEST(ecp_system, test_ident)
{
	struct ecp_request_ident q = {.major = 1, .sw_part_number = "45116-560"};

	ecp_test_expect(ECP_CMD_IDENT, 1, &q, sizeof(q), ECP_RES_SUCCESS, &resp);
	zassert_equal(resp.data_len, sizeof(struct ecp_response_ident));

	struct ecp_response_ident r;
	memcpy(&r, resp.data, sizeof(r));
	zassert_equal(r.major, APP_VERSION_MAJOR);
	zassert_equal(r.minor, APP_VERSION_MINOR);
	zassert_equal(r.patch, APP_PATCHLEVEL);
	zassert_equal(r.build, APP_TWEAK);
	zassert_mem_equal(r.sw_part_number, CONFIG_APP_ECP_SW_PART_NUMBER,
			  sizeof(CONFIG_APP_ECP_SW_PART_NUMBER));
}

/* A complete request with less data than the command needs */
ZTEST(ecp_system, test_ident_request_too_small)
{
	struct ecp_request_ident q = {0};

	ecp_test_expect(ECP_CMD_IDENT, 1, &q, sizeof(q) - 1, ECP_RES_INVALID_PARAM, &resp);
}

ZTEST(ecp_system, test_hello)
{
	struct ecp_request_hello q = {.in_data = 0xFFFFFFF0};

	ecp_test_expect(ECP_CMD_HELLO, 1, &q, sizeof(q), ECP_RES_SUCCESS, &resp);
	zassert_equal(resp.data_len, sizeof(struct ecp_response_hello));
	/* Wraps around */
	zassert_equal(sys_get_le32(resp.data), 0xFFFFFFF0 + 0x01020304);
}

/* A complete request with less data than the command needs */
ZTEST(ecp_system, test_hello_request_too_small)
{
	struct ecp_request_hello q = {0};

	ecp_test_expect(ECP_CMD_HELLO, 1, &q, sizeof(q) - 1, ECP_RES_INVALID_PARAM, &resp);
}

ZTEST(ecp_system, test_echo)
{
	static uint8_t data[ECP_MAX_PAYLOAD_BYTES];
	const uint16_t lengths[] = {0, 1, 2, 255, 256, ECP_MAX_PAYLOAD_BYTES};

	for (size_t i = 0; i < sizeof(data); i++) {
		data[i] = i ^ 0xA5;
	}

	ARRAY_FOR_EACH(lengths, i) {
		ecp_test_expect(ECP_CMD_ECHO, 1, data, lengths[i], ECP_RES_SUCCESS, &resp);
		zassert_equal(resp.data_len, lengths[i]);
		zassert_mem_equal(resp.data, data, lengths[i], "Length %u", lengths[i]);
	}
}

ZTEST(ecp_system, test_uptime)
{
	uint32_t before = k_uptime_get_32();

	ecp_test_expect(ECP_CMD_UPTIME, 1, NULL, 0, ECP_RES_SUCCESS, &resp);
	zassert_equal(resp.data_len, sizeof(struct ecp_response_uptime));

	struct ecp_response_uptime r;
	memcpy(&r, resp.data, sizeof(r));
	zassert_between_inclusive(r.uptime_ms, before, k_uptime_get_32());
	/* Values 0-7 are defined. native_sim can't tell, so it must use 0 (UNKNOWN). */
	zassert_equal(r.reset_reason, 0);
}

static void get_comm_stats(struct ecp_response_comm_stats *stats)
{
	ecp_test_expect(ECP_CMD_COMM_STATS, 1, NULL, 0, ECP_RES_SUCCESS, &resp);
	zassert_equal(resp.data_len, sizeof(*stats));
	memcpy(stats, resp.data, sizeof(*stats));
}

ZTEST(ecp_system, test_comm_stats)
{
	Z_TEST_SKIP_IFNDEF(CONFIG_APP_ECP_COMM_STATS);

	struct ecp_response_comm_stats before, after;
	static uint8_t frame[64];

	get_comm_stats(&before);

	/* One request that fails, and one duplicate */
	ecp_test_expect(ECP_CMD_RESERVED, 1, NULL, 0, ECP_RES_INVALID_COMMAND, &resp);

	struct ecp_request_hello q = {0};
	size_t len = ecp_sim_host_build_request(frame, sizeof(frame), ECP_CMD_HELLO, 1, 3, false,
						&q, sizeof(q));
	ecp_sim_host_send_raw(frame, len, &resp);
	len = ecp_sim_host_build_request(frame, sizeof(frame), ECP_CMD_HELLO, 1, 3, true, &q,
					 sizeof(q));
	ecp_sim_host_send_raw(frame, len, &resp);
	zassert_true(resp.seq_dup);

	get_comm_stats(&after);

	/* The 3 requests above, plus the request for the stats themselves */
	zassert_equal(after.request_count - before.request_count, 4);
	zassert_equal(after.error_count - before.error_count, 1);
	zassert_equal(after.dup_count - before.dup_count, 1);
	zassert_equal(after.request_bytes - before.request_bytes,
		      ECP_REQUEST_HEADER_SIZE +
			      2 * (ECP_REQUEST_HEADER_SIZE + sizeof(q) + ECP_DATA_CRC_SIZE) +
			      ECP_REQUEST_HEADER_SIZE);
	zassert_true(after.response_bytes > before.response_bytes);
}

/* Commands for features that are disabled must be rejected */
ZTEST(ecp_system, test_disabled_features)
{
	const struct {
		bool enabled;
		uint16_t command;
	} commands[] = {
		{IS_ENABLED(CONFIG_APP_ECP_PEEK_POKE), ECP_CMD_PEEK},
		{IS_ENABLED(CONFIG_APP_ECP_PEEK_POKE), ECP_CMD_POKE},
		{IS_ENABLED(CONFIG_APP_ECP_GPIO), ECP_CMD_GPIO_CONFIG},
		{IS_ENABLED(CONFIG_APP_ECP_GPIO), ECP_CMD_GPIO},
		{IS_ENABLED(CONFIG_APP_ECP_COMM_STATS), ECP_CMD_COMM_STATS},
		{IS_ENABLED(CONFIG_APP_ECP_LED), ECP_CMD_LED_COLOR},
		{IS_ENABLED(CONFIG_APP_ECP_LED), ECP_CMD_LED_BRIGHTNESS},
		{IS_ENABLED(CONFIG_APP_ECP_DEVICE_EVENTS), ECP_CMD_DEVICE_EVENT},
	};

	ARRAY_FOR_EACH_PTR(commands, c) {
		if (!c->enabled) {
			ecp_test_expect(c->command, 1, NULL, 0, ECP_RES_INVALID_COMMAND, &resp);
		}
	}
}

ZTEST_SUITE(ecp_system, ecp_phase_any, NULL, NULL, NULL, NULL);
