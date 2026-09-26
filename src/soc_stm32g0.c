#include <zephyr/init.h>

#include <stm32_ll_dac.h>
#include <stm32_ll_tim.h>
#include <stm32_ll_system.h>

/* Peripheral routing that the upstream drivers and device tree can't express. Runs after
 * all drivers are initialized, and before the host can enumerate the ports.
 */
static int soc_stm32g0_routing_init(void)
{
	// Timers TIM1 and TIM3 are not directly connected to any I/O. Instead, they
	// connect to the comparators internally. The upstream drivers do not have a
	// way to configure this via pinctrl or the device tree, so we manually re-map
	// them here.
	// LPTIM1, used for the encoder, has a device tree binding that does this.
	LL_TIM_SetRemap(TIM1, LL_TIM_TIM1_TI2_RMP_COMP2);
	LL_TIM_SetRemap(TIM3, LL_TIM_TIM3_TI1_RMP_COMP1);

	// The STM32G0xx series has a mux that can connect the output of the DAC to a GPIO pad,
	// internal peripherals, and/or the output buffer. By default, it's connected to the
	// pad.
	// The Zephyr DAC driver doesn't set this, so we set it here.
	LL_DAC_SetOutputConnection(DAC1, LL_DAC_CHANNEL_2, LL_DAC_OUTPUT_CONNECT_INTERNAL);

	return 0;
}
SYS_INIT(soc_stm32g0_routing_init, APPLICATION, 99);
