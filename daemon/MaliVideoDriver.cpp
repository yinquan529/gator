/**
 * Copyright (C) ARM Limited 2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "MaliVideoDriver.h"

#include <unistd.h>

#include "Buffer.h"
#include "Counter.h"
#include "Logging.h"
#include "SessionData.h"

// From instr/src/mve_instr_comm_protocol.h
typedef enum mve_instr_configuration_type {
	MVE_INSTR_RAW         = 1 << 0,
	MVE_INSTR_COUNTERS    = 1 << 1,
	MVE_INSTR_EVENTS      = 1 << 2,
	MVE_INSTR_ACTIVITIES  = 1 << 3,

	// Raw always pushed regardless
	MVE_INSTR_PULL        = 1 << 12,
	// Raw always unpacked regardless
	MVE_INSTR_PACKED_COMM = 1 << 13,
	// Don’t send ACKt response
	MVE_INSTR_NO_AUTO_ACK   = 1 << 14,
} mve_instr_configuration_type_t;

static const char COUNTER[] = "ARM_Mali-V500_cnt";
static const char EVENT[] = "ARM_Mali-V500_evn";
static const char ACTIVITY[] = "ARM_Mali-V500_act";

class MaliVideoCounter {
public:
	MaliVideoCounter(MaliVideoCounter *next, const char *name, const MaliVideoCounterType type, const int id) : mNext(next), mName(name), mType(type), mId(id), mKey(getEventKey()), mEnabled(false) {
	}

	~MaliVideoCounter() {
		delete mName;
	}

	MaliVideoCounter *getNext() const { return mNext; }
	const char *getName() const { return mName; }
	MaliVideoCounterType getType() const { return mType; }
	int getId() const { return mId; }
	int getKey() const { return mKey; }
	bool isEnabled() const { return mEnabled; }
	void setEnabled(const bool enabled) { mEnabled = enabled; }

private:
	MaliVideoCounter *const mNext;
	const char *const mName;
	const MaliVideoCounterType mType;
	// Mali Video id
	const int mId;
	// Streamline key
	const int mKey;
	bool mEnabled;
};

MaliVideoDriver::MaliVideoDriver() : mCounters(NULL), mActivityCount(0) {
}

MaliVideoDriver::~MaliVideoDriver() {
	while (mCounters != NULL) {
		MaliVideoCounter *counter = mCounters;
		mCounters = counter->getNext();
		delete counter;
	}
}

void MaliVideoDriver::setup(mxml_node_t *const xml) {
	// hwmon does not currently work with perf
	if (gSessionData->perf.isSetup()) {
		return;
	}

	mxml_node_t *node = xml;
	while (true) {
		node = mxmlFindElement(node, xml, "event", NULL, NULL, MXML_DESCEND);
		if (node == NULL) {
			break;
		}
		const char *counter = mxmlElementGetAttr(node, "counter");
		if (counter == NULL) {
			// Ignore
		} else if (strncmp(counter, COUNTER, sizeof(COUNTER) - 1) == 0) {
			const int i = strtol(counter + sizeof(COUNTER) - 1, NULL, 10);
			mCounters = new MaliVideoCounter(mCounters, strdup(counter), MVCT_COUNTER, i);
		} else if (strncmp(counter, EVENT, sizeof(EVENT) - 1) == 0) {
			const int i = strtol(counter + sizeof(EVENT) - 1, NULL, 10);
			mCounters = new MaliVideoCounter(mCounters, strdup(counter), MVCT_EVENT, i);
		} else if (strcmp(counter, ACTIVITY) == 0) {
			mCounters = new MaliVideoCounter(mCounters, strdup(ACTIVITY), MVCT_ACTIVITY, 0);
			mActivityCount = 0;
			while (true) {
				char buf[32];
				snprintf(buf, sizeof(buf), "activity%i", mActivityCount + 1);
				if (mxmlElementGetAttr(node, buf) == NULL) {
					break;
				}
				++mActivityCount;
			}
		}
	}
}

MaliVideoCounter *MaliVideoDriver::findCounter(const Counter &counter) const {
	for (MaliVideoCounter *maliVideoCounter = mCounters; maliVideoCounter != NULL; maliVideoCounter = maliVideoCounter->getNext()) {
		if (strcmp(maliVideoCounter->getName(), counter.getType()) == 0) {
			return maliVideoCounter;
		}
	}

	return NULL;
}

bool MaliVideoDriver::claimCounter(const Counter &counter) const {
	return findCounter(counter) != NULL;
}

bool MaliVideoDriver::countersEnabled() const {
	for (MaliVideoCounter * counter = mCounters; counter != NULL; counter = counter->getNext()) {
		if (counter->isEnabled()) {
			return true;
		}
	}
	return false;
}

void MaliVideoDriver::resetCounters() {
	for (MaliVideoCounter * counter = mCounters; counter != NULL; counter = counter->getNext()) {
		counter->setEnabled(false);
	}
}

void MaliVideoDriver::setupCounter(Counter &counter) {
	MaliVideoCounter *const maliVideoCounter = findCounter(counter);
	if (maliVideoCounter == NULL) {
		counter.setEnabled(false);
		return;
	}
	maliVideoCounter->setEnabled(true);
	counter.setKey(maliVideoCounter->getKey());
}

int MaliVideoDriver::writeCounters(mxml_node_t *root) const {
	if (access("/dev/mv500", F_OK) != 0) {
		return 0;
	}

	int count = 0;
	for (MaliVideoCounter * counter = mCounters; counter != NULL; counter = counter->getNext()) {
		mxml_node_t *node = mxmlNewElement(root, "counter");
		mxmlElementSetAttr(node, "name", counter->getName());
		++count;
	}

	return count;
}

void MaliVideoDriver::marshalEnable(const MaliVideoCounterType type, char *const buf, const size_t bufsize, int &pos) {
	// size
	int numEnabled = 0;
	for (MaliVideoCounter * counter = mCounters; counter != NULL; counter = counter->getNext()) {
		if (counter->isEnabled() && (counter->getType() == type)) {
			++numEnabled;
		}
	}
	Buffer::packInt(buf, bufsize, pos, numEnabled*sizeof(uint32_t));
	for (MaliVideoCounter * counter = mCounters; counter != NULL; counter = counter->getNext()) {
		if (counter->isEnabled() && (counter->getType() == type)) {
			Buffer::packInt(buf, bufsize, pos, counter->getId());
		}
	}
}

bool MaliVideoDriver::start(const int mveUds) {
	char buf[256];
	int pos = 0;

	// code - MVE_INSTR_STARTUP
	buf[pos++] = 'C';
	buf[pos++] = 'L';
	buf[pos++] = 'N';
	buf[pos++] = 'T';
	// size
	Buffer::packInt(buf, sizeof(buf), pos, sizeof(uint32_t));
	// client_version_number
	Buffer::packInt(buf, sizeof(buf), pos, 1);

	// code - MVE_INSTR_CONFIGURE
	buf[pos++] = 'C';
	buf[pos++] = 'N';
	buf[pos++] = 'F';
	buf[pos++] = 'G';
	// size
	Buffer::packInt(buf, sizeof(buf), pos, 5*sizeof(uint32_t));
	// configuration
	Buffer::packInt(buf, sizeof(buf), pos, MVE_INSTR_COUNTERS | MVE_INSTR_EVENTS | MVE_INSTR_ACTIVITIES | MVE_INSTR_PACKED_COMM);
	// communication_protocol_version
	Buffer::packInt(buf, sizeof(buf), pos, 1);
	// data_protocol_version
	Buffer::packInt(buf, sizeof(buf), pos, 1);
	// sample_rate - convert samples/second to ms/sample
	Buffer::packInt(buf, sizeof(buf), pos, 1000/gSessionData->mSampleRate);
	// live_rate - convert ns/flush to ms/flush
	Buffer::packInt(buf, sizeof(buf), pos, gSessionData->mLiveRate/1000000);

	// code - MVE_INSTR_ENABLE_COUNTERS
	buf[pos++] = 'C';
	buf[pos++] = 'F';
	buf[pos++] = 'G';
	buf[pos++] = 'c';
	marshalEnable(MVCT_COUNTER, buf, sizeof(buf), pos);

	// code - MVE_INSTR_ENABLE_EVENTS
	buf[pos++] = 'C';
	buf[pos++] = 'F';
	buf[pos++] = 'G';
	buf[pos++] = 'e';
	marshalEnable(MVCT_EVENT, buf, sizeof(buf), pos);

	/*
	// code - MVE_INSTR_ENABLE_ACTIVITIES
	buf[pos++] = 'C';
	buf[pos++] = 'F';
	buf[pos++] = 'G';
	buf[pos++] = 'a';
	// size
	Buffer::packInt(buf, sizeof(buf), pos, mActivityCount*sizeof(uint32_t));
	for (int i = 0; i < mActivityCount; ++i) {
		// activity_id
		Buffer::packInt(buf, sizeof(buf), pos, i);
	}
	*/

	int written = 0;
	while (written < pos) {
		size_t bytes = ::write(mveUds, buf + written, pos - written);
		if (bytes <= 0) {
			logg->logMessage("%s(%s:%i): write failed", __FUNCTION__, __FILE__, __LINE__);
			return false;
		}
		written += bytes;
	}

	return true;
}
