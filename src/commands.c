#include <string.h>

#include <zephyr/kernel.h>
#include <sys/errno.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/led.h>
#include <zephyr/sys/math_extras.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/check.h>

#include "app_version.h"
#include "dp/ecp/ecp_device_cmd/ecp_device_cmd.h"
#include "dp/ecp/protocol.h"
#include "dp/dp-bindesc.h"

#include "dp/drivers/port.h"

#include "dp/metrics.h"

LOG_MODULE_REGISTER(ecp_commands, CONFIG_LOG_DEFAULT_LEVEL);

static enum ecp_result_code ecp_device_cmd_proto(struct ecp_device_cmd_handler_args *args)
{
	struct ecp_response_proto_version *r = (void *)args->rdata;
	r->versions = BIT(ECP_PROTO_VERSION);
	/* Limited by the buffers, and by the largest payload the header can describe */
	r->max_request_packet_size =
		MIN(CONFIG_ECP_DEVICE_CMD_HANDLER_RX_BUFFER_SIZE,
		    ECP_REQUEST_HEADER_SIZE + ECP_MAX_PAYLOAD_BYTES + ECP_DATA_CRC_SIZE);
	r->max_response_packet_size =
		MIN(CONFIG_ECP_DEVICE_CMD_HANDLER_TX_BUFFER_SIZE,
		    ECP_RESPONSE_HEADER_SIZE + ECP_MAX_PAYLOAD_BYTES + ECP_DATA_CRC_SIZE);
	r->flags = 0;
#if defined(CONFIG_ECP_DEVICE_CMD_IN_PROGRESS_STATUS)
	r->flags |= ECP_PROTOCOL_INFO_IN_PROGRESS_SUPPORTED;
#endif
	args->rdata_len = sizeof(*r);

	return ECP_RES_SUCCESS;
}
ECP_DEVICE_CMD_HANDLER_RESP_ONLY(ECP_CMD_PROTO_VERSION, ecp_device_cmd_proto, ECP_VER_MASK(2),
				 struct ecp_response_proto_version);

extern struct bindesc_entry bindesc_entry_dp_sw_part_number;

static enum ecp_result_code ecp_device_cmd_ident(struct ecp_device_cmd_handler_args *args)
{
	/* The host has provided ident data in the request, but we don't necessarily need
	 * anything from it.
	 */
	struct ecp_request_ident *q = (void *)args->qdata;
	LOG_DBG("ECP host %s %d.%d.%d+%d", q->sw_part_number, q->major, q->minor, q->patch,
		q->build);

	struct ecp_response_ident *r = (void *)args->rdata;
	*r = (const struct ecp_response_ident){
		.major = APP_VERSION_MAJOR,
		.minor = APP_VERSION_MINOR,
		.patch = APP_PATCHLEVEL,
		.build = APP_TWEAK,
	};
	strncpy(r->sw_part_number, BINDESC_GET_STR(dp_sw_part_number), sizeof(r->sw_part_number));

	args->rdata_len = sizeof(*r);

	return ECP_RES_SUCCESS;
}
ECP_DEVICE_CMD_HANDLER(ECP_CMD_IDENT, ecp_device_cmd_ident, ECP_VER_MASK(1),
		       struct ecp_request_ident, struct ecp_response_ident);

static void port_handler_thread(void *p1, void *p2, void *p3)
{
	const struct port_init_data *pidata = p1;
	int nports = *(int *)p2;

	return ports_task(pidata, nports);
}

/* The ECP is unconfigured until ENUM succeeds, and then active (ECP protocol specification,
 * "Network layer"). Some commands are only available in one of the states.
 */
static bool ecp_active;

