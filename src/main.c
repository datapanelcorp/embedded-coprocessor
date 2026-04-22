#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/pm/pm.h>

#include <zephyr/app_version.h>

#include <stm32_ll_tim.h>

LOG_MODULE_REGISTER(app, CONFIG_APP_LOG_LEVEL);

extern int handle_indicators();

#if defined(CONFIG_WATCHDOG) && DT_NODE_HAS_STATUS(DT_ALIAS(watchdog0), okay)
#define WDT_MAX_WINDOW       CONFIG_IWDG_STM32_INITIAL_TIMEOUT
#define WDT_MIN_WINDOW       0
#define WDT_FEED_INTERVAL_MS 50

static const struct device *wdt = DEVICE_DT_GET(DT_ALIAS(watchdog0));
#endif

int main(void)
{
#if defined(CONFIG_WATCHDOG) && DT_NODE_HAS_STATUS(DT_ALIAS(watchdog0), okay)
	if (!device_is_ready(wdt)) {
		LOG_ERR("%s: device not ready", wdt->name);
		return -ENODEV;
	}

	struct wdt_timeout_cfg wdt_config = {
		.flags = WDT_FLAG_RESET_SOC,
		.window.min = WDT_MIN_WINDOW,
		.window.max = WDT_MAX_WINDOW,
	};

	int wdt_channel_id = wdt_install_timeout(wdt, &wdt_config);
	if (wdt_channel_id < 0) {
		LOG_ERR("Could not install watchdog timeout: %d", wdt_channel_id);
		return wdt_channel_id;
	}

	int ret = wdt_setup(wdt, WDT_OPT_PAUSE_HALTED_BY_DBG);
	if (ret < 0) {
		LOG_ERR("Watchdog setup failed: %d", ret);
		return ret;
	}

#if WDT_MIN_WINDOW != 0
	k_msleep(WDT_MIN_WINDOW);
#endif

	wdt_feed(wdt, wdt_channel_id);
#endif

	printk("ECP %d.%d.%d+%d\n", APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_PATCHLEVEL,
	       APP_TWEAK);

	// Timers TIM1 and TIM3 are not directly connected to any I/O. Instead, they
	// connect to the comparators internally. The upstream drivers do not have a
	// way to configure this via pinctrl or the device tree, so we manually re-map
	// them here.
	// LPTIM1, used for the encoder, has a device tree binding that does this.
	LL_TIM_SetRemap(TIM1, LL_TIM_TIM1_TI2_RMP_COMP2);
	LL_TIM_SetRemap(TIM3, LL_TIM_TIM3_TI1_RMP_COMP1);

	while (1) {

#if defined(CONFIG_WATCHDOG) && DT_NODE_HAS_STATUS(DT_ALIAS(watchdog0), okay)
		wdt_feed(wdt, wdt_channel_id);
		k_msleep(WDT_FEED_INTERVAL_MS);
#else
		k_msleep(100);
#endif
	}

	return 0;
}
