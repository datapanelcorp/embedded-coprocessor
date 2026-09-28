#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/task_wdt/task_wdt.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/pm/pm.h>

#include <zephyr/app_version.h>

#include <dp/metrics.h>

LOG_MODULE_REGISTER(app, CONFIG_APP_LOG_LEVEL);

#define MAIN_LOOP_INTERVAL_MS 50
#define MAIN_WDT_TIMEOUT_MS   1000

int main(void)
{
	/* The task watchdog, and the hardware watchdog behind it, are started in watchdog.c.
	 * The main loop feeds its own channel.
	 */
#if defined(CONFIG_TASK_WDT)
	int wdt_channel = task_wdt_add(MAIN_WDT_TIMEOUT_MS, NULL, NULL);

	if (wdt_channel < 0) {
		LOG_ERR("Could not add task watchdog channel: %d", wdt_channel);
	}
#endif

	printk("ECP %d.%d.%d+%d-%s\n", APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_PATCHLEVEL,
	       APP_TWEAK, STRINGIFY(APP_BUILD_VERSION));

	while (1) {

		if (IS_ENABLED(CONFIG_DP_METRICS)) {
			dp_metrics_collect();
		}

#if defined(CONFIG_TASK_WDT)
		if (wdt_channel >= 0) {
			task_wdt_feed(wdt_channel);
		}
#endif
		k_msleep(MAIN_LOOP_INTERVAL_MS);
	}

	return 0;
}
