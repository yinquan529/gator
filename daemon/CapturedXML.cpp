/**
 * Copyright (C) ARM Limited 2010-2013. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include "SessionData.h"
#include "CapturedXML.h"
#include "Logging.h"
#include "OlyUtility.h"

CapturedXML::CapturedXML() {
}

CapturedXML::~CapturedXML() {
}

mxml_node_t* CapturedXML::getTree(bool includeTime) {
	mxml_node_t *xml;
	mxml_node_t *captured;
	mxml_node_t *target;
	int x;

	xml = mxmlNewXML("1.0");

	captured = mxmlNewElement(xml, "captured");
	mxmlElementSetAttr(captured, "version", "1");
	mxmlElementSetAttrf(captured, "protocol", "%d", PROTOCOL_VERSION);
	if (includeTime) { // Send the following only after the capture is complete
		if (time(NULL) > 1267000000) { // If the time is reasonable (after Feb 23, 2010)
			mxmlElementSetAttrf(captured, "created", "%lu", time(NULL)); // Valid until the year 2038
		}
	}

	target = mxmlNewElement(captured, "target");
	mxmlElementSetAttr(target, "name", gSessionData->mCoreName);
	mxmlElementSetAttrf(target, "sample_rate", "%d", gSessionData->mSampleRate);
	mxmlElementSetAttrf(target, "cores", "%d", gSessionData->mCores);
	mxmlElementSetAttrf(target, "cpuid", "0x%x", gSessionData->mCpuId);

	mxml_node_t *counters = NULL;
	for (x = 0; x < MAX_PERFORMANCE_COUNTERS; x++) {
		const Counter & counter = gSessionData->mCounters[x];
		if (counter.isEnabled()) {
			if (counters == NULL) {
				counters = mxmlNewElement(captured, "counters");
			}
			mxml_node_t *const node = mxmlNewElement(counters, "counter");
			mxmlElementSetAttr(node, "title", counter.getTitle());
			mxmlElementSetAttr(node, "name", counter.getName());
			mxmlElementSetAttrf(node, "key", "0x%08x", counter.getKey());
			mxmlElementSetAttr(node, "type", counter.getType());
			mxmlElementSetAttrf(node, "event", "0x%08x", counter.getEvent());
			if (counter.isPerCPU()) {
				mxmlElementSetAttr(node, "per_cpu", "yes");
			}
			if (counter.getCount() > 0) {
				mxmlElementSetAttrf(node, "count", "%d", counter.getCount());
			}
			if (strlen(counter.getDisplay()) > 0) {
				mxmlElementSetAttr(node, "display", counter.getDisplay());
			}
			if (strlen(counter.getUnits()) > 0) {
				mxmlElementSetAttr(node, "units", counter.getUnits());
			}
			if (counter.getModifier() != 1) {
				mxmlElementSetAttrf(node, "modifier", "%d", counter.getModifier());
			}
			if (counter.isAverageSelection()) {
				mxmlElementSetAttr(node, "average_selection", "yes");
			}
			mxmlElementSetAttr(node, "description", counter.getDescription());
		}
	}

	return xml;
}

char* CapturedXML::getXML(bool includeTime) {
	char* xml_string;
	mxml_node_t *xml = getTree(includeTime);
	xml_string = mxmlSaveAllocString(xml, mxmlWhitespaceCB);
	mxmlDelete(xml);
	return xml_string;
}

void CapturedXML::write(char* path) {
	char file[PATH_MAX];

	// Set full path
	snprintf(file, PATH_MAX, "%s/captured.xml", path);
	
	char* xml = getXML(true);
	if (util->writeToDisk(file, xml) < 0) {
		logg->logError(__FILE__, __LINE__, "Error writing %s\nPlease verify the path.", file);
		handleException();
	}

	free(xml);
}

// whitespace callback utility function used with mini-xml
const char * mxmlWhitespaceCB(mxml_node_t *node, int loc) {
	const char *name;

	name = mxmlGetElement(node);

	if (loc == MXML_WS_BEFORE_OPEN) {
		// Single indentation
		if (!strcmp(name, "target") || !strcmp(name, "counters"))
			return("\n  ");

		// Double indentation
		if (!strcmp(name, "counter"))
			return("\n    ");

		// Avoid a carriage return on the first line of the xml file
		if (!strncmp(name, "?xml", 4))
			return(NULL);

		// Default - no indentation
		return("\n");
	}

	if (loc == MXML_WS_BEFORE_CLOSE) {
		// No indentation
		if (!strcmp(name, "captured"))
			return("\n");

		// Single indentation
		if (!strcmp(name, "counters"))
			return("\n  ");

		// Default - no carriage return
		return(NULL);
	}

	return(NULL);
}
