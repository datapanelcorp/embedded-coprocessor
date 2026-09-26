/*
 * PEEK and POKE, against buffers in the test. On native_sim, every address is allowed.
 */
#include <string.h>

#include <zephyr/ztest.h>

#include "ecp_test.h"

static struct ecp_sim_response resp;

/* Aligned, so 2 and 4 byte POKEs are aligned accesses as the target requires */
static uint8_t target[ECP_MAX_PAYLOAD_BYTES + 1] __aligned(4);

static uint32_t address_of(const void *p)
{
	return (uint32_t)(uintptr_t)p;
}

static void peek_poke_before(void *fixture)
{
	ARG_UNUSED(fixture);

	for (size_t i = 0; i < sizeof(target); i++) {
		target[i] = i * 3;
	}
}

static enum ecp_result_code peek(const void *address, uint16_t count)
{
	struct ecp_request_peek q = {.address = address_of(address), .count = count};

	return ecp_sim_host_transact(ECP_CMD_PEEK, 1, &q, sizeof(q), &resp);
}

static enum ecp_result_code poke(void *address, const void *data, uint16_t count)
{
	static uint8_t q_buf[sizeof(struct ecp_request_poke) + ECP_MAX_PAYLOAD_BYTES];
	struct ecp_request_poke *q = (void *)q_buf;

	q->address = address_of(address);
	q->count = count;
	memcpy(q->data, data, count);

	return ecp_sim_host_transact(ECP_CMD_POKE, 1, q, sizeof(*q) + count, &resp);
}

ZTEST(ecp_peek_poke, test_peek)
{
	const uint16_t counts[] = {1, 2, 3, 4, 100, ecp_sim_host_max_response_data()};

	ARRAY_FOR_EACH(counts, i) {
		zassert_equal(peek(&target[1], counts[i]), ECP_RES_SUCCESS, "count %u", counts[i]);
		zassert_equal(resp.data_len, counts[i]);
		zassert_mem_equal(resp.data, &target[1], counts[i], "count %u", counts[i]);
	}
}

ZTEST(ecp_peek_poke, test_peek_invalid)
{
	zassert_equal(peek(target, 0), ECP_RES_INVALID_PARAM);
	zassert_equal(peek(target, ecp_sim_host_max_response_data() + 1), ECP_RES_INVALID_PARAM);

	/* One byte short, and one byte extra */
	uint8_t q_buf[sizeof(struct ecp_request_peek) + 1] = {0};
	struct ecp_request_peek *q = (void *)q_buf;

	q->address = address_of(target);
	q->count = 1;
	ecp_test_expect(ECP_CMD_PEEK, 1, q, sizeof(*q) - 1, ECP_RES_INVALID_PARAM, &resp);
	ecp_test_expect(ECP_CMD_PEEK, 1, q, sizeof(*q) + 1, ECP_RES_INVALID_PARAM, &resp);
}

ZTEST(ecp_peek_poke, test_poke)
{
	/* 1, 2 and 4 bytes are written as a single access; others with memcpy */
	const uint16_t counts[] = {1, 2, 3, 4, 5, 100};
	uint8_t data[100];

	ARRAY_FOR_EACH(counts, i) {
		uint8_t expected[sizeof(target)];

		for (int j = 0; j < counts[i]; j++) {
			data[j] = 0xF0 ^ (i + j);
		}
		memcpy(expected, target, sizeof(target));
		memcpy(&expected[4], data, counts[i]);

		zassert_equal(poke(&target[4], data, counts[i]), ECP_RES_SUCCESS, "count %u",
			      counts[i]);
		zassert_equal(resp.data_len, 0);
		/* Written, and nothing else touched */
		zassert_mem_equal(target, expected, sizeof(target), "count %u", counts[i]);
	}
}

ZTEST(ecp_peek_poke, test_poke_invalid)
{
	static uint8_t q_buf[sizeof(struct ecp_request_poke) + 4];
	struct ecp_request_poke *q = (void *)q_buf;
	uint8_t data = 0xAA;
	uint8_t before[sizeof(target)];

	memcpy(before, target, sizeof(target));

	zassert_equal(poke(target, &data, 0), ECP_RES_INVALID_PARAM);

	/* count doesn't match the data length */
	q->address = address_of(target);
	q->count = 4;
	ecp_test_expect(ECP_CMD_POKE, 1, q, sizeof(*q) + 3, ECP_RES_INVALID_PARAM, &resp);
	q->count = 2;
	ecp_test_expect(ECP_CMD_POKE, 1, q, sizeof(*q) + 3, ECP_RES_INVALID_PARAM, &resp);

	/* Shorter than the address and count */
	ecp_test_expect(ECP_CMD_POKE, 1, q, sizeof(*q) - 1, ECP_RES_INVALID_COMMAND, &resp);

	zassert_mem_equal(target, before, sizeof(target), "Invalid request wrote memory");
}

ZTEST_SUITE(ecp_peek_poke, ecp_phase_any, NULL, peek_poke_before, NULL, NULL);