static struct k_thread port_thread;
#define PORT_HANDLER_STACK_SIZE 1024
#define PORT_HANDLER_PRIORITY   4
static K_KERNEL_STACK_DEFINE(port_handler_stack, PORT_HANDLER_STACK_SIZE);
static int config_activate(uint8_t offset, enum ecp_type ecp_type, uint8_t ecp_revision)
{
	/* The port handler thread keeps using these */
	static struct port_init_data pidata;
	static int nports = 1;

	pidata = (struct port_init_data){
		.port = 1,
		.dev = NULL,
	};

	switch (ecp_type) {
	case ECP_TYPE_DO_DI_5A:
		if (ecp_revision == 2) {
			pidata.dev = DEVICE_DT_GET(DT_NODELABEL(port1_5a));
		}
		break;
	case ECP_TYPE_DO_DI_13A:
		if (ecp_revision == 2) {
			pidata.dev = DEVICE_DT_GET(DT_NODELABEL(port1_13a));
		}
		break;
	default:
		break;
	}

	if (pidata.dev == NULL) {
		return -ENOTSUP;
	}

	int ret = device_init(DEVICE_DT_GET(DT_NODELABEL(port)));
	if (ret != 0) {
		LOG_ERR("Could not initialize port controller: %d", ret);
		return -ENODEV;
	}

	ret = device_init(pidata.dev);
	if (ret != 0) {
		LOG_ERR("Could not initialize PORT_%c: %d", '1' + pidata.port, ret);
	}

	k_thread_create(&port_thread, port_handler_stack, PORT_HANDLER_STACK_SIZE,
			port_handler_thread, (void *)&pidata, (void *)&nports, NULL,
			PORT_HANDLER_PRIORITY, K_ESSENTIAL, K_NO_WAIT);
	k_thread_name_set(&port_thread, "ecp_port");

	return 0;
}

static enum ecp_result_code ecp_device_cmd_enum(struct ecp_device_cmd_handler_args *args)
{
	if (ecp_active) {
		return ECP_RES_NOT_ALLOWED;
	}

	const struct ecp_request_enum *q = (void *)args->qdata;
	LOG_INF("enumerating... offset=%d type=%d rev=%d", q->offset, q->ecp_type, q->ecp_revision);

	int ret = config_activate(q->offset, q->ecp_type, q->ecp_revision);

	args->rdata_len = 0;

	switch (ret) {
	case -EINVAL:
	case -ENOTSUP:
		return ECP_RES_INVALID_PARAM;
	case 0:
		ecp_active = true;
		return ECP_RES_SUCCESS;
	default:
		return ECP_RES_ERROR;
	}
}
ECP_DEVICE_CMD_HANDLER_REQ_ONLY(ECP_CMD_ENUM, ecp_device_cmd_enum, BIT(1), struct ecp_request_enum);

static enum ecp_result_code ecp_device_cmd_hello(struct ecp_device_cmd_handler_args *args)
{
	LOG_INF("HELLO");
	const struct ecp_request_hello *q = (void *)args->qdata;
	struct ecp_response_hello *r = (void *)args->rdata;

	r->out_data = q->in_data + 0x01020304;
	args->rdata_len = sizeof(*r);

	return ECP_RES_SUCCESS;
}
ECP_DEVICE_CMD_HANDLER(ECP_CMD_HELLO, ecp_device_cmd_hello, BIT(1), struct ecp_request_hello,
		       struct ecp_response_hello);

static enum ecp_result_code ecp_device_cmd_echo(struct ecp_device_cmd_handler_args *args)
{
	if (args->qdata_len > args->rdata_max) {
		return ECP_RES_RESPONSE_TOO_BIG;
	}
	args->rdata_len = args->qdata_len;
	memcpy(args->rdata, args->qdata, args->qdata_len);

	return ECP_RES_SUCCESS;
}
ECP_DEVICE_CMD_HANDLER_UNBOUND(ECP_CMD_ECHO, ecp_device_cmd_echo, BIT(1));

static enum ecp_result_code reboot_continue(void *user_data)
{
	sys_reboot(SYS_REBOOT_COLD);

	// If reboot returns, there was an error
	return ECP_RES_ERROR;
}

static enum ecp_result_code ecp_device_cmd_reboot(struct ecp_device_cmd_handler_args *args)
{
	if (args->qdata_len != 0) {
		return ECP_RES_INVALID_COMMAND;
	}
	args->rdata_len = 0;

	/* BUSY if another long-running command hasn't finished */
	enum ecp_result_code result = ecp_device_cmd_send_in_progress_continue(reboot_continue, NULL);

	return (result == ECP_RES_SUCCESS) ? ECP_RES_IN_PROGRESS : result;
}
ECP_DEVICE_CMD_HANDLER_UNBOUND(ECP_CMD_REBOOT, ecp_device_cmd_reboot, BIT(1));

