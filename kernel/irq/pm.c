/*
 * linux/kernel/irq/pm.c
 *
 * Copyright (C) 2009 Rafael J. Wysocki <rjw@sisk.pl>, Novell Inc.
 *
 * This file contains power management functions related to interrupts.
 */

#include <linux/irq.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/suspend.h>
#include <linux/syscore_ops.h>
#include <linux/wakeup_reason.h>

#include "internals.h"

void irq_pm_restore_handler(struct irqaction *action)
{
	if (action->s_handler) {
		action->handler = action->s_handler;
		action->s_handler = NULL;
		action->dev_id = action->s_dev_id;
		action->s_dev_id = NULL;
	}
}

static void irq_pm_substitute_handler(struct irqaction *action,
				      irq_handler_t new_handler)
{
	if (!action->s_handler) {
		action->s_handler = action->handler;
		action->handler = new_handler;
		action->s_dev_id = action->dev_id;
		action->dev_id = action;
	}
}

static irqreturn_t irq_wakeup_mode_handler(int irq, void *dev_id)
{
	struct irqaction *action = dev_id;
	struct irq_desc *desc;

	if (action->next)
		return IRQ_NONE;

	desc = irq_to_desc(irq);
	desc->istate |= IRQS_SUSPENDED | IRQS_PENDING;
	desc->depth++;
	irq_disable(desc);
	pm_system_wakeup();
	return IRQ_HANDLED;
}

static void irq_pm_wakeup_mode(struct irq_desc *desc)
{
	struct irqaction *action;

	for (action = desc->action; action; action = action->next)
		irq_pm_substitute_handler(action, irq_wakeup_mode_handler);
}

static void irq_pm_normal_mode(struct irq_desc *desc)
{
	struct irqaction *action;

	for (action = desc->action; action; action = action->next)
		irq_pm_restore_handler(action);
}

void wakeup_mode_for_irqs(bool enable)
{
	struct irq_desc *desc;
	int irq;

	for_each_irq_desc(irq, desc) {
		struct irqaction *action = desc->action;
		unsigned long flags;

		raw_spin_lock_irqsave(&desc->lock, flags);

		if (action && irqd_is_wakeup_set(&desc->irq_data)) {
			if (enable) {
				if (desc->istate & IRQS_SUSPENDED) {
					irq_pm_wakeup_mode(desc);
					desc->istate &= ~IRQS_SUSPENDED;
					__enable_irq(desc, irq, false);
				}
			} else {
				if (!(desc->istate & IRQS_SUSPENDED)) {
					__disable_irq(desc, irq, false);
					desc->istate |= IRQS_SUSPENDED;
				}
				irq_pm_normal_mode(desc);
			}
		}

		raw_spin_unlock_irqrestore(&desc->lock, flags);
	}
}

/**
 * suspend_device_irqs - disable all currently enabled interrupt lines
 *
 * During system-wide suspend or hibernation device drivers need to be prevented
 * from receiving interrupts and this function is provided for this purpose.
 * It marks all interrupt lines in use, except for the timer ones, as disabled
 * and sets the IRQS_SUSPENDED flag for each of them.
 */
void suspend_device_irqs(void)
{
	struct irq_desc *desc;
	int irq;

	for_each_irq_desc(irq, desc) {
		unsigned long flags;

		if (irq_settings_is_nested_thread(desc))
			continue;
		raw_spin_lock_irqsave(&desc->lock, flags);
		__disable_irq(desc, irq, true);
		raw_spin_unlock_irqrestore(&desc->lock, flags);
	}

	for_each_irq_desc(irq, desc)
		if (desc->istate & IRQS_SUSPENDED)
			synchronize_irq(irq);
}
EXPORT_SYMBOL_GPL(suspend_device_irqs);

static void resume_irqs(bool want_early)
{
	struct irq_desc *desc;
	int irq;

	for_each_irq_desc(irq, desc) {
		unsigned long flags;
		bool is_early = desc->action &&
			desc->action->flags & IRQF_EARLY_RESUME;

		if (!is_early && want_early)
			continue;
		if (irq_settings_is_nested_thread(desc))
			continue;

		raw_spin_lock_irqsave(&desc->lock, flags);
		__enable_irq(desc, irq, true);
		raw_spin_unlock_irqrestore(&desc->lock, flags);
	}
}

/**
 * irq_pm_syscore_ops - enable interrupt lines early
 *
 * Enable all interrupt lines with %IRQF_EARLY_RESUME set.
 */
static void irq_pm_syscore_resume(void)
{
	resume_irqs(true);
}

static struct syscore_ops irq_pm_syscore_ops = {
	.resume		= irq_pm_syscore_resume,
};

static int __init irq_pm_init_ops(void)
{
	register_syscore_ops(&irq_pm_syscore_ops);
	return 0;
}

device_initcall(irq_pm_init_ops);

/**
 * resume_device_irqs - enable interrupt lines disabled by suspend_device_irqs()
 *
 * Enable all non-%IRQF_EARLY_RESUME interrupt lines previously
 * disabled by suspend_device_irqs() that have the IRQS_SUSPENDED flag
 * set as well as those with %IRQF_FORCE_RESUME.
 */
void resume_device_irqs(void)
{
	resume_irqs(false);
}
EXPORT_SYMBOL_GPL(resume_device_irqs);

/**
 * check_wakeup_irqs - check if any wake-up interrupts are pending
 */
int check_wakeup_irqs(void)
{
	struct irq_desc *desc;
	/* FIXME : Fix build error */
	/* char suspend_abort[MAX_SUSPEND_ABORT_LEN]; */
	int irq;

	for_each_irq_desc(irq, desc) {
		if (irqd_is_wakeup_set(&desc->irq_data)) {
			if (desc->istate & IRQS_PENDING) {
				log_suspend_abort_reason("Wakeup IRQ %d %s pending",
					irq,
					desc->action && desc->action->name ?
					desc->action->name : "");
				pr_info("Wakeup IRQ %d %s pending, suspend aborted\n",
					irq,
					desc->action && desc->action->name ?
					desc->action->name : "");
				return -EBUSY;
			}
			continue;
		}
		/*
		 * Check the non wakeup interrupts whether they need
		 * to be masked before finally going into suspend
		 * state. That's for hardware which has no wakeup
		 * source configuration facility. The chip
		 * implementation indicates that with
		 * IRQCHIP_MASK_ON_SUSPEND.
		 */
		if (desc->istate & IRQS_SUSPENDED &&
		    irq_desc_get_chip(desc)->flags & IRQCHIP_MASK_ON_SUSPEND)
			mask_irq(desc);
	}

	return 0;
}
