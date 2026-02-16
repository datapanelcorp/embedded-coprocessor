#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>

#include "app_version.h"
#include "dp/ecp.h"
#include "dp/ecp-target.h"

#include "dp/drivers/port.h"

#include <zephyr/logging/log.h>
#define LOG_LEVEL CONFIG_I2C_LOG_LEVEL
LOG_MODULE_DECLARE(i2c_target);

enum ecp_result_code ecp_target_cmd_handler_NOP(const struct device *dev, const void *qdata,
						uint16_t qlen, void *rdata, uint16_t *rlen)
{
	*rlen = 0;
	if (qlen != 0) {
		return ECP_RES_INVALID_COMMAND;
	}
	return ECP_RES_SUCCESS;
}
ECP_CMD_DEFINE(NOP, ECP_CMD_F_ALWAYS);

enum ecp_result_code ecp_target_cmd_handler_PING(const struct device *dev, const void *qdata,
						 uint16_t qlen, void *rdata, uint16_t *rlen)
{
	if (qlen != sizeof(struct ecp_cmd_data_req_PING)) {
		*rlen = 0;
		return ECP_RES_INVALID_COMMAND;
	}
	const struct ecp_cmd_data_req_PING *q = (struct ecp_cmd_data_req_PING *)qdata;
	struct ecp_cmd_data_res_PING *r = (struct ecp_cmd_data_res_PING *)rdata;

	r->pattern = q->pattern ^ 0x12345678;
	*rlen = sizeof(*r);

	return ECP_RES_SUCCESS;
}
ECP_CMD_DEFINE(PING, ECP_CMD_F_ALWAYS);

enum ecp_result_code ecp_target_cmd_handler_INIT(const struct device *dev, const void *qdata,
						 uint16_t qlen, void *rdata, uint16_t *rlen)
{
	if (qlen != sizeof(struct ecp_cmd_data_req_INIT)) {
		*rlen = 0;
		return ECP_RES_INVALID_COMMAND;
	}
	const struct ecp_cmd_data_req_INIT *q = (struct ecp_cmd_data_req_INIT *)qdata;

	if (q->port_index >= 8) {
		return ECP_RES_INVALID_PARAMETER;
	}

	int ret = ecp_target_enumerate(dev, q->port_index, q->port_type, q->port_variant,
				       q->port_revision);
	*rlen = 0;

	switch (ret) {
	case 0:
		return ECP_RES_SUCCESS;
	case -EPERM:
		return ECP_RES_ACCESS_DENIED;
	case -EINVAL:
		return ECP_RES_INVALID_PARAMETER;
	default:
		return ECP_RES_ERROR;
	}
}
ECP_CMD_DEFINE(INIT, ECP_CMD_F_PRE_INIT);

static const struct ecp_cmd_data_res_IDENT ident = {
	.major = APP_VERSION_MAJOR,
	.minor = APP_VERSION_MINOR,
	.patch = APP_PATCHLEVEL,
	.build = APP_TWEAK,
	.features = 0,
	.sw_part_number = "43044-561",
};

enum ecp_result_code ecp_target_cmd_handler_IDENT(const struct device *dev, const void *qdata,
						  uint16_t qlen, void *rdata, uint16_t *rlen)
{
	if (qlen != sizeof(struct ecp_cmd_data_req_IDENT)) {
		*rlen = 0;
		return ECP_RES_INVALID_COMMAND;
	}

	memcpy(rdata, &ident, sizeof(ident));
	*rlen = sizeof(ident);

	return ECP_RES_SUCCESS;
}
ECP_CMD_DEFINE(IDENT, ECP_CMD_F_ALWAYS);

enum ecp_result_code ecp_target_cmd_handler_REBOOT(const struct device *dev, const void *qdata,
						   uint16_t qlen, void *rdata, uint16_t *rlen)
{
	if (qlen != sizeof(struct ecp_cmd_data_req_REBOOT)) {
		*rlen = 0;
		return ECP_RES_INVALID_COMMAND;
	}
	*rlen = 0;
	int ret = ecp_target_reboot(dev);

	return (ret == 0) ? ECP_RES_SUCCESS : ECP_RES_ERROR;
}
ECP_CMD_DEFINE(REBOOT, ECP_CMD_F_ALWAYS);

enum ecp_result_code ecp_target_cmd_handler_ESTOP(const struct device *dev, const void *qdata,
						  uint16_t qlen, void *rdata, uint16_t *rlen)
{
	if (qlen != sizeof(struct ecp_cmd_data_req_ESTOP)) {
		*rlen = 0;
		return ECP_RES_INVALID_COMMAND;
	}
	*rlen = 0;
	int ret = ecp_target_estop(dev);
	return (ret == 0) ? ECP_RES_SUCCESS : ECP_RES_ERROR;
}
ECP_CMD_DEFINE(ESTOP, ECP_CMD_F_ALWAYS);

