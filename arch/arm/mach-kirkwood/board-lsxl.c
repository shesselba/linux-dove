/*
 * Copyright 2012 (C), Michael Walle <michael@walle.cc>
 *
 * arch/arm/mach-kirkwood/board-lsxl.c
 *
 * Buffalo Linkstation LS-XHL and LS-CHLv2 init for drivers not
 * converted to flattened device tree yet.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include "common.h"

/*
 * On the LS-XHL/LS-CHLv2, the shutdown process is following:
 * - Userland monitors key events until the power switch goes to off position
 * - The board reboots
 * - U-boot starts and goes into an idle mode waiting for the user
 *   to move the switch to ON position
 *
 */
static void lsxl_power_off(void)
{
	kirkwood_restart('h', NULL);
}

void __init lsxl_init(void)
{
	/*
	 * Basic setup. Needs to be called early.
	 */

	/* register power-off method */
	pm_power_off = lsxl_power_off;
}
