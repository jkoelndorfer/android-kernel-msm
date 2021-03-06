/* arch/arm/mach-msm/board-trout.c
 *
 * Copyright (C) 2008 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/leds.h>
#include <linux/switch.h>
#include <linux/android_timed_gpio.h>
#include <linux/synaptics_i2c_rmi.h>
#include <linux/akm8976.h>
#include <linux/sysdev.h>
#include <linux/usb/mass_storage_function.h>

#include <linux/delay.h>

#include <asm/gpio.h>
#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/flash.h>
#include <asm/system.h>
#include <asm/arch/system.h>
#include <asm/arch/vreg.h>

#include <asm/io.h>
#include <asm/delay.h>
#include <asm/setup.h>

#include <linux/gpio_event.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>

#include <asm/mach/mmc.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/msm_audio.h>

#include "board-trout.h"

#include "gpio_chip.h"

#include <asm/arch/board.h>
#include <asm/arch/clock.h>
#include <asm/arch/msm_fb.h>
#include <asm/arch/msm_hsusb.h>
#include <asm/arch/rpc_pm.h>

#include <asm/arch/trout_pwrsink.h>

#include "proc_comm.h"

void msm_init_irq(void);
void msm_init_gpio(void);

struct trout_axis_info {
	struct gpio_event_axis_info info;
	uint16_t in_state;
	uint16_t out_state;
};
static bool nav_just_on;
static int nav_on_jiffies;

uint16_t trout_axis_map(struct gpio_event_axis_info *info, uint16_t in)
{
	struct trout_axis_info *ai = container_of(info, struct trout_axis_info, info);
	uint16_t out = ai->out_state;
	
	if (nav_just_on) {
		if (jiffies == nav_on_jiffies || jiffies == nav_on_jiffies + 1)
			goto ignore;
		nav_just_on = 0;
	}
	if((ai->in_state ^ in) & 1)
		out--;
	if((ai->in_state ^ in) & 2)
		out++;
	ai->out_state = out;
ignore:
	ai->in_state = in;
	return out;
}

int trout_nav_power(const struct gpio_event_platform_data *pdata, bool on)
{
	gpio_set_value(TROUT_GPIO_JOG_EN, on);
	if (on) {
		nav_just_on = 1;
		nav_on_jiffies = jiffies;
	}
	return 0;
}

static uint32_t trout_4_x_axis_gpios[] = {
	TROUT_4_BALL_LEFT_0, TROUT_4_BALL_RIGHT_0
};
static uint32_t trout_5_x_axis_gpios[] = {
	TROUT_5_BALL_LEFT_0, TROUT_5_BALL_RIGHT_0
};

static struct trout_axis_info trout_x_axis = {
	.info = {
		.info.func = gpio_event_axis_func,
		.count = ARRAY_SIZE(trout_5_x_axis_gpios),
		.type = EV_REL,
		.code = REL_X,
		.decoded_size = 1U << ARRAY_SIZE(trout_5_x_axis_gpios),
		.map = trout_axis_map,
		.gpio = trout_5_x_axis_gpios,
		.flags = GPIOEAF_PRINT_UNKNOWN_DIRECTION /*| GPIOEAF_PRINT_RAW | GPIOEAF_PRINT_EVENT */
	}
};

static uint32_t trout_4_y_axis_gpios[] = {
	TROUT_4_BALL_UP_0, TROUT_4_BALL_DOWN_0
};
static uint32_t trout_5_y_axis_gpios[] = {
	TROUT_5_BALL_UP_0, TROUT_5_BALL_DOWN_0
};

static struct trout_axis_info trout_y_axis = {
	.info = {
		.info.func = gpio_event_axis_func,
		.count = ARRAY_SIZE(trout_5_y_axis_gpios),
		.type = EV_REL,
		.code = REL_Y,
		.decoded_size = 1U << ARRAY_SIZE(trout_5_y_axis_gpios),
		.map = trout_axis_map,
		.gpio = trout_5_y_axis_gpios,
		.flags = GPIOEAF_PRINT_UNKNOWN_DIRECTION /*| GPIOEAF_PRINT_RAW | GPIOEAF_PRINT_EVENT */
	}
};

