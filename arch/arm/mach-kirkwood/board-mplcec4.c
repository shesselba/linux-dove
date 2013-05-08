/*
 * Copyright (C) 2012 MPL AG, Switzerland
 * Stefan Peter <s.peter@mpl.ch>
 *
 * arch/arm/mach-kirkwood/board-mplcec4.c
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include "common.h"

void __init mplcec4_init(void)
{
	/*
	 * Basic setup. Needs to be called early.
	 */
	kirkwood_pcie_init(KW_PCIE0);
}