static enum ecp_result_code ecp_device_cmd_features(struct ecp_device_cmd_handler_args *args)
{
	struct ecp_response_features *r = (void *)args->rdata;
	args->rdata_len = sizeof(*r);
	r->flags = COND_CODE_1(CONFIG_APP_ECP_IO, (ECP_FEATURE_CH_IO), (0)) |
		   COND_CODE_1(CONFIG_APP_ECP_PEEK_POKE, (ECP_FEATURE_PEEK_POKE), (0)) |
		   COND_CODE_1(CONFIG_APP_ECP_GPIO, (ECP_FEATURE_GPIO), (0)) |
		   COND_CODE_1(CONFIG_APP_ECP_DEVICE_EVENTS, (ECP_FEATURE_DEVICE_EVENT), (0)) |
		   COND_CODE_1(CONFIG_APP_ECP_COMM_STATS, (ECP_FEATURE_COMM_STATS), (0)) |
		   COND_CODE_1(CONFIG_APP_ECP_LED, (ECP_FEATURE_LED), (0));

	return ECP_RES_SUCCESS;
}
ECP_DEVICE_CMD_HANDLER_RESP_ONLY(ECP_CMD_FEATURES, ecp_device_cmd_features, BIT(1),
				 struct ecp_response_features);

static enum ecp_result_code estop_continue(void *user_data)
{
	// TODO: get to a safe state
	return ECP_RES_SUCCESS;
}

static enum ecp_result_code ecp_device_cmd_estop(struct ecp_device_cmd_handler_args *args)
{
	if (args->qdata_len != 0) {
		return ECP_RES_INVALID_COMMAND;
	}
	args->rdata_len = 0;

	/* BUSY if another long-running command hasn't finished */
	enum ecp_result_code result = ecp_device_cmd_send_in_progress_continue(estop_continue, NULL);

	return (result == ECP_RES_SUCCESS) ? ECP_RES_IN_PROGRESS : result;
}
ECP_DEVICE_CMD_HANDLER_UNBOUND(ECP_CMD_ESTOP, ecp_device_cmd_estop, BIT(1));

static enum ecp_result_code boot_jump_continue(void *user_data)
{
	// TODO: set up to enter bootloader
	sys_reboot(SYS_REBOOT_COLD);
	return ECP_RES_ERROR;
}

static enum ecp_result_code ecp_device_cmd_boot_jump(struct ecp_device_cmd_handler_args *args)
{
	if (args->qdata_len != 0) {
		return ECP_RES_INVALID_COMMAND;
	}
	args->rdata_len = 0;

	/* BUSY if another long-running command hasn't finished */
	enum ecp_result_code result = ecp_device_cmd_send_in_progress_continue(boot_jump_continue, NULL);

	return (result == ECP_RES_SUCCESS) ? ECP_RES_IN_PROGRESS : result;
}
ECP_DEVICE_CMD_HANDLER_UNBOUND(ECP_CMD_BOOT_JUMP, ecp_device_cmd_boot_jump, BIT(1));

#if defined(CONFIG_APP_ECP_IO)

static bool channel_valid(uint8_t ch)
{
	int nchannels = port_channel_count(PORT_1);

	return nchannels > 0 && ch < nchannels;
}

static int prepare_io_read_response(struct ecp_response_io_read *r, enum port_channel_id ch)
{
	CHECKIF(r == NULL) {
		return -EINVAL;
	}

	uintptr_t value;
	int ret = port_channel_get_attribute(PORT_1, ch, PORT_CH_ATTRIB_MODE, &value);
	if (ret != 0) {
		return ret;
	}

	uint32_t raw = -1UL;
	port_channel_get_raw_value(PORT_1, ch, &raw);

	int scaled = INT_MIN;
	port_channel_get_value(PORT_1, ch, &scaled);

	int voltage = INT_MIN;
	port_channel_get_analog_value(PORT_1, ch, &voltage);

	int current = INT_MIN;
	port_channel_get_current(PORT_1, ch, &current);

	int temperature = INT_MIN;
	port_channel_get_temperature(PORT_1, ch, &temperature);

	enum port_channel_fault_status fault_status = PORT_CH_FAULT_OTHER;
	port_channel_get_fault_status(PORT_1, ch, &fault_status);

	enum port_channel_status ch_status = PORT_CH_STATUS_NA;
	port_channel_get_status(PORT_1, ch, &ch_status);

	int spwr = -1;
	port_channel_get_sensor_power(PORT_1, ch, &spwr);

	bool paused = port_channel_paused(PORT_1, ch) == 1;

	int softswitch = -1;
	port_channel_get_soft_switch_status(PORT_1, ch, &softswitch);

	r->ch = ch;
	r->mode = (uint8_t)value;
	r->raw = raw;
	r->scaled = scaled;
	r->voltage = voltage;
	r->current = current;
	r->temperature = temperature;
	r->fault_status = fault_status;
	r->hb_direction = PORT_HBRIDGE_DIR_INVALID;
	r->flags = FIELD_PREP(ECP_IO_F_CH_STATUS_MASK, ch_status) |
		   FIELD_PREP(ECP_IO_F_SPWR_STATUS_MASK, spwr) |
		   FIELD_PREP(ECP_IO_F_SOFT_SWITCH_STATUS_MASK, softswitch) |
		   FIELD_PREP(ECP_IO_F_PAUSED, paused);

	return 0;
}