static struct gpio_event_direct_entry trout_nav_buttons[] = {
	{ TROUT_GPIO_NAVI_ACT_N, BTN_MOUSE }
};

static struct gpio_event_input_info trout_nav_button_info = {
	.info.func = gpio_event_input_func,
	.flags = 0,
	.type = EV_KEY,
	.keymap = trout_nav_buttons,
	.keymap_size = ARRAY_SIZE(trout_nav_buttons)
};

static struct gpio_event_info *trout_nav_info[] = {
	&trout_x_axis.info.info,
	&trout_y_axis.info.info,
	&trout_nav_button_info.info
};

static struct gpio_event_platform_data trout_nav_data = {
	.name = "trout-nav",
	.info = trout_nav_info,
	.info_count = ARRAY_SIZE(trout_nav_info),
	.power = trout_nav_power,
};

static struct platform_device trout_nav_device = {
	.name = GPIO_EVENT_DEV_NAME,
	.id = 2,
	.dev		= {
		.platform_data	= &trout_nav_data,
	},
};

static int trout_ts_power(int on)
{
	int tp_ls_gpio = system_rev < 5 ? TROUT_4_TP_LS_EN : TROUT_5_TP_LS_EN;
	if (on) {
		trout_gpio_write(NULL, TROUT_GPIO_TP_I2C_PULL, 1);
		trout_gpio_write(NULL, TROUT_GPIO_TP_EN, 1);
		/* touchscreen must be powered before we enable i2c pullup */
		msleep(2);
		/* enable touch panel level shift */
		gpio_set_value(tp_ls_gpio, 1);
		msleep(2);
	}
	else {
		gpio_set_value(tp_ls_gpio, 0);
		trout_gpio_write(NULL, TROUT_GPIO_TP_EN, 0);
		trout_gpio_write(NULL, TROUT_GPIO_TP_I2C_PULL, 0);
	}
	return 0;
}

static struct synaptics_i2c_rmi_platform_data trout_ts_data[] = {
	{
		.version = 0x010c,
		.power = trout_ts_power,
		.flags = SYNAPTICS_FLIP_Y | SYNAPTICS_SNAP_TO_INACTIVE_EDGE,
		.inactive_left = -100 * 0x10000 / 4334,
		.inactive_right = -100 * 0x10000 / 4334,
		.inactive_top = -40 * 0x10000 / 6696,
		.inactive_bottom = -40 * 0x10000 / 6696,
		.snap_left_on = 300 * 0x10000 / 4334,
		.snap_left_off = 310 * 0x10000 / 4334,
		.snap_right_on = 300 * 0x10000 / 4334,
		.snap_right_off = 310 * 0x10000 / 4334,
		.snap_top_on = 100 * 0x10000 / 6696,
		.snap_top_off = 110 * 0x10000 / 6696,
		.snap_bottom_on = 100 * 0x10000 / 6696,
		.snap_bottom_off = 110 * 0x10000 / 6696,
	},
	{
		.flags = SYNAPTICS_FLIP_Y | SYNAPTICS_SNAP_TO_INACTIVE_EDGE,
		.inactive_left = ((4674 - 4334) / 2 + 200) * 0x10000 / 4334,
		.inactive_right = ((4674 - 4334) / 2 + 200) * 0x10000 / 4334,
		.inactive_top = ((6946 - 6696) / 2) * 0x10000 / 6696,
		.inactive_bottom = ((6946 - 6696) / 2) * 0x10000 / 6696,
	}
};

static struct akm8976_platform_data compass_platform_data = {
	.reset = TROUT_GPIO_COMPASS_RST_N,
	.clk_on = TROUT_GPIO_COMPASS_32K_EN,
	.intr = TROUT_GPIO_COMPASS_IRQ,
};

