/*
 * Task watchdog: each ECP thread that must keep running (command handler, port handler,
 * main loop) has a channel it feeds. The hardware watchdog backs the task watchdog up, and
 * is only fed while every channel is.
 */
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/task_wdt/task_wdt.h>

LOG_MODULE_REGISTER(ecp_watchdog, CONFIG_APP_LOG_LEVEL);

/* After the hardware watchdog driver, and before the command handler thread starts and
 * registers its channel
 */
#define ECP_WATCHDOG_INIT_PRIORITY 60
BUILD_ASSERT(ECP_WATCHDOG_INIT_PRIORITY > CONFIG_KERNEL_INIT_PRIORITY_DEVICE);
BUILD_ASSERT(ECP_WATCHDOG_INIT_PRIORITY < CONFIG_ECP_DEVICE_CMD_INIT_PRIORITY);

static int ecp_watchdog_init(void)
{
	const struct device *hw_wdt = NULL;

#if DT_NODE_HAS_STATUS(DT_ALIAS(watchdog0), okay)
	hw_wdt = DEVICE_DT_GET(DT_ALIAS(watchdog0));
	if (!device_is_ready(hw_wdt)) {
		LOG_ERR("%s: device not ready", hw_wdt->name);
		hw_wdt = NULL;
	}
#endif

	int ret = task_wdt_init(hw_wdt);

	if (ret != 0) {
		LOG_ERR("Task watchdog init failed: %d", ret);
	}
	return ret;
}
SYS_INIT(ecp_watchdog_init, POST_KERNEL, ECP_WATCHDOG_INIT_PRIORITY);