static enum ecp_result_code ecp_device_cmd_io_read_single(struct ecp_device_cmd_handler_args *args)
{
	if (args->qdata_len != sizeof(struct ecp_request_io_read)) {
		return ECP_RES_INVALID_PARAM;
	}
	const struct ecp_request_io_read *q = (void *)args->qdata;
	if (!channel_valid(q->ch)) {
		return ECP_RES_INVALID_PARAM;
	}
	enum port_channel_id ch = (enum port_channel_id)q->ch;
	struct ecp_response_io_read *r = (void *)args->rdata;

	if (prepare_io_read_response(r, ch) != 0) {
		args->rdata_len = 0;
		return ECP_RES_ERROR;
	}
	args->rdata_len = sizeof(*r);

	return ECP_RES_SUCCESS;
}

static enum ecp_result_code ecp_device_cmd_io_read_all(struct ecp_device_cmd_handler_args *args)
{
	if (args->qdata_len != 0) {
		return ECP_RES_INVALID_PARAM;
	}
	struct ecp_response_io_read3 *r = (void *)args->rdata;

	r->nchannels = port_channel_count(PORT_1);

	struct ecp_response_io_read *chdata = (struct ecp_response_io_read *)&args->rdata[1];
	for (enum port_channel_id ch = PORT_CH_A; ch < r->nchannels; ch++) {
		if (prepare_io_read_response(chdata, ch) != 0) {
			args->rdata_len = 0;
			return ECP_RES_ERROR;
		}
		chdata++;
	}

	args->rdata_len = sizeof(*r) + r->nchannels * sizeof(*chdata);

	return ECP_RES_SUCCESS;
}

static enum ecp_result_code ecp_device_cmd_io_read(struct ecp_device_cmd_handler_args *args)
{
	if (!ecp_active) {
		return ECP_RES_NOT_ALLOWED;
	}

	switch (args->version) {
	case 2:
		return ecp_device_cmd_io_read_single(args);
	case 3:
		return ecp_device_cmd_io_read_all(args);
	default:
		return ECP_RES_UNSUPPORTED_CMD_VERSION;
	}
}
ECP_DEVICE_CMD_HANDLER_UNBOUND(ECP_CMD_IO_READ, ecp_device_cmd_io_read, BIT(2) | BIT(3));

static enum ecp_result_code ecp_device_cmd_io_write(struct ecp_device_cmd_handler_args *args)
{
	if (!ecp_active) {
		return ECP_RES_NOT_ALLOWED;
	}
	const struct ecp_request_io_write *q = (void *)args->qdata;
	if (!channel_valid(q->ch)) {
		return ECP_RES_INVALID_PARAM;
	}

	int ret = port_channel_write(PORT_1, q->ch, q->value);

	return (ret == 0) ? ECP_RES_SUCCESS : ECP_RES_ERROR;
}
ECP_DEVICE_CMD_HANDLER_REQ_ONLY(ECP_CMD_IO_WRITE, ecp_device_cmd_io_write, BIT(1),
				struct ecp_request_io_write);

enum ecp_result_code ecp_device_cmd_io_get_attrib(struct ecp_device_cmd_handler_args *args)
{
	if (!ecp_active) {
		return ECP_RES_NOT_ALLOWED;
	}
	const struct ecp_request_io_get_attrib *q = (void *)args->qdata;
	if (!channel_valid(q->ch)) {
		return ECP_RES_INVALID_PARAM;
	}
	struct ecp_response_io_get_attrib *r = (void *)args->rdata;