static struct i2c_board_info i2c_devices[] = {
	{
		I2C_BOARD_INFO(SYNAPTICS_I2C_RMI_NAME, 0x20),
		.platform_data = trout_ts_data,
		.irq = TROUT_GPIO_TO_INT(TROUT_GPIO_TP_ATT_N)
	},
	{
		I2C_BOARD_INFO("elan-touch", 0x10),
		.irq = TROUT_GPIO_TO_INT(TROUT_GPIO_TP_ATT_N),
	},
	{
		I2C_BOARD_INFO("akm8976", 0x1C),
		.platform_data = &compass_platform_data,
		.irq = TROUT_GPIO_TO_INT(TROUT_GPIO_COMPASS_IRQ),
	},
	{
		I2C_BOARD_INFO("pca963x", 0x62),
	},
	{
		I2C_BOARD_INFO("mt9t013", 0x6C >> 1),
		/* .irq = TROUT_GPIO_TO_INT(TROUT_GPIO_CAM_BTN_STEP1_N), */
	},
};

static struct android_pmem_platform_data android_pmem_pdata = {
	.name = "pmem",
	.start = MSM_PMEM_MDP_BASE,
	.size = MSM_PMEM_MDP_SIZE,
	.no_allocator = 1,
	.cached = 1,
};

static struct android_pmem_platform_data android_pmem_adsp_pdata = {
	.name = "pmem_adsp",
	.start = MSM_PMEM_ADSP_BASE,
	.size = MSM_PMEM_ADSP_SIZE,
	.no_allocator = 0,
	.cached = 0,
};

static struct android_pmem_platform_data android_pmem_camera_pdata = {
	.name = "pmem_camera",
	.start = MSM_PMEM_CAMERA_BASE,
	.size = MSM_PMEM_CAMERA_SIZE,
	.no_allocator = 0,
	.cached = 0,
};

static struct android_pmem_platform_data android_pmem_gpu0_pdata = {
	.name = "pmem_gpu0",
	.start = MSM_PMEM_GPU0_BASE,
	.size = MSM_PMEM_GPU0_SIZE,
	.no_allocator = 1,
	.cached = 0,
	.buffered = 1,
};

static struct android_pmem_platform_data android_pmem_gpu1_pdata = {
	.name = "pmem_gpu1",
	.start = MSM_PMEM_GPU1_BASE,
	.size = MSM_PMEM_GPU1_SIZE,
	.no_allocator = 1,
	.cached = 0,
	.buffered = 1,
};

static struct platform_device android_pmem_device = {
	.name = "android_pmem",
	.id = 0,
	.dev = { .platform_data = &android_pmem_pdata },
};

static struct platform_device android_pmem_adsp_device = {
	.name = "android_pmem",
	.id = 1,
	.dev = { .platform_data = &android_pmem_adsp_pdata },
};

static struct platform_device android_pmem_gpu0_device = {
	.name = "android_pmem",
	.id = 2,
	.dev = { .platform_data = &android_pmem_gpu0_pdata },
};

static struct platform_device android_pmem_gpu1_device = {
	.name = "android_pmem",
	.id = 3,
	.dev = { .platform_data = &android_pmem_gpu1_pdata },
};

static struct platform_device android_pmem_camera_device = {
	.name = "android_pmem",
	.id = 4,
	.dev = { .platform_data = &android_pmem_camera_pdata },
};

static struct timed_gpio timed_gpios[] = {
	{ 
		.name = "vibrator",
		.gpio = TROUT_GPIO_HAPTIC_PWM,
		.max_timeout = 15000,
	},
	{
		.name = "flash",
		.gpio = TROUT_GPIO_FLASH_EN,
		.max_timeout = 400,
	},
};

static struct timed_gpio_platform_data timed_gpio_data = {
	.num_gpios	= ARRAY_SIZE(timed_gpios),
	.gpios		= timed_gpios,
};

