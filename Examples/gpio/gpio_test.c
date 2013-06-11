/*
 * Copyright (C) 2009 Texas Instruments Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option)any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/module.h>
#include <linux/version.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/completion.h>
#include <mach/cputype.h>
#include <mach/hardware.h>
#include <mach/mux.h>
#include <asm/gpio.h>

static int gpio_num;
static int gpio_pin;

DECLARE_COMPLETION(work);

static irqreturn_t handler (int irq, void * dev)
{
	complete_all(&work);
	return IRQ_HANDLED;
}

int init_module()
{
	int status;

	init_completion(&work);

	if(cpu_is_davinci_da830()) {
		gpio_num = 87;	/* gpio_num = (bank_num * 16) + pin_num */
		gpio_pin = DA830_GPIO5_7;
	} else {
		gpio_num = 116;
		gpio_pin = DA850_GPIO7_4;
	}

	if (cpu_is_davinci_da830())
		printk("\nTesting gpio %d (connected to boot pin S2-7)\n",
			gpio_num);
	else
		printk("\nTesting gpio %d (connected to boot pin S7-8)\n",
			gpio_num);

	/* init/set pinmux */
	status = davinci_cfg_reg(gpio_pin);
	if (status < 0) {
		printk("pin could not be muxed for GPIO functionality %d\n",
								gpio_num);
		return status;
	}

	status = gpio_request(gpio_num, "gpio_test\n");
	if (status < 0) {
		printk("ERROR can not open GPIO %d\n", gpio_num);
		return status;
	}

	gpio_direction_input(gpio_num);

	local_irq_disable();
	pr_emerg("Before gpio_get\n");
	gpio_get_value(gpio_num);
	pr_emerg("After gpio_get\n");
	local_irq_enable();

	if (cpu_is_davinci_da830())
		printk("The current state of S2-7 pin is ");
	else
		printk("The current state of S7-8 pin is ");
	if(gpio_get_value(gpio_num) == 0)
		printk("OFF. \n\tWaiting for the pin to be on..\n");
	else
		printk("ON. \n\tWaiting for the pin to be off..\n");

	status = request_irq(gpio_to_irq(gpio_num), handler, 0,
				"gpio_test",NULL);
	if(status < 0) {
		printk(KERN_ERR "error %d requesting GPIO IRQ %d\n",
				status, gpio_num);
		return status;
	}

	irq_set_irq_type(gpio_to_irq(gpio_num), IRQ_TYPE_EDGE_RISING);

	wait_for_completion_interruptible(&work);

	printk(".. done\n");

	return 0;
}

void cleanup_module(void) {

	gpio_free(gpio_num);

	free_irq(gpio_to_irq(gpio_num), NULL);
}

MODULE_LICENSE("GPL");
