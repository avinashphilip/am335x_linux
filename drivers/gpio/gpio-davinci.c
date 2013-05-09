/*
 * TI DaVinci GPIO Support
 *
 * Copyright (c) 2006-2007 David Brownell
 * Copyright (c) 2007, MontaVista Software, Inc. <source@mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/clk.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/kernel.h>

#include <asm/mach/irq.h>

struct davinci_gpio_regs {
	u32	dir;
	u32	out_data;
	u32	set_data;
	u32	clr_data;
	u32	in_data;
	u32	set_rising;
	u32	clr_rising;
	u32	set_falling;
	u32	clr_falling;
	u32	intstat;
};

#define BINTEN	0x08 /* GPIO Interrupt Per-Bank Enable Register */

#define chip2controller(chip)	\
	container_of(chip, struct davinci_gpio_controller, chip)

static struct davinci_gpio_controller ctlrs[DIV_ROUND_UP(DAVINCI_N_GPIO, 32)];
static void __iomem *gpio_base;

static struct davinci_gpio_regs __iomem __init *gpio2regs(unsigned gpio)
{
	void __iomem *ptr;

	if (gpio < 32 * 1)
		ptr = gpio_base + 0x10;
	else if (gpio < 32 * 2)
		ptr = gpio_base + 0x38;
	else if (gpio < 32 * 3)
		ptr = gpio_base + 0x60;
	else if (gpio < 32 * 4)
		ptr = gpio_base + 0x88;
	else if (gpio < 32 * 5)
		ptr = gpio_base + 0xb0;
	else
		ptr = NULL;

	return ptr;
}

static inline struct davinci_gpio_regs __iomem *irq2regs(int irq)
{
	return (__force struct davinci_gpio_regs __iomem *)
		irq_get_chip_data(irq);
}

static int __init davinci_gpio_irq_setup(void);

static inline int __davinci_direction(struct gpio_chip *chip,
			unsigned offset, bool out, int value)
{
	struct davinci_gpio_controller *ctlr = chip2controller(chip);
	struct davinci_gpio_regs __iomem *regs = ctlr->regs;
	unsigned long flags;
	u32 temp;
	u32 mask = 1 << offset;

	spin_lock_irqsave(&ctlr->lock, flags);
	temp = __raw_readl(&regs->dir);
	if (out) {
		temp &= ~mask;
		__raw_writel(mask, value ? &regs->set_data : &regs->clr_data);
	} else {
		temp |= mask;
	}
	__raw_writel(temp, &regs->dir);
	spin_unlock_irqrestore(&ctlr->lock, flags);

	return 0;
}

static int davinci_direction_in(struct gpio_chip *chip, unsigned offset)
{
	return __davinci_direction(chip, offset, false, 0);
}

static int
davinci_direction_out(struct gpio_chip *chip, unsigned offset, int value)
{
	return __davinci_direction(chip, offset, true, value);
}

/*
 * Read the pin's value (works even if it's set up as output);
 * returns zero/nonzero.
 *
 * Note that changes are synched to the GPIO clock, so reading values back
 * right after you've set them may give old values.
 */
static int davinci_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct davinci_gpio_controller *ctlr = chip2controller(chip);
	struct davinci_gpio_regs __iomem *regs = ctlr->regs;

	return (1 << offset) & __raw_readl(&regs->in_data);
}

/*
 * Assuming the pin is muxed as a gpio output, set its output value.
 */
static void
davinci_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct davinci_gpio_controller *ctlr = chip2controller(chip);
	struct davinci_gpio_regs __iomem *regs = ctlr->regs;

	__raw_writel((1 << offset), value ? &regs->set_data : &regs->clr_data);
}