static struct platform_device android_timed_gpios = {
	.name		= "android-timed-gpio",
	.id		= -1,
	.dev		= {
		.platform_data	= &timed_gpio_data,
	},
};

static struct gpio_led android_led_list[] = {
	{ 
		.name = "spotlight",
		.gpio = TROUT_GPIO_SPOTLIGHT_EN,
	},
	{ 
		.name = "keyboard-backlight",
		.gpio = TROUT_GPIO_QTKEY_LED_EN,
	},
	{ 
		.name = "button-backlight",
		.gpio = TROUT_GPIO_UI_LED_EN,
	},
};

static struct gpio_led_platform_data android_leds_data = {
	.num_leds	= ARRAY_SIZE(android_led_list),
	.leds		= android_led_list,
};

static struct platform_device android_leds = {
	.name		= "leds-gpio",
	.id		= -1,
	.dev		= {
		.platform_data	= &android_leds_data,
	},
};

static struct gpio_switch_platform_data sd_door_switch_data = {
	.name		= "sd-door",
	.gpio		= TROUT_GPIO_SD_DOOR_N,
	.state_on	= "open",
	.state_off	= "closed",
};

static struct platform_device sd_door_switch = {
	.name		= "switch-gpio",
	.id		= -1,
	.dev		= {
		.platform_data	= &sd_door_switch_data,
	},
};

static struct resource msm_serial0_resources[] = {
	{
		.start	= INT_UART1,
		.end	= INT_UART1,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= MSM_UART1_PHYS,
		.end	= MSM_UART1_PHYS + MSM_UART1_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device msm_serial0_device = {
	.name = "msm_serial",
	.id = 0,
	.num_resources	= ARRAY_SIZE(msm_serial0_resources),
	.resource	= msm_serial0_resources,
};

static struct resource msm_serial2_resources[] = {
	{
		.start	= INT_UART3,
		.end	= INT_UART3,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= MSM_UART3_PHYS,
		.end	= MSM_UART3_PHYS + MSM_UART3_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device msm_serial2_device = {
	.name = "msm_serial",
	.id = 2,
	.num_resources	= ARRAY_SIZE(msm_serial2_resources),
	.resource	= msm_serial2_resources,
};

static struct resource usb_resources[] = {
	{
		.start	= MSM_HSUSB_PHYS,
		.end	= MSM_HSUSB_PHYS + MSM_HSUSB_SIZE,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= INT_USB_HS,
		.end	= INT_USB_HS,
		.flags	= IORESOURCE_IRQ,
	},
};

/* adjust eye diagram, disable vbusvalid interrupts */
static int trout_phy_init_seq[] = { 0x40, 0x31, 0x1D, 0x0D, 0x1D, 0x10, -1 };

static void trout_phy_reset(void)
{
	gpio_set_value(TROUT_GPIO_USB_PHY_RST_N, 0);
	mdelay(10);
	gpio_set_value(TROUT_GPIO_USB_PHY_RST_N, 1);
	mdelay(10);
}

static char *trout_usb_functions[] = {
	"usb_mass_storage",
	"adb",
};

static struct msm_hsusb_product trout_usb_products[] = {
	{
		.product_id     = 0x0c01,
		.functions      = 0x00000001, /* "usb_mass_storage" only */
	},
	{
		.product_id     = 0x0c02,
		.functions      = 0x00000003, /* "usb_mass_storage" and "adb" */
	},
};

static struct msm_hsusb_platform_data msm_hsusb_pdata = {
	.phy_reset	= trout_phy_reset,
	.phy_init_seq	= trout_phy_init_seq,
	.vendor_id	= 0x0bb4,
	.product_id	= 0x0c02,
	.version	= 0x0100,
	.product_name	= "Android Phone",
	.manufacturer_name = "HTC",

