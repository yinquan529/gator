/**
 * Copyright (C) ARM Limited 2010-2011. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include "gator.h"
#include <trace/events/irq.h>

#define HARDIRQ		0
#define SOFTIRQ		1
#define TOTALIRQ	(SOFTIRQ+1)

static ulong hardirq_enabled;
static ulong softirq_enabled;
static ulong hardirq_key;
static ulong softirq_key;
static DEFINE_PER_CPU(int[TOTALIRQ], irqCnt);
static DEFINE_PER_CPU(int[TOTALIRQ], irqPrev);
static DEFINE_PER_CPU(int[TOTALIRQ * 2], irqGet);

GATOR_DEFINE_PROBE(irq_handler_exit, TP_PROTO(int irq,
		struct irqaction *action, int ret))
{
	unsigned long flags;

	// disable interrupts to synchronize with gator_events_irq_read()
	// spinlocks not needed since percpu buffers are used
	local_irq_save(flags);
	per_cpu(irqCnt, raw_smp_processor_id())[HARDIRQ]++;
	local_irq_restore(flags);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 37)
GATOR_DEFINE_PROBE(softirq_exit, TP_PROTO(struct softirq_action *h, struct softirq_action *vec))
#else
GATOR_DEFINE_PROBE(softirq_exit, TP_PROTO(unsigned int vec_nr))
#endif
{
	unsigned long flags;

	// disable interrupts to synchronize with gator_events_irq_read()
	// spinlocks not needed since percpu buffers are used
	local_irq_save(flags);
	per_cpu(irqCnt, raw_smp_processor_id())[SOFTIRQ]++;
	local_irq_restore(flags);
}

static int gator_events_irq_create_files(struct super_block *sb, struct dentry *root)
{
	struct dentry *dir;

	/* irq */
	dir = gatorfs_mkdir(sb, root, "Linux_irq_irq");
	if (!dir) {
		return -1;
	}
	gatorfs_create_ulong(sb, dir, "enabled", &hardirq_enabled);
	gatorfs_create_ro_ulong(sb, dir, "key", &hardirq_key);

	/* soft irq */
	dir = gatorfs_mkdir(sb, root, "Linux_irq_softirq");
	if (!dir) {
		return -1;
	}
	gatorfs_create_ulong(sb, dir, "enabled", &softirq_enabled);
	gatorfs_create_ro_ulong(sb, dir, "key", &softirq_key);

	return 0;
}

static int gator_events_irq_init(int *key)
{
	hardirq_key = *key;
	*key = *key + 1;
	softirq_key = *key;
	*key = *key + 1;

	hardirq_enabled = 0;
	softirq_enabled = 0;

	return 0;
}

static int gator_events_irq_start(void)
{
	int cpu, i;

	for_each_present_cpu(cpu) {
		for (i = 0; i < TOTALIRQ; i++)
			per_cpu(irqPrev, cpu)[i] = 0;
	}

	// register tracepoints
	if (hardirq_enabled)
		if (GATOR_REGISTER_TRACE(irq_handler_exit))
			goto fail_hardirq_exit;
	if (softirq_enabled)
		if (GATOR_REGISTER_TRACE(softirq_exit))
			goto fail_softirq_exit;
	pr_debug("gator: registered irq tracepoints\n");

	return 0;

	// unregister tracepoints on error
fail_softirq_exit:
	if (hardirq_enabled)
		GATOR_UNREGISTER_TRACE(irq_handler_exit);
fail_hardirq_exit:
	pr_err("gator: irq tracepoints failed to activate, please verify that tracepoints are enabled in the linux kernel\n");

	return -1;
}

static void gator_events_irq_stop(void)
{
	if (hardirq_enabled)
		GATOR_UNREGISTER_TRACE(irq_handler_exit);
	if (softirq_enabled)
		GATOR_UNREGISTER_TRACE(softirq_exit);
	pr_debug("gator: unregistered irq tracepoints\n");

	hardirq_enabled = 0;
	softirq_enabled = 0;
}

static int gator_events_irq_read(int **buffer)
{
	unsigned long flags;
	int len, value;
	int cpu = raw_smp_processor_id();

	len = 0;
	if (hardirq_enabled) {
		local_irq_save(flags);
		value = per_cpu(irqCnt, cpu)[HARDIRQ];
		per_cpu(irqCnt, cpu)[HARDIRQ] = 0;
		local_irq_restore(flags);
		if (value != per_cpu(irqPrev, cpu)[HARDIRQ]) {
			per_cpu(irqPrev, cpu)[HARDIRQ] = value;
			per_cpu(irqGet, cpu)[len++] = hardirq_key;
			per_cpu(irqGet, cpu)[len++] = value;
		}
	}

	if (softirq_enabled) {
		local_irq_save(flags);
		value = per_cpu(irqCnt, cpu)[SOFTIRQ];
		per_cpu(irqCnt, cpu)[SOFTIRQ] = 0;
		local_irq_restore(flags);
		if (value != per_cpu(irqPrev, cpu)[SOFTIRQ]) {
			per_cpu(irqPrev, cpu)[SOFTIRQ] = value;
			per_cpu(irqGet, cpu)[len++] = softirq_key;
			per_cpu(irqGet, cpu)[len++] = value;
		}
	}

	if (buffer)
		*buffer = per_cpu(irqGet, cpu);

	return len;
}

int gator_events_irq_install(gator_interface *gi) {
	gi->create_files = gator_events_irq_create_files;
	gi->init = gator_events_irq_init;
	gi->start = gator_events_irq_start;
	gi->stop = gator_events_irq_stop;
	gi->read = gator_events_irq_read;
	return 0;
}