static int __init davinci_gpio_setup(void)
{
	int i, base;
	unsigned ngpio;
	struct davinci_soc_info *soc_info = &davinci_soc_info;
	struct davinci_gpio_regs *regs;

	if (soc_info->gpio_type != GPIO_TYPE_DAVINCI)
		return 0;

	/*
	 * The gpio banks conceptually expose a segmented bitmap,
	 * and "ngpio" is one more than the largest zero-based
	 * bit index that's valid.
	 */
	ngpio = soc_info->gpio_num;
	if (ngpio == 0) {
		pr_err("GPIO setup:  how many GPIOs?\n");
		return -EINVAL;
	}

	if (WARN_ON(DAVINCI_N_GPIO < ngpio))
		ngpio = DAVINCI_N_GPIO;

	gpio_base = ioremap(soc_info->gpio_base, SZ_4K);
	if (WARN_ON(!gpio_base))
		return -ENOMEM;

	for (i = 0, base = 0; base < ngpio; i++, base += 32) {
		ctlrs[i].chip.label = "DaVinci";

		ctlrs[i].chip.direction_input = davinci_direction_in;
		ctlrs[i].chip.direction_output = davinci_direction_out;
		ctlrs[i].chip.get = davinci_gpio_get;
		ctlrs[i].chip.set = davinci_gpio_set;

		ctlrs[i].chip.base = base;
		ctlrs[i].chip.ngpio = ngpio - base;
		if (ctlrs[i].chip.ngpio > 32)
			ctlrs[i].chip.ngpio = 32;

		spin_lock_init(&ctlrs[i].lock);

		regs = gpio2regs(base);
		ctlrs[i].regs = regs;
		ctlrs[i].set_data = &regs->set_data;
		ctlrs[i].clr_data = &regs->clr_data;
		ctlrs[i].in_data = &regs->in_data;

		gpiochip_add(&ctlrs[i].chip);
	}

	soc_info->gpio_ctlrs = ctlrs;
	soc_info->gpio_ctlrs_num = DIV_ROUND_UP(ngpio, 32);

	davinci_gpio_irq_setup();
	return 0;
}
pure_initcall(davinci_gpio_setup);

/*
 * We expect irqs will normally be set up as input pins, but they can also be
 * used as output pins ... which is convenient for testing.
 *
 * NOTE:  The first few GPIOs also have direct INTC hookups in addition
 * to their GPIOBNK0 irq, with a bit less overhead.
 *
 * All those INTC hookups (direct, plus several IRQ banks) can also
 * serve as EDMA event triggers.
 */

static void gpio_irq_disable(struct irq_data *d)
{
	struct davinci_gpio_regs __iomem *regs = irq2regs(d->irq);
	u32 mask = (u32) irq_data_get_irq_handler_data(d);

	__raw_writel(mask, &regs->clr_falling);
	__raw_writel(mask, &regs->clr_rising);
}

static void gpio_irq_enable(struct irq_data *d)
{
	struct davinci_gpio_regs __iomem *regs = irq2regs(d->irq);
	u32 mask = (u32) irq_data_get_irq_handler_data(d);
	unsigned status = irqd_get_trigger_type(d);

	status &= IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_EDGE_RISING;
	if (!status)
		status = IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_EDGE_RISING;

	if (status & IRQ_TYPE_EDGE_FALLING)
		__raw_writel(mask, &regs->set_falling);
	if (status & IRQ_TYPE_EDGE_RISING)
		__raw_writel(mask, &regs->set_rising);
}

static int gpio_irq_type(struct irq_data *d, unsigned trigger)
{
	if (trigger & ~(IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_EDGE_RISING))
		return -EINVAL;

	return 0;
}

static struct irq_chip gpio_irqchip = {
	.name		= "GPIO",
	.irq_enable	= gpio_irq_enable,
	.irq_disable	= gpio_irq_disable,
	.irq_set_type	= gpio_irq_type,
	.flags		= IRQCHIP_SET_TYPE_MASKED,
};