	.functions = trout_usb_functions,
	.num_functions = ARRAY_SIZE(trout_usb_functions),
	.products  = trout_usb_products,
	.num_products = ARRAY_SIZE(trout_usb_products),
};

static struct platform_device msm_hsusb_device = {
	.name		= "msm_hsusb",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(usb_resources),
	.resource	= usb_resources,
	.dev		= {
		.coherent_dma_mask	= 0xffffffff,
		.platform_data		= &msm_hsusb_pdata,
	},
};

static struct usb_mass_storage_platform_data mass_storage_pdata = {
	.nluns		= 1,
	.buf_size	= 16384,
	.vendor		= "HTC     ",
	.product	= "Android Phone   ",
	.release	= 0x0100,
};

static struct platform_device usb_mass_storage_device = {
	.name	= "usb_mass_storage",
	.id		= -1,
	.dev		= {
		.platform_data = &mass_storage_pdata,
	},
};

static struct resource trout_ram_console_resource[] = {
	{
		.start	= MSM_RAM_CONSOLE_BASE,
		.end	= MSM_RAM_CONSOLE_BASE + MSM_RAM_CONSOLE_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	}
};

static struct platform_device trout_ram_console_device = {
	.name = "ram_console",
	.id = -1,
	.num_resources  = ARRAY_SIZE(trout_ram_console_resource),
	.resource       = trout_ram_console_resource,
};

static struct msm_camera_device_platform_data msm_camera_device = {
	.sensor_reset	= 108,
	.sensor_pwd	= 85,
	.vcm_pwd	= TROUT_GPIO_VCM_PWDN,
};

static struct platform_device trout_camera = {
	.name		= "camera",
	.dev		= { 
		.platform_data = &msm_camera_device,
	},
};

static struct pwr_sink trout_pwrsink_table[] = {
	{
		.id	= PWRSINK_AUDIO,
		.ua_max	= 90000,
	},
	{
		.id	= PWRSINK_BACKLIGHT,
		.ua_max	= 128000,
	},
	{
		.id	= PWRSINK_LED_BUTTON,
		.ua_max	= 17000,
	},
	{
		.id	= PWRSINK_LED_KEYBOARD,
		.ua_max	= 22000,
	},
	{
		.id	= PWRSINK_GP_CLK,
		.ua_max	= 30000,
	},
	{
		.id	= PWRSINK_BLUETOOTH,
		.ua_max	= 15000,
	},	
	{
		.id	= PWRSINK_CAMERA,
		.ua_max	= 0,
	},
	{
		.id	= PWRSINK_SDCARD,
		.ua_max	= 0,
	},	
	{
		.id	= PWRSINK_VIDEO,
		.ua_max	= 0,
	},
	{
		.id	= PWRSINK_WIFI,
		.ua_max = 200000,
	},
	{
		.id	= PWRSINK_SYSTEM_LOAD,
		.ua_max	= 63000,
		.percent_util = 100,
	},
};

static struct pwr_sink_platform_data trout_pwrsink_data = {
	.num_sinks	= ARRAY_SIZE(trout_pwrsink_table),
	.sinks		= trout_pwrsink_table,
};

static struct platform_device trout_pwr_sink = {
	.name = "trout_pwrsink",
	.id = -1,
	.dev	= { 
		.platform_data = &trout_pwrsink_data,
	},
};

#define SND(desc,num) { .name = #desc, .id = num }
static struct snd_endpoint snd_endpoints_list[] = {
	SND(HANDSET, 0),
	SND(SPEAKER, 1),
	SND(HEADSET, 2),
	SND(BT, 3),
	SND(HEADSET_AND_SPEAKER, 10),
        SND(CURRENT, 256),

	/* Bluetooth accessories. */