	uintptr_t value;
	int ret = port_channel_get_attribute(PORT_1, (enum port_channel_id)q->ch,
					     (enum port_channel_attribute_id)q->attrib_id, &value);
	switch (ret) {
	case 0:
		args->rdata_len = sizeof(*r);
		r->ch = q->ch;
		r->attrib_id = q->attrib_id;
		r->value = (uint32_t)value;
		return ECP_RES_SUCCESS;
	case -ENOTSUP:
		args->rdata_len = 0;
		return ECP_RES_NOT_SUPPORTED;
	default:
		args->rdata_len = 0;
		return ECP_RES_ERROR;
	}
}
ECP_DEVICE_CMD_HANDLER(ECP_CMD_IO_GET_ATTRIB, ecp_device_cmd_io_get_attrib, BIT(1),
		       struct ecp_request_io_get_attrib, struct ecp_response_io_get_attrib);

static enum ecp_result_code ecp_device_cmd_io_set_attrib(struct ecp_device_cmd_handler_args *args)
{
	args->rdata_len = 0;
	if (!ecp_active) {
		return ECP_RES_NOT_ALLOWED;
	}
	const struct ecp_request_io_set_attrib *q = (void *)args->qdata;
	if (!channel_valid(q->ch)) {
		return ECP_RES_INVALID_PARAM;
	}

	uintptr_t value = (uintptr_t)q->value;
	int ret = port_channel_set_attribute(PORT_1, (enum port_channel_id)q->ch,
					     (enum port_channel_attribute_id)q->attrib_id, value);
	switch (ret) {
	case 0:
		return ECP_RES_SUCCESS;
	case -ENOTSUP:
		return ECP_RES_NOT_SUPPORTED;
	case -EINVAL:
	default:
		LOG_ERR("port_channel_set_attribute(PORT_1, PORT_CH_%c, %d, %lu) => %d",
			'A' + q->ch, q->attrib_id, value, ret);
		return ECP_RES_ERROR;
	}
}
ECP_DEVICE_CMD_HANDLER_REQ_ONLY(ECP_CMD_IO_SET_ATTRIB, ecp_device_cmd_io_set_attrib, BIT(1),
				struct ecp_request_io_set_attrib);

static enum ecp_result_code ecp_device_cmd_io_pause(struct ecp_device_cmd_handler_args *args)
{
	const struct ecp_request_io_pause *q = (void *)args->qdata;
	args->rdata_len = 0;
	if (!ecp_active) {
		return ECP_RES_NOT_ALLOWED;
	}

	/* Check every channel before pausing or resuming any of them */
	int nchannels = port_channel_count(PORT_1);
	if (nchannels <= 0 || (q->ch_mask & ~BIT_MASK(nchannels)) != 0) {
		return ECP_RES_INVALID_PARAM;
	}

	// For each bit in the mask, pause or resume the corresponding channel
	uint32_t mask = q->ch_mask;
	while (mask > 0) {
		int index = u32_count_trailing_zeros(mask);
		enum port_channel_id ch = index;

		bool pause = q->pause & BIT(index);
		int ret = pause ? port_channel_pause(PORT_1, ch) : port_channel_resume(PORT_1, ch);

		if (ret != 0) {
			return ECP_RES_INVALID_PARAM;
		}

		// Use Kernighan's trick to clear lowest set bit
		mask &= (mask - 1);
	}

	struct ecp_response_io_pause *r = (void *)args->rdata;
	args->rdata_len = sizeof(*r);
	r->paused = 0;

	for (enum port_channel_id ch = PORT_CH_A; ch < nchannels; ch++) {
		if (port_channel_paused(PORT_1, ch) == 1) {
			r->paused |= BIT(ch);
		}
	}

	return ECP_RES_SUCCESS;
}
ECP_DEVICE_CMD_HANDLER(ECP_CMD_IO_PAUSE, ecp_device_cmd_io_pause, BIT(1),
		       struct ecp_request_io_pause, struct ecp_response_io_pause);

static enum ecp_result_code ecp_device_cmd_io_clear_fault(struct ecp_device_cmd_handler_args *args)
{
	args->rdata_len = 0;
	if (!ecp_active) {
		return ECP_RES_NOT_ALLOWED;
	}
	const struct ecp_request_io_clear_fault *q = (void *)args->qdata;
	if (!channel_valid(q->ch)) {
		return ECP_RES_INVALID_PARAM;
	}