static void gpio_irq_handler(unsigned irq, struct irq_desc *desc)
{
	u32 mask = 0xffff;
	struct davinci_gpio_controller *ctlr;
	struct davinci_gpio_regs __iomem *regs;

	ctlr = (struct davinci_gpio_controller *)
		irq_desc_get_handler_data(desc);
	regs = ctlr->regs;

	/* we only care about one bank */
	if (irq & 1)
		mask <<= 16;

	/* temporarily mask (level sensitive) parent IRQ */
	desc->irq_data.chip->irq_mask(&desc->irq_data);
	desc->irq_data.chip->irq_ack(&desc->irq_data);
	while (1) {
		u32		status;
		int		n;
		int		res;

		/* ack any irqs */
		status = __raw_readl(&regs->intstat) & mask;
		if (!status)
			break;
		__raw_writel(status, &regs->intstat);

		/* now demux them to the right lowlevel handler */
		n = ctlr->irq_base;
		if (irq & 1) {
			n += 16;
			status >>= 16;
		}

		while (status) {
			res = ffs(status);
			n += res;
			generic_handle_irq(n - 1);
			status >>= res;
		}
	}
	desc->irq_data.chip->irq_unmask(&desc->irq_data);
	/* now it may re-trigger */
}

static int gpio_to_irq_banked(struct gpio_chip *chip, unsigned offset)
{
	struct davinci_gpio_controller *ctlr = chip2controller(chip);

	if (ctlr->irq_base >= 0)
		return ctlr->irq_base + offset;
	else
		return -ENODEV;
}

static int gpio_to_irq_unbanked(struct gpio_chip *chip, unsigned offset)
{
	struct davinci_soc_info *soc_info = &davinci_soc_info;

	/*
	 * NOTE:  we assume for now that only irqs in the first gpio_chip
	 * can provide direct-mapped IRQs to AINTC (up to 32 GPIOs).
	 */
	if (offset < soc_info->gpio_unbanked)
		return soc_info->gpio_irq + offset;
	else
		return -ENODEV;
}

static int gpio_irq_type_unbanked(struct irq_data *data, unsigned trigger)
{
	struct davinci_gpio_controller *ctlr;
	struct davinci_gpio_regs __iomem *regs;
	struct davinci_soc_info *soc_info = &davinci_soc_info;
	u32 mask;

	ctlr = (struct davinci_gpio_controller *)data->handler_data;
	regs = (struct davinci_gpio_regs __iomem *)ctlr->regs;
	mask = __gpio_mask(data->irq - soc_info->gpio_irq);

	if (trigger & ~(IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_EDGE_RISING))
		return -EINVAL;

	__raw_writel(mask, (trigger & IRQ_TYPE_EDGE_FALLING)
		     ? &regs->set_falling : &regs->clr_falling);
	__raw_writel(mask, (trigger & IRQ_TYPE_EDGE_RISING)
		     ? &regs->set_rising : &regs->clr_rising);

	return 0;
}

/*
 * NOTE:  for suspend/resume, probably best to make a platform_device with
 * suspend_late/resume_resume calls hooking into results of the set_wake()
 * calls ... so if no gpios are wakeup events the clock can be disabled,
 * with outputs left at previously set levels, and so that VDD3P3V.IOPWDN0
 * (dm6446) can be set appropriately for GPIOV33 pins.
 */

