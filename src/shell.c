#include <zephyr/shell/shell.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/comparator.h>
#include <zephyr/app_version.h>

static const struct device *vload_dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(vload));
static const struct device *ntc_dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(pcb_ntc));
static const struct device *csense_a_dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(current_sense_a));
static const struct device *csense_b_dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(current_sense_b));
static const struct device *analog_in_a_dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(analog_in_a));
static const struct device *analog_in_b_dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(analog_in_b));
static const struct device *encoder_in_dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(qdec));
static const struct device *comp2_dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(comp2));
static const struct device *comp1_dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(comp1));

const struct gpio_dt_spec fault_out_a_gpio =
	GPIO_DT_SPEC_GET_BY_IDX(DT_NODELABEL(port1), fault_gpios, 0);
const struct gpio_dt_spec fault_out_b_gpio =
	GPIO_DT_SPEC_GET_BY_IDX(DT_NODELABEL(port1), fault_gpios, 1);

const struct gpio_dt_spec feedback_out_a_gpio =
	GPIO_DT_SPEC_GET_BY_IDX(DT_NODELABEL(port1), output_feedback_gpios, 0);
const struct gpio_dt_spec feedback_out_b_gpio =
	GPIO_DT_SPEC_GET_BY_IDX(DT_NODELABEL(port1), output_feedback_gpios, 1);

static void shell_print_sensor_value_milli(const struct shell *sh, const char *label,
					   const char *units, struct sensor_value *val)
{
	shell_print(sh, "%s=%d.%03d %s", label, val->val1, val->val2 / 1000, units);
}

static void shell_print_gpio_state(const struct shell *sh, const struct gpio_dt_spec *gpio,
				   const char *label)
{
	int logical = gpio_pin_get_dt(gpio);
	int raw = gpio_pin_get_raw(gpio->port, gpio->pin);

	shell_print(sh, "%s=%d (raw=%d)", label, logical, raw);
}

static void shell_print_sensor_aux_value_milli(const struct shell *sh, const char *label,
					       const char *units_main,
					       struct sensor_value *val_main, const char *units_aux,
					       struct sensor_value *val_aux)
{
	shell_print(sh, "%s=%d.%03d %s   (%d.%03d %s)", label, val_main->val1,
		    val_main->val2 / 1000, units_main, val_aux->val1, val_aux->val2 / 1000,
		    units_aux);
}

static int cmd_sensors(const struct shell *sh, size_t argc, char **argv)
{
	sensor_sample_fetch(vload_dev);
	sensor_sample_fetch(analog_in_a_dev);
	sensor_sample_fetch(analog_in_b_dev);

	struct sensor_value val;
	int ret = sensor_channel_get(vload_dev, SENSOR_CHAN_VOLTAGE, &val);
	if (ret != 0) {
		return -EIO;
	}
	shell_print_sensor_value_milli(sh, "VLOAD_VSENSE", "V", &val);

	ret = sensor_channel_get(ntc_dev, SENSOR_CHAN_AMBIENT_TEMP, &val);
	if (ret != 0) {
		return -EIO;
	}
	shell_print_sensor_value_milli(sh, "TEMP", "°C", &val);

	ret = sensor_channel_get(encoder_in_dev, SENSOR_CHAN_ROTATION, &val);
	if (ret != 0) {
		return -EIO;
	}
	shell_print(sh, "ENCODER=%d counts", val.val1);
	shell_print(sh, "");

	ret = sensor_channel_get(csense_a_dev, SENSOR_CHAN_CURRENT, &val);
	if (ret != 0) {
		return -EIO;
	}
	struct sensor_value val_aux;
	ret = sensor_channel_get(csense_a_dev, SENSOR_CHAN_VOLTAGE, &val_aux);
	if (ret != 0) {
		return -EIO;
	}
	shell_print_sensor_aux_value_milli(sh, "CSENSE_OUT_A", "A", &val, "V", &val_aux);
	shell_print_gpio_state(sh, &fault_out_a_gpio, "FAULT_OUT_A");
	shell_print_gpio_state(sh, &feedback_out_a_gpio, "VSENSE_OUT_A");

	sensor_channel_get(analog_in_a_dev, SENSOR_CHAN_VOLTAGE, &val);
	shell_print_sensor_value_milli(sh, "ANALOG_IN_A", "V", &val);
	ret = comparator_get_output(comp2_dev);
	if (ret < 0) {
		return -EIO;
	}
	shell_print(sh, "COMP2_OUT_A=%d", ret);

	shell_print(sh, "");

	ret = sensor_channel_get(csense_b_dev, SENSOR_CHAN_CURRENT, &val);
	if (ret != 0) {
		return -EIO;
	}
	ret = sensor_channel_get(csense_b_dev, SENSOR_CHAN_VOLTAGE, &val_aux);
	if (ret != 0) {
		return -EIO;
	}
	shell_print_sensor_aux_value_milli(sh, "CSENSE_OUT_B", "A", &val, "V", &val_aux);

	shell_print_gpio_state(sh, &fault_out_b_gpio, "FAULT_OUT_B");
	shell_print_gpio_state(sh, &feedback_out_b_gpio, "VSENSE_OUT_B");

	sensor_channel_get(analog_in_b_dev, SENSOR_CHAN_VOLTAGE, &val);
	shell_print_sensor_value_milli(sh, "ANALOG_IN_B", "V", &val);

	ret = comparator_get_output(comp1_dev);
	if (ret < 0) {
		return -EIO;
	}
	shell_print(sh, "COMP1_OUT_B=%d", ret);

	return 0;
}

SHELL_CMD_ARG_REGISTER(sensors, NULL, "Read sensor data", cmd_sensors, 1, 0);

static int cmd_version(const struct shell *sh, size_t argc, char **argv)
{
	shell_print(sh, "%d.%d.%d+%d", APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_PATCHLEVEL,
		    APP_TWEAK);
	return 0;
}
SHELL_CMD_ARG_REGISTER(version, NULL, "Get version", cmd_version, 1, 0);