	enum port_channel_id ch = (enum port_channel_id)q->ch;
	if (port_channel_clear_fault(PORT_1, ch) != 0) {
		return ECP_RES_ERROR;
	}
	return ECP_RES_SUCCESS;
}
ECP_DEVICE_CMD_HANDLER_REQ_ONLY(ECP_CMD_IO_CLEAR_FAULT, ecp_device_cmd_io_clear_fault, BIT(1),
				struct ecp_request_io_clear_fault);

#endif /* CONFIG_APP_ECP_IO */

static enum ecp_result_code ecp_device_cmd_uptime(struct ecp_device_cmd_handler_args *args)
{
	struct ecp_response_uptime *r = (void *)args->rdata;
	r->uptime_ms = k_uptime_get_32();
	struct dp_metrics metrics;
	int ret = dp_metrics_latest(&metrics);
	if (ret != 0) {
		args->rdata_len = 0;
		return ECP_RES_ERROR;
	}

	r->reset_reason = metrics.reset_reason;
	args->rdata_len = sizeof(*r);

	return ECP_RES_SUCCESS;
}
ECP_DEVICE_CMD_HANDLER_RESP_ONLY(ECP_CMD_UPTIME, ecp_device_cmd_uptime, BIT(1),
				 struct ecp_response_uptime);

#if defined(CONFIG_APP_ECP_LED)
struct ecp_led {
	const struct device *dev;
	uint8_t index;
	uint8_t color_count;
};

static const struct ecp_led supported_leds[] = {};

static enum ecp_result_code ecp_device_cmd_led_color(struct ecp_device_cmd_handler_args *args)
{
	struct ecp_request_led_color *q = (void *)args->qdata;
	// The correct length depends on how many colors there are.
	if ((args->qdata_len < 3) || (args->qdata_len != (2 + q->color_count * sizeof(uint8_t)))) {
		return ECP_RES_INVALID_COMMAND;
	}
	args->rdata_len = 0;

	if (q->led >= ARRAY_SIZE(supported_leds)) {
		return ECP_RES_INVALID_PARAM;
	}

	const struct ecp_led *led = &supported_leds[q->led];

	if (q->color_count > led->color_count) {
		return ECP_RES_INVALID_PARAM;
	}
	int ret = led_set_color(led->dev, led->index, q->color_count, q->colors);
	if (ret != 0) {
		return ECP_RES_ERROR;
	}
	return ECP_RES_SUCCESS;
}
ECP_DEVICE_CMD_HANDLER_UNBOUND(ECP_CMD_LED_COLOR, ecp_device_cmd_led_color, BIT(1));

static enum ecp_result_code ecp_device_cmd_led_brightness(struct ecp_device_cmd_handler_args *args)
{
	struct ecp_request_led_brightness *q = (void *)args->qdata;
	args->rdata_len = 0;

	// 0xFF indicates all supported LEDs
	if (q->led == 0xFF) {
		for (int i = 0; i < ARRAY_SIZE(supported_leds); i++) {
			const struct ecp_led *led = &supported_leds[i];
			int ret = led_set_brightness(led->dev, led->index, q->brightness);
			if (ret != 0) {
				return ECP_RES_ERROR;
			}
		}
		return ECP_RES_SUCCESS;
	} else if (q->led >= ARRAY_SIZE(supported_leds)) {
		return ECP_RES_INVALID_PARAM;
	}
	// Otherwise, set a single LED
	const struct ecp_led *led = &supported_leds[q->led];
	int ret = led_set_brightness(led->dev, led->index, q->brightness);

	return (ret == 0) ? ECP_RES_SUCCESS : ECP_RES_ERROR;
}
ECP_DEVICE_CMD_HANDLER_REQ_ONLY(ECP_CMD_LED_BRIGHTNESS, ecp_device_cmd_led_brightness, BIT(1),
				struct ecp_request_led_brightness);
#endif /* CONFIG_APP_ECP_LED */