static int __init davinci_gpio_irq_setup(void)
{
	u32		binten = 0;
	unsigned	gpio, irq, bank, ngpio, bank_irq;
	struct clk	*clk;
	struct davinci_gpio_regs __iomem *regs;
	struct davinci_soc_info *soc_info = &davinci_soc_info;

	ngpio = soc_info->gpio_num;

	bank_irq = soc_info->gpio_irq;
	if (bank_irq == 0) {
		printk(KERN_ERR "Don't know first GPIO bank IRQ.\n");
		return -EINVAL;
	}

	clk = clk_get(NULL, "gpio");
	if (IS_ERR(clk)) {
		printk(KERN_ERR "Error %ld getting gpio clock?\n",
		       PTR_ERR(clk));
		return PTR_ERR(clk);
	}
	clk_prepare_enable(clk);

	/*
	 * Arrange gpio_to_irq() support, handling either direct IRQs or
	 * banked IRQs.  Having GPIOs in the first GPIO bank use direct
	 * IRQs, while the others use banked IRQs, would need some setup
	 * tweaks to recognize hardware which can do that.
	 */
	for (gpio = 0, bank = 0; gpio < ngpio; bank++, gpio += 32) {
		ctlrs[bank].chip.to_irq = gpio_to_irq_banked;
		ctlrs[bank].irq_base = soc_info->gpio_unbanked ?
			-EINVAL : soc_info->intc_irq_num + gpio;
	}

	/*
	 * AINTC can handle direct/unbanked IRQs for GPIOs, with the GPIO
	 * controller only handling trigger modes.  We currently assume no
	 * IRQ mux conflicts; gpio_irq_type_unbanked() is only for GPIOs.
	 */
	if (soc_info->gpio_unbanked) {
		static struct irq_chip_type gpio_unbanked;

		/* pass "bank 0" GPIO IRQs to AINTC */
		ctlrs[0].chip.to_irq = gpio_to_irq_unbanked;
		binten = BIT(0);

		/* AINTC handles mask/unmask; GPIO handles triggering */
		irq = bank_irq;
		gpio_unbanked = *container_of(irq_get_chip(irq),
					      struct irq_chip_type, chip);
		gpio_unbanked.chip.name = "GPIO-AINTC";
		gpio_unbanked.chip.irq_set_type = gpio_irq_type_unbanked;

		/* default trigger: both edges */
		regs = gpio2regs(0);
		__raw_writel(~0, &regs->set_falling);
		__raw_writel(~0, &regs->set_rising);

		/* set the direct IRQs up to use that irqchip */
		for (gpio = 0; gpio < soc_info->gpio_unbanked; gpio++, irq++) {
			irq_set_chip(irq, &gpio_unbanked.chip);
			irq_set_handler_data(irq, &ctlrs[gpio / 32]);
			irq_set_status_flags(irq, IRQ_TYPE_EDGE_BOTH);
		}

		goto done;
	}

	/*
	 * Or, AINTC can handle IRQs for banks of 16 GPIO IRQs, which we
	 * then chain through our own handler.
	 */
	for (gpio = 0, irq = gpio_to_irq(0), bank = 0;
			gpio < ngpio;
			bank++, bank_irq++) {
		unsigned		i;

		/* disabled by default, enabled only as needed */
		regs = gpio2regs(gpio);
		__raw_writel(~0, &regs->clr_falling);
		__raw_writel(~0, &regs->clr_rising);

		/* set up all irqs in this bank */
		irq_set_chained_handler(bank_irq, gpio_irq_handler);

		/*
		 * Each chip handles 32 gpios, and each irq bank consists of 16
		 * gpio irqs. Pass the irq bank's corresponding controller to
		 * the chained irq handler.
		 */
		irq_set_handler_data(bank_irq, &ctlrs[gpio / 32]);

		for (i = 0; i < 16 && gpio < ngpio; i++, irq++, gpio++) {
			irq_set_chip(irq, &gpio_irqchip);
			irq_set_chip_data(irq, (__force void *)regs);
			irq_set_handler_data(irq, (void *)__gpio_mask(gpio));
			irq_set_handler(irq, handle_simple_irq);
			set_irq_flags(irq, IRQF_VALID);
		}

		binten |= BIT(bank);
	}

done:
	/*
	 * BINTEN -- per-bank interrupt enable. genirq would also let these
	 * bits be set/cleared dynamically.
	 */
	__raw_writel(binten, gpio_base + BINTEN);

	printk(KERN_INFO "DaVinci: %d gpio irqs\n", irq - gpio_to_irq(0));

	return 0;
}