	SND(BH_S100, 12),
	SND(BH_M100, 13),
	SND(Motorola_H500, 14),
	SND(Nokia_HS_36W, 15),
	SND(PLT_510vD, 16),
	SND(M2500_Plantronics, 17),
	SND(Nokia_HDW_3, 18),
	SND(HBH_608, 19),
	SND(HBH_DS970, 20),
	SND(iTech_BlueBAND, 21),
	SND(Nokia_BH_800, 22),
	SND(Motorola_H700, 23),
	SND(HTC_BH_M200, 24),
	SND(Jabra_JX10, 25),
	SND(Plantronics_320, 26),
	SND(Plantronics_640, 27),
	SND(Jabra_BT500, 28),
	SND(Motorola_HT820, 29),
	SND(HBH_IV840, 30),
	SND(Plantronics_6XX, 31),
	SND(Plantronics_3XX, 32),
	SND(HBH_PV710, 33),
	SND(Motorola_H670, 34),
	SND(HBM_300, 35),
	SND(Nokia_BH_208, 36),
	SND(Samsung_WEP410, 37),
	SND(Jabra_BT8010, 38),
	SND(Motorola_S9, 39),
	SND(Jabra_BT620s, 40),
	SND(Nokia_BH_902, 41),
	SND(HBH_DS220, 42),
	SND(HBH_DS980, 43),
	SND(BT_EC_OFF, 44),
};
#undef SND

static struct msm_snd_endpoints trout_snd_endpoints = {
        .endpoints = snd_endpoints_list,
        .num = ARRAY_SIZE(snd_endpoints_list),
};

static struct platform_device trout_snd = {
	.name = "msm_snd",
	.id = -1,
	.dev	= {
		.platform_data = &trout_snd_endpoints,
	},
};

static struct platform_device trout_rfkill = {
	.name = "trout_rfkill",
	.id = -1,
};

static struct platform_device *devices[] __initdata = {
	&msm_serial0_device,
#if !defined(CONFIG_MSM_SERIAL_DEBUGGER) && !defined(CONFIG_TROUT_H2W)
	&msm_serial2_device,
#endif
	&msm_hsusb_device,
	&usb_mass_storage_device,
	&trout_nav_device,
	&android_leds,
	&sd_door_switch,
	&android_timed_gpios,
	&android_pmem_device,
	&android_pmem_adsp_device,
	&android_pmem_gpu0_device,
	&android_pmem_gpu1_device,
	&android_pmem_camera_device,
	&trout_ram_console_device,
	&trout_camera,
	&trout_rfkill,
#if defined(CONFIG_TROUT_PWRSINK)
	&trout_pwr_sink,
#endif
	&trout_snd,
};

extern struct sys_timer msm_timer;

static void __init trout_init_irq(void)
{
	printk("trout_init_irq()\n");
	msm_init_irq();
}

static uint cpld_iset;
static uint cpld_charger_en;
static uint cpld_usb_h2w_sw;
static uint opt_disable_uart3;
static char *keycaps = "qwerty";

module_param_named(iset, cpld_iset, uint, 0);
module_param_named(charger_en, cpld_charger_en, uint, 0);
module_param_named(usb_h2w_sw, cpld_usb_h2w_sw, uint, 0);
module_param_named(disable_uart3, opt_disable_uart3, uint, 0);
module_param_named(keycaps, keycaps, charp, 0);

static int __init trout_serialno_setup(char *str)
{
	msm_hsusb_pdata.serial_number = str;
	return 1;
}

__setup("androidboot.serialno=", trout_serialno_setup);

static void trout_reset(char mode)
{
	gpio_set_value(TROUT_GPIO_PS_HOLD, 0);
}

static uint32_t gpio_table[] = {
	/* BLUETOOTH */
	PCOM_GPIO_CFG(43, 1, GPIO_OUTPUT, GPIO_PULL_UP, GPIO_4MA), /* RTS */
	PCOM_GPIO_CFG(44, 1, GPIO_OUTPUT, GPIO_PULL_UP, GPIO_4MA), /* CTS */
	PCOM_GPIO_CFG(45, 1, GPIO_OUTPUT, GPIO_PULL_UP, GPIO_4MA), /* RX */
	PCOM_GPIO_CFG(46, 1, GPIO_OUTPUT, GPIO_PULL_UP, GPIO_4MA), /* TX */
};


static uint32_t camera_off_gpio_table[] = {
	/* CAMERA */
	PCOM_GPIO_CFG(2, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_4MA), /* DAT2 */
	PCOM_GPIO_CFG(3, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_4MA), /* DAT3 */
	PCOM_GPIO_CFG(4, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_4MA), /* DAT4 */
	PCOM_GPIO_CFG(5, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_4MA), /* DAT5 */
	PCOM_GPIO_CFG(6, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_4MA), /* DAT6 */
	PCOM_GPIO_CFG(7, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_4MA), /* DAT7 */
	PCOM_GPIO_CFG(8, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_4MA), /* DAT8 */
	PCOM_GPIO_CFG(9, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_4MA), /* DAT9 */
	PCOM_GPIO_CFG(10, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_4MA), /* DAT10 */
	PCOM_GPIO_CFG(11, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_4MA), /* DAT11 */
	PCOM_GPIO_CFG(12, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_4MA), /* PCLK */
	PCOM_GPIO_CFG(13, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_4MA), /* HSYNC_IN */
	PCOM_GPIO_CFG(14, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_4MA), /* VSYNC_IN */
	PCOM_GPIO_CFG(15, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_4MA), /* MCLK */
};

static uint32_t camera_on_gpio_table[] = {
	/* CAMERA */
	PCOM_GPIO_CFG(2, 1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT2 */
	PCOM_GPIO_CFG(3, 1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT3 */
	PCOM_GPIO_CFG(4, 1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT4 */
	PCOM_GPIO_CFG(5, 1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT5 */
	PCOM_GPIO_CFG(6, 1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT6 */
	PCOM_GPIO_CFG(7, 1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT7 */
	PCOM_GPIO_CFG(8, 1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT8 */
	PCOM_GPIO_CFG(9, 1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT9 */
	PCOM_GPIO_CFG(10, 1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT10 */
	PCOM_GPIO_CFG(11, 1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT11 */
	PCOM_GPIO_CFG(12, 1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_16MA), /* PCLK */
	PCOM_GPIO_CFG(13, 1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* HSYNC_IN */
	PCOM_GPIO_CFG(14, 1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* VSYNC_IN */
	PCOM_GPIO_CFG(15, 1, GPIO_OUTPUT, GPIO_PULL_DOWN, GPIO_16MA), /* MCLK */
};

static void config_gpio_table(uint32_t *table, int len)
{
	int n;
	unsigned id;
	for(n = 0; n < len; n++) {
		id = table[n];
		msm_proc_comm(PCOM_RPC_GPIO_TLMM_CONFIG_EX, &id, 0);
	}
}

void config_camera_on_gpios(void)
{
	config_gpio_table(camera_on_gpio_table,
		ARRAY_SIZE(camera_on_gpio_table));
}

void config_camera_off_gpios(void)
{
	config_gpio_table(camera_off_gpio_table,
		ARRAY_SIZE(camera_off_gpio_table));
}

static void __init config_gpios(void)
{
	config_gpio_table(gpio_table, ARRAY_SIZE(gpio_table));
	config_camera_off_gpios();
}

void msm_serial_debug_init(unsigned int base, int irq, 
			   const char *clkname, int signal_irq);

static void __init trout_init(void)
{
	int rc;
	printk("trout_init() revision=%d\n", system_rev);

	/*
	 * Setup common MSM GPIOS
	 */
	msm_init_gpio();

	config_gpios();

	msm_reset_hook = trout_reset;

	trout_init_cpldshadow(system_rev);

	trout_sysdev_resume(NULL);

	/* adjust GPIOs based on bootloader request */
	printk("trout_init: cpld_usb_hw2_sw = %d\n", cpld_usb_h2w_sw);
	trout_gpio_write(NULL, TROUT_GPIO_USB_H2W_SW, cpld_usb_h2w_sw);

#if defined(CONFIG_MSM_SERIAL_DEBUGGER)
	if (!opt_disable_uart3)
		msm_serial_debug_init(MSM_UART3_PHYS, INT_UART3,
				      "uart3_clk", 1);
#endif

	rc = trout_init_gpio(system_rev);
	if (rc)
		printk(KERN_CRIT "%s: GPIO init failure (%d)\n", __func__, rc);

	/* gpio_configure(108, IRQF_TRIGGER_LOW); */

#if defined(CONFIG_LL_DEBUG_UART1)
	/* H2W pins <-> UART1 */
	gpio_set_value(TROUT_GPIO_H2W_SEL0, 1);
	gpio_set_value(TROUT_GPIO_H2W_SEL1, 0);
#else
	/* H2W pins <-> UART3, Bluetooth <-> UART1 */
	gpio_set_value(TROUT_GPIO_H2W_SEL0, 0);
	gpio_set_value(TROUT_GPIO_H2W_SEL1, 1);
#endif

	/* put the AF VCM in powerdown mode to avoid noise */
	trout_gpio_write(NULL, TROUT_GPIO_VCM_PWDN, 1);
	mdelay(100);

	msm_add_devices();

	rc = trout_init_keypad(system_rev, keycaps);
	if (rc)
		printk(KERN_CRIT "%s: Keypad init failure (%d)\n", __func__, rc);

	rc = trout_init_panel(system_rev);
	if (rc)
		printk(KERN_CRIT "%s: Panel init failure (%d)\n", __func__, rc);

	rc = trout_init_mmc(system_rev);
	if (rc)
		printk(KERN_CRIT "%s: MMC init failure (%d)\n", __func__, rc);

	if (system_rev < 5) {
		trout_x_axis.info.gpio = trout_4_x_axis_gpios;
		trout_y_axis.info.gpio = trout_4_y_axis_gpios;
	}

	platform_add_devices(devices, ARRAY_SIZE(devices));
	i2c_register_board_info(0, i2c_devices, ARRAY_SIZE(i2c_devices));

	/* SD card door should wake the device */
	trout_gpio_irq_set_wake(TROUT_GPIO_TO_INT(TROUT_GPIO_SD_DOOR_N), 1);
}

static struct map_desc trout_io_desc[] __initdata = {
	{
		.virtual = TROUT_CPLD_BASE,
		.pfn     = __phys_to_pfn(TROUT_CPLD_START),
		.length  = TROUT_CPLD_SIZE,
		.type    = MT_DEVICE_NONSHARED
	}
};

static struct msm_clock_platform_data trout_clock_data = {
	.acpu_switch_time_us = 20,
	.max_speed_delta_khz = 256000,
	.vdd_switch_time_us = 62,
	.power_collapse_khz = 19200000,
	.wait_for_irq_khz = 128000000,
};

static void __init trout_fixup(struct machine_desc *desc, struct tag *tags,
                               char **cmdline, struct meminfo *mi)
{
	mi->nr_banks=1;
	mi->bank[0].start = PHYS_OFFSET;
	mi->bank[0].node = PHYS_TO_NID(PHYS_OFFSET);
	mi->bank[0].size = (101*1024*1024);
}

static void __init trout_map_io(void)
{
	printk("trout_init_map_io()\n");
	msm_map_common_io();
	iotable_init(trout_io_desc, ARRAY_SIZE(trout_io_desc));
	msm_clock_init(&trout_clock_data);
}

MACHINE_START(TROUT, "trout")
/* Maintainer: Brian Swetland <swetland@google.com> */

/* this is broken... can we just opt out of specifying something here? */
	.phys_io        = 0x80000000,
	.io_pg_offst    = ((0x80000000) >> 18) & 0xfffc,

	.boot_params    = 0x10000100,
	.fixup          = trout_fixup,
	.map_io         = trout_map_io,
	.init_irq       = trout_init_irq,
	.init_machine   = trout_init,
	.timer          = &msm_timer,
MACHINE_END