#if defined(CONFIG_APP_ECP_PEEK_POKE)
static bool allow_peek_poke(uint32_t addr)
{
#if defined(CONFIG_SOC_SERIES_STM32G0X)
	/* This isn't intended to be a super-rigorous check. It's based on the memory map
	 * of the STM32G0x1. No attempt is made to validate against the size of flash/ram
	 * or the presence of specific peripherals. There are several gaps within these valid
	 * ranges that are reserved and will cause bus errors on read.
	 */
	return IN_RANGE(addr, 0x00000000, 0x1FFFFFFF) || // code (flash, OTP, option bytes)
	       IN_RANGE(addr, 0x20000000, 0x20024000) || // RAM
	       IN_RANGE(addr, 0x40000000, 0x50001FFF) || // Peripherals
	       IN_RANGE(addr, 0xE0000000, 0xE1000000);   // Cortex M0+ internal peripherals
#else
	return true;
#endif
}

static enum ecp_result_code ecp_device_cmd_peek(struct ecp_device_cmd_handler_args *args)
{
	args->rdata_len = 0;
	if (args->qdata_len != sizeof(struct ecp_request_peek)) {
		return ECP_RES_INVALID_PARAM;
	}

	const struct ecp_request_peek *q = (void *)args->qdata;
	if ((q->count == 0) || (q->count > args->rdata_max)) {
		return ECP_RES_INVALID_PARAM;
	}

	if (!allow_peek_poke(q->address)) {
		return ECP_RES_NOT_ALLOWED;
	}

	args->rdata_len = q->count;
	memcpy(args->rdata, (void *)q->address, q->count);

	return ECP_RES_SUCCESS;
}
ECP_DEVICE_CMD_HANDLER_UNBOUND(ECP_CMD_PEEK, ecp_device_cmd_peek, BIT(1));

static enum ecp_result_code ecp_device_cmd_poke(struct ecp_device_cmd_handler_args *args)
{
	args->rdata_len = 0;
	if (args->qdata_len < sizeof(struct ecp_request_poke)) {
		return ECP_RES_INVALID_COMMAND;
	}

	const struct ecp_request_poke *q = (void *)args->qdata;

	if ((q->count == 0) || (q->count > args->rdata_max) ||
	    (args->qdata_len != sizeof(struct ecp_request_poke) + q->count * sizeof(uint8_t))) {
		return ECP_RES_INVALID_PARAM;
	}

	if (!allow_peek_poke(q->address)) {
		return ECP_RES_NOT_ALLOWED;
	}

	// Special case some sizes so that registers can be written atomically
	if (q->count == 1) {
		uint8_t *dest = (uint8_t *)q->address;
		*dest = q->data[0];
	} else if (q->count == 2) {
		uint16_t *dest = (uint16_t *)q->address;
		uint16_t value;
		memcpy(&value, q->data, sizeof(value));
		*dest = value;
	} else if (q->count == 4) {
		uint32_t *dest = (uint32_t *)q->address;
		uint32_t value;
		memcpy(&value, q->data, sizeof(value));
		*dest = value;
	} else {
		memcpy((void *)q->address, q->data, q->count);
	}

	return ECP_RES_SUCCESS;
}
ECP_DEVICE_CMD_HANDLER_UNBOUND(ECP_CMD_POKE, ecp_device_cmd_poke, BIT(1));
#endif /* CONFIG_APP_ECP_PEEK_POKE */

#if defined(CONFIG_APP_ECP_COMM_STATS)
static enum ecp_result_code ecp_device_cmd_comm_stats(struct ecp_device_cmd_handler_args *args)
{
	struct ecp_device_cmd_stats stats;
	int ret = ecp_device_cmd_stats_get(&stats);
	if (ret != 0) {
		args->rdata_len = 0;
		return ECP_RES_ERROR;
	}

	struct ecp_response_comm_stats *r = (void *)args->rdata;
	r->request_count = stats.qcount;
	r->error_count = stats.errcount;
	r->dup_count = stats.dupcount;
	r->request_bytes = stats.qbytes;
	r->response_bytes = stats.rbytes;
	args->rdata_len = sizeof(*r);

	return ECP_RES_SUCCESS;
}
ECP_DEVICE_CMD_HANDLER_RESP_ONLY(ECP_CMD_COMM_STATS, ecp_device_cmd_comm_stats, BIT(1),
				 struct ecp_response_comm_stats);
#endif

#if defined(CONFIG_APP_ECP_GPIO)
struct ecp_gpio {
	struct gpio_dt_spec spec;
	uint8_t virt_pin;
	bool output;
};

#define ECP_GPIO_OUTPUT(_node, _pin)                                                               \
	{.spec = GPIO_DT_SPEC_GET(_node, gpios), .virt_pin = _pin, .output = true}
