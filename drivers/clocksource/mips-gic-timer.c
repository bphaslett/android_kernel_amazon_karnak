/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2012 MIPS Technologies, Inc.  All rights reserved.
 */
#include <linux/clockchips.h>
#include <linux/cpu.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irqchip/mips-gic.h>
#include <linux/notifier.h>
#include <linux/percpu.h>
#include <linux/smp.h>
#include <linux/time.h>

static DEFINE_PER_CPU(struct clock_event_device, gic_clockevent_device);
static int gic_timer_irq;
static unsigned int gic_frequency;

static int gic_next_event(unsigned long delta, struct clock_event_device *evt)
{
	u64 cnt;
	int res;

	cnt = gic_read_count();
	cnt += (u64)delta;
	gic_write_cpu_compare(cnt, cpumask_first(evt->cpumask));
	res = ((int)(gic_read_count() - cnt) >= 0) ? -ETIME : 0;
	return res;
}

static void gic_set_clock_mode(enum clock_event_mode mode,
				struct clock_event_device *evt)
{
	/* Nothing to do ...  */
}

static irqreturn_t gic_compare_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *cd = dev_id;

	gic_write_compare(gic_read_compare());
	cd->event_handler(cd);
	return IRQ_HANDLED;
}

struct irqaction gic_compare_irqaction = {
	.handler = gic_compare_interrupt,
	.percpu_dev_id = &gic_clockevent_device,
	.flags = IRQF_PERCPU | IRQF_TIMER,
	.name = "timer",
};

static void gic_clockevent_cpu_init(struct clock_event_device *cd)
{
	unsigned int cpu = smp_processor_id();

	cd->name		= "MIPS GIC";
	cd->features		= CLOCK_EVT_FEAT_ONESHOT |
				  CLOCK_EVT_FEAT_C3STOP;

	cd->rating		= 300;
	cd->irq			= gic_timer_irq;
	cd->cpumask		= cpumask_of(cpu);
	cd->set_next_event	= gic_next_event;
	cd->set_mode		= gic_set_clock_mode;

	clockevents_config_and_register(cd, gic_frequency, 0x300, 0x7fffffff);

	enable_percpu_irq(gic_timer_irq, IRQ_TYPE_NONE);
}

static void gic_clockevent_cpu_exit(struct clock_event_device *cd)
{
	disable_percpu_irq(gic_timer_irq);
}

static int gic_cpu_notifier(struct notifier_block *nb, unsigned long action,
				void *data)
{
	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_STARTING:
		gic_clockevent_cpu_init(this_cpu_ptr(&gic_clockevent_device));
		break;
	case CPU_DYING:
		gic_clockevent_cpu_exit(this_cpu_ptr(&gic_clockevent_device));
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block gic_cpu_nb = {
	.notifier_call = gic_cpu_notifier,
};

static int gic_clockevent_init(void)
{
	if (!cpu_has_counter || !gic_frequency)
		return -ENXIO;

	gic_timer_irq = MIPS_GIC_IRQ_BASE +
		GIC_LOCAL_TO_HWIRQ(GIC_LOCAL_INT_COMPARE);
	setup_percpu_irq(gic_timer_irq, &gic_compare_irqaction);

	register_cpu_notifier(&gic_cpu_nb);

	gic_clockevent_cpu_init(this_cpu_ptr(&gic_clockevent_device));

	return 0;
}

static cycle_t gic_hpt_read(struct clocksource *cs)
{
	return gic_read_count();
}

static struct clocksource gic_clocksource = {
	.name	= "GIC",
	.read	= gic_hpt_read,
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
};

void __init gic_clocksource_init(unsigned int frequency)
{
	gic_frequency = frequency;

	/* Set clocksource mask. */
	gic_clocksource.mask = CLOCKSOURCE_MASK(gic_get_count_width());

	/* Calculate a somewhat reasonable rating value. */
	gic_clocksource.rating = 200 + frequency / 10000000;

	clocksource_register_hz(&gic_clocksource, frequency);

	gic_clockevent_init();
}
