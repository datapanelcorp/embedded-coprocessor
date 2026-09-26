/*
 * LED_COLOR and LED_BRIGHTNESS. The application doesn't map any LEDs yet, so every
 * LED index is out of range.
 */
#include <zephyr/ztest.h>

#include "ecp_test.h"

static struct ecp_sim_response resp;

ZTEST(ecp_led, test_led_color_no_leds)
{
	const uint8_t q[] = {0, 3, 0x10, 0x20, 0x30};

	ecp_test_expect(ECP_CMD_LED_COLOR, 1, q, sizeof(q), ECP_RES_INVALID_PARAM, &resp);
}

ZTEST(ecp_led, test_led_color_length_mismatch)
{
	/* 3 colors, but 2 provided */
	const uint8_t q[] = {0, 3, 0x10, 0x20};

	ecp_test_expect(ECP_CMD_LED_COLOR, 1, q, sizeof(q), ECP_RES_INVALID_COMMAND, &resp);
}

ZTEST(ecp_led, test_led_brightness_no_leds)
{
	struct ecp_request_led_brightness q = {.led = 0, .brightness = 128};

	ecp_test_expect(ECP_CMD_LED_BRIGHTNESS, 1, &q, sizeof(q), ECP_RES_INVALID_PARAM, &resp);

	/* All LEDs: there are none, so nothing to fail */
	q.led = 0xFF;
	ecp_test_expect(ECP_CMD_LED_BRIGHTNESS, 1, &q, sizeof(q), ECP_RES_SUCCESS, &resp);
}

ZTEST_SUITE(ecp_led, ecp_phase_active, NULL, NULL, NULL, NULL);
