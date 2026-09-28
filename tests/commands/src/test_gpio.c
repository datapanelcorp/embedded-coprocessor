/*
 * GPIO_CONFIG and GPIO. The ECP exposes 6 virtual pins (the RGB LEDs for channels A and
 * B), mapped to gpio0 pins 0-5 on native_sim. The LEDs are active low.
 */
#include <zephyr/ztest.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/sys/byteorder.h>

#include "ecp_test.h"

#define GPIO_DEV       DEVICE_DT_GET(DT_NODELABEL(gpio0))
#define NUM_PINS       6
#define SUPPORTED_PINS BIT_MASK(NUM_PINS)

static struct ecp_sim_response resp;

/* Returns the direction bits: 0 for output, 1 for input */
static uint16_t gpio_config(uint16_t direction, uint16_t mask)
{
	struct ecp_request_gpio_config q = {.direction = direction, .mask = mask};

	ecp_test_expect(ECP_CMD_GPIO_CONFIG, 1, &q, sizeof(q), ECP_RES_SUCCESS, &resp);
	zassert_equal(resp.data_len, sizeof(struct ecp_response_gpio_config));
	return sys_get_le16(resp.data);
}

/* Returns the input bits */
static uint16_t gpio(uint16_t outputs, uint16_t mask)
{
	struct ecp_request_gpio q = {.outputs = outputs, .mask = mask};

	ecp_test_expect(ECP_CMD_GPIO, 1, &q, sizeof(q), ECP_RES_SUCCESS, &resp);
	zassert_equal(resp.data_len, sizeof(struct ecp_response_gpio));
	return sys_get_le16(resp.data);
}

static void gpio_before(void *fixture)
{
	ARG_UNUSED(fixture);

	/* All supported pins are outputs, and off */
	gpio_config(0, SUPPORTED_PINS);
	gpio(0, SUPPORTED_PINS);
}

ZTEST(ecp_gpio, test_gpio_config)
{
	/* Unsupported pins always read as inputs */
	zassert_equal(gpio_config(0, 0), (uint16_t)~SUPPORTED_PINS);

	zassert_equal(gpio_config(BIT(0) | BIT(4), BIT(0) | BIT(4)),
		      (uint16_t)~SUPPORTED_PINS | BIT(0) | BIT(4));

	/* Bits outside the mask aren't changed */
	zassert_equal(gpio_config(0xFFFF, BIT(1)),
		      (uint16_t)~SUPPORTED_PINS | BIT(0) | BIT(1) | BIT(4));
	zassert_equal(gpio_config(0, BIT(0)), (uint16_t)~SUPPORTED_PINS | BIT(1) | BIT(4));
}

ZTEST(ecp_gpio, test_gpio_config_unsupported_pins_ignored)
{
	zassert_equal(gpio_config(0, 0xFFFF), (uint16_t)~SUPPORTED_PINS);
	zassert_equal(gpio_config(0xFFFF, (uint16_t)~SUPPORTED_PINS), (uint16_t)~SUPPORTED_PINS);
}

ZTEST(ecp_gpio, test_gpio_outputs)
{
	for (int pin = 0; pin < NUM_PINS; pin++) {
		gpio(BIT(pin), BIT(pin));
		/* Active low */
		zassert_equal(gpio_emul_output_get(GPIO_DEV, pin), 0, "Pin %d not on", pin);

		gpio(0, BIT(pin));
		zassert_equal(gpio_emul_output_get(GPIO_DEV, pin), 1, "Pin %d not off", pin);
	}

	/* Bits outside the mask aren't changed */
	gpio(BIT(1), BIT(1));
	gpio(BIT(2), BIT(2));
	zassert_equal(gpio_emul_output_get(GPIO_DEV, 1), 0);
	zassert_equal(gpio_emul_output_get(GPIO_DEV, 2), 0);
}

ZTEST(ecp_gpio, test_gpio_inputs)
{
	gpio_config(BIT(3), BIT(3));

	/* Active low */
	zassert_ok(gpio_emul_input_set(GPIO_DEV, 3, 0));
	zassert_equal(gpio(0, 0), BIT(3));

	zassert_ok(gpio_emul_input_set(GPIO_DEV, 3, 1));
	zassert_equal(gpio(0, 0), 0);

	/* Spec: attempts to set inputs are ignored */
	zassert_equal(gpio(BIT(3), BIT(3)), 0);
	zassert_equal(gpio_config(0, 0), (uint16_t)~SUPPORTED_PINS | BIT(3), "Pin became an output");
}

ZTEST_SUITE(ecp_gpio, ecp_phase_any, NULL, gpio_before, NULL, NULL);
