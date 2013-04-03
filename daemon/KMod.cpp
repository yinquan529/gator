/**
 * Copyright (C) ARM Limited 2013. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "KMod.h"

#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>

#include "Collector.h"
#include "Counter.h"
#include "Logging.h"

// Claim all the counters in /dev/gator/events
bool KMod::claimCounter(const Counter &counter) const {
	char text[128];
	snprintf(text, sizeof(text), "/dev/gator/events/%s", counter.getType());
	return access(text, F_OK) == 0;
}

void KMod::resetCounters() {
	char base[128];
	char text[128];

	// Initialize all perf counters in the driver, i.e. set enabled to zero
	struct dirent *ent;
	DIR* dir = opendir("/dev/gator/events");
	if (dir) {
		while ((ent = readdir(dir)) != NULL) {
			// skip hidden files, current dir, and parent dir
			if (ent->d_name[0] == '.')
				continue;
			snprintf(base, sizeof(base), "/dev/gator/events/%s", ent->d_name);
			snprintf(text, sizeof(text), "%s/enabled", base);
			Collector::writeDriver(text, 0);
			snprintf(text, sizeof(text), "%s/count", base);
			Collector::writeDriver(text, 0);
		}
		closedir(dir);
	}
}

void KMod::setupCounter(Counter &counter) {
	char base[128];
	char text[128];
	snprintf(base, sizeof(base), "/dev/gator/events/%s", counter.getType());

	snprintf(text, sizeof(text), "%s/enabled", base);
	int enabled = true;
	if (Collector::writeReadDriver(text, &enabled) || !enabled) {
		counter.setEnabled(false);
		return;
	}

	snprintf(text, sizeof(text), "%s/key", base);
	int key;
	Collector::readIntDriver(text, &key);
	counter.setKey(key);

	snprintf(text, sizeof(text), "%s/event", base);
	Collector::writeDriver(text, counter.getEvent());
	if (counter.isEBSCapable()) {
		snprintf(text, sizeof(text), "%s/count", base);
		if (access(text, F_OK) == 0) {
			int count = counter.getCount();
			if (Collector::writeReadDriver(text, &count) && counter.getCount() > 0) {
				logg->logError(__FILE__, __LINE__, "Cannot enable EBS for %s:%s with a count of %d\n", counter.getTitle(), counter.getName(), counter.getCount());
				handleException();
			}
			counter.setCount(count);
		} else if (counter.getCount() > 0) {
			logg->logError(__FILE__, __LINE__, "Event Based Sampling is only supported with kernel versions 3.0.0 and higher with CONFIG_PERF_EVENTS=y, and CONFIG_HW_PERF_EVENTS=y\n");
			handleException();
		}
	}
}

void KMod::writeCounters(mxml_node_t *root) const {
	struct dirent *ent;
	mxml_node_t *counter;

	// counters.xml is simply a file listing of /dev/gator/events
	DIR* dir = opendir("/dev/gator/events");
	if (dir == NULL) {
		logg->logError(__FILE__, __LINE__, "Cannot create counters.xml since unable to read /dev/gator/events");
		handleException();
	}

	while ((ent = readdir(dir)) != NULL) {
		// skip hidden files, current dir, and parent dir
		if (ent->d_name[0] == '.')
			continue;
		counter = mxmlNewElement(root, "counter");
		mxmlElementSetAttr(counter, "name", ent->d_name);
	}
	closedir(dir);
}