#define ECP_GPIO_INPUT(_node, _pin)                                                                \
	{.spec = GPIO_DT_SPEC_GET(_node, gpios), .virt_pin = _pin, .output = false}

static struct ecp_gpio supported_gpio[] = {
	ECP_GPIO_OUTPUT(DT_NODELABEL(led_r_a), 0), // channel A red
	ECP_GPIO_OUTPUT(DT_NODELABEL(led_g_a), 1), // channel A green
	ECP_GPIO_OUTPUT(DT_NODELABEL(led_b_a), 2), // channel A blue
	ECP_GPIO_OUTPUT(DT_NODELABEL(led_r_b), 3), // channel B red
	ECP_GPIO_OUTPUT(DT_NODELABEL(led_g_b), 4), // channel B green
	ECP_GPIO_OUTPUT(DT_NODELABEL(led_b_b), 5), // channel B blue
};

static int setup_ecp_gpio(void)
{
	for (int i = 0; i < ARRAY_SIZE(supported_gpio); i++) {
		struct ecp_gpio *gpio = &supported_gpio[i];
		gpio_pin_configure_dt(&gpio->spec, gpio->output ? GPIO_OUTPUT | GPIO_OUTPUT_INACTIVE
								: GPIO_INPUT);
	}
	return 0;
}
// Should be initialized just after GPIO
SYS_INIT(setup_ecp_gpio, POST_KERNEL, 41);

static enum ecp_result_code ecp_device_cmd_gpio_config(struct ecp_device_cmd_handler_args *args)
{
	const struct ecp_request_gpio_config *q = (void *)args->qdata;
	struct ecp_response_gpio_config *r = (void *)args->rdata;

	// Ignore any pins we don't support. The direction in the
	// response will serve as feedback.
	//

	// Default to all inputs
	r->direction = UINT16_MAX;
	for (int i = 0; i < ARRAY_SIZE(supported_gpio); i++) {
		struct ecp_gpio *gpio = &supported_gpio[i];
		if (q->mask & BIT(gpio->virt_pin)) {
			bool is_input = q->direction & BIT(gpio->virt_pin);
			gpio->output = !is_input;
			int ret = gpio_pin_configure_dt(
				&gpio->spec,
				gpio->output ? GPIO_OUTPUT | GPIO_OUTPUT_INACTIVE : GPIO_INPUT);
			if (ret != 0) {
				args->rdata_len = 0;
				return ECP_RES_ERROR;
			}
		}

		// Clear bit for outputs; inputs are already set
		if (gpio->output) {
			r->direction &= ~BIT(gpio->virt_pin);
		}
	}

	args->rdata_len = sizeof(*r);
	return ECP_RES_SUCCESS;
}
ECP_DEVICE_CMD_HANDLER(ECP_CMD_GPIO_CONFIG, ecp_device_cmd_gpio_config, BIT(1),
		       struct ecp_request_gpio_config, struct ecp_response_gpio_config);

static enum ecp_result_code ecp_device_cmd_gpio(struct ecp_device_cmd_handler_args *args)
{
	const struct ecp_request_gpio *q = (void *)args->qdata;
	struct ecp_response_gpio *r = (void *)args->rdata;

	r->inputs = 0;
	for (int i = 0; i < ARRAY_SIZE(supported_gpio); i++) {
		struct ecp_gpio *gpio = &supported_gpio[i];

		if (q->mask & BIT(gpio->virt_pin)) {
			// This pin is being set
			if (gpio->output) {
				// Ignore attempts to set inputs
				int value = (q->outputs & BIT(gpio->virt_pin)) ? 1 : 0;
				int ret = gpio_pin_set_dt(&gpio->spec, value);
				if (ret != 0) {
					args->rdata_len = 0;
					return ECP_RES_ERROR;
				}
			}
		}
		// Always update response value for supported GPIO
		if (!gpio->output) {
			r->inputs |= gpio_pin_get_dt(&gpio->spec) << gpio->virt_pin;
		}
	}

	args->rdata_len = sizeof(*r);
	return ECP_RES_SUCCESS;
}
ECP_DEVICE_CMD_HANDLER(ECP_CMD_GPIO, ecp_device_cmd_gpio, BIT(1), struct ecp_request_gpio,
		       struct ecp_response_gpio);
#endif /* CONFIG_APP_ECP_GPIO */