enum ecp_result_code ecp_target_cmd_handler_CH_GET_ATTRIB(const struct device *dev,
							  const void *qdata, uint16_t qlen,
							  void *rdata, uint16_t *rlen)
{
	if (qlen != sizeof(struct ecp_cmd_data_req_CH_GET_ATTRIB)) {
		*rlen = 0;
		return ECP_RES_INVALID_COMMAND;
	}

	const struct ecp_cmd_data_req_CH_GET_ATTRIB *q = qdata;

	uintptr_t value;
	int ret = port_channel_get_attribute(PORT_1, (enum port_channel_id)q->ch,
					     (enum port_channel_attribute_id)q->attr, &value);
	if (ret != 0) {
		*rlen = 0;
		return ECP_RES_INVALID_PARAMETER;
	}
	struct ecp_cmd_data_res_CH_GET_ATTRIB *r = rdata;
	r->ch = q->ch;
	r->attr = r->attr;
	r->value = (uint32_t)value;

	*rlen = sizeof(*r);

	return ECP_RES_SUCCESS;
}
ECP_CMD_DEFINE(CH_GET_ATTRIB, ECP_CMD_F_POST_INIT);

enum ecp_result_code ecp_target_cmd_handler_CH_SET_ATTRIB(const struct device *dev,
							  const void *qdata, uint16_t qlen,
							  void *rdata, uint16_t *rlen)
{
	if (qlen != sizeof(struct ecp_cmd_data_req_CH_SET_ATTRIB)) {
		*rlen = 0;
		return ECP_RES_INVALID_COMMAND;
	}

	const struct ecp_cmd_data_req_CH_SET_ATTRIB *q = qdata;

	uintptr_t value = (uintptr_t)q->value;
	int ret = port_channel_set_attribute(PORT_1, (enum port_channel_id)q->ch,
					     (enum port_channel_attribute_id)q->attr, value);
	if (ret != 0) {
		*rlen = 0;
		return ECP_RES_INVALID_PARAMETER;
	}
	struct ecp_cmd_data_res_CH_SET_ATTRIB *r = rdata;
	*rlen = sizeof(*r);

	return ECP_RES_SUCCESS;
}
ECP_CMD_DEFINE(CH_SET_ATTRIB, ECP_CMD_F_POST_INIT);

enum ecp_result_code ecp_target_cmd_handler_CH_STAT(const struct device *dev, const void *qdata,
						    uint16_t qlen, void *rdata, uint16_t *rlen)
{
	if (qlen != sizeof(struct ecp_cmd_data_req_CH_STAT)) {
		*rlen = 0;
		return ECP_RES_INVALID_COMMAND;
	}

	const struct ecp_cmd_data_req_CH_STAT *q = qdata;

	struct ecp_cmd_data_res_CH_STAT *r = rdata;
	r->ch = q->ch;
	enum port_channel_id chid = (enum port_channel_id)q->ch;

	int v = -1;
	int ret = port_channel_get_value(PORT_1, chid, &v);
	r->value = (ret == 0) ? v : 0;

	ret = port_channel_get_raw_value(PORT_1, chid, &v);
	r->raw = (ret == 0) ? v : 0;

	ret = port_channel_get_current(PORT_1, chid, &v);
	r->current_mA = (ret == 0) ? v : 0;

	ret = port_channel_get_temperature(PORT_1, chid, &v);
	r->temp_mC = (ret == 0) ? v : 0;

	*rlen = sizeof(*r);

	return ECP_RES_SUCCESS;
}
ECP_CMD_DEFINE(CH_STAT, ECP_CMD_F_POST_INIT);

enum ecp_result_code ecp_target_cmd_handler_CH_SET(const struct device *dev, const void *qdata,
						   uint16_t qlen, void *rdata, uint16_t *rlen)
{
	if (qlen != sizeof(struct ecp_cmd_data_req_CH_SET)) {
		*rlen = 0;
		return ECP_RES_INVALID_COMMAND;
	}

	const struct ecp_cmd_data_req_CH_SET *q = qdata;

	struct ecp_cmd_data_res_CH_SET *r = rdata;
	r->ch = q->ch;
	enum port_channel_id chid = (enum port_channel_id)q->ch;

	int ret = port_channel_set_value(PORT_1, chid, q->value);
	if (ret != 0) {
		return ECP_RES_INVALID_PARAMETER;
	}
	*rlen = sizeof(*r);

	return ECP_RES_SUCCESS;
}
ECP_CMD_DEFINE(CH_SET, ECP_CMD_F_POST_INIT);
