/** \file
 *
 *  \brief Message groups.
 *
 *  \copyright Copyright 2024 Ciaran Anscomb
 *
 *  \licenseblock This file is part of XRoar, a Dragon/Tandy CoCo emulator.
 *
 *  XRoar is free software; you can redistribute it and/or modify it under the
 *  terms of the GNU General Public License as published by the Free Software
 *  Foundation, either version 3 of the License, or (at your option) any later
 *  version.
 *
 *  See COPYING.GPL for redistribution conditions.
 *
 *  \endlicenseblock
 */

#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "delegate.h"
#include "xalloc.h"

#include "messenger.h"

struct messenger_group_member {
	messenger_notify_delegate notify;
	int client_id;
};

struct messenger_group {
	int group_id;
	char *name;
	_Bool sending;  // prevent recursive message sending
	int nmembers;
	struct messenger_group_member *members;
};

// We need to reuse client ids, but can't compact them, so maintain a free
// space map.  The map is searched each time, so client registration is a
// relatively slow operation.

static int nclients = 0;
static uint8_t *client_fsm = NULL;

// A simple list of known groups.

static int ngroups = 0;
static struct messenger_group *groups = NULL;

void messenger_shutdown(void) {
	if (groups) {
		for (int i = 0; i < ngroups; ++i) {
			if (groups[i].name) {
				free(groups[i].name);
			}
			if (groups[i].members) {
				free(groups[i].members);
			}
		}
		free(groups);
		groups = NULL;
	}
	ngroups = 0;
	free(client_fsm);
	nclients = 0;
}

int messenger_client_register(void) {
	// Find gap in free space map
	for (int i = 0; i < nclients; ++i) {
		if (!(client_fsm[i >> 3] & (1 << (i & 7)))) {
			client_fsm[i >> 3] |= (1 << (i & 7));
			return i;
		}
	}

	// No gap found, increase size of free space map
	int nbytes = (nclients | 7) + 1;
	client_fsm = xrealloc(client_fsm, nbytes);
	int client_id = nclients++;
	client_fsm[client_id >> 3] |= (1 << (client_id & 7));
	return client_id;
}

void messenger_client_unregister(int client_id) {
	if (client_id < 0 || client_id >= nclients) {
		return;
	}

	// Remove client from all groups
	for (int i = 0; i < ngroups; ++i) {
		messenger_leave_group(i, client_id);
	}

	// Mark client id free in free space map
	client_fsm[client_id >> 3] &= ~(1 << (client_id & 7));
}

static struct messenger_group *find_group(const char *name) {
	// Sanity check
	assert(ngroups == 0 || groups != NULL);
	assert(name != NULL);
	for (int i = 0; i < ngroups; ++i) {
		struct messenger_group *mgrp = &groups[i];
		if (0 == strcmp(name, mgrp->name)) {
			return mgrp;
		}
	}
	return NULL;
}

static struct messenger_group *find_or_create_group(const char *name) {
	struct messenger_group *mgrp = find_group(name);
	if (!mgrp) {
		int group_id = ngroups++;
		groups = xrealloc(groups, ngroups * sizeof(*groups));
		mgrp = &groups[group_id];
		*mgrp = (struct messenger_group){0};
		mgrp->group_id = group_id;
		mgrp->name = xstrdup(name);
	}
	return mgrp;
}

int messenger_join_group(int client_id, const char *group_name,
			 messenger_notify_delegate notify) {
	struct messenger_group *mgrp = find_or_create_group(group_name);
	assert(mgrp != NULL);

	// It's ok to not be a valid client
	if (client_id < 0 || notify.func == NULL) {
		return mgrp->group_id;
	}

	int index = mgrp->nmembers++;
	mgrp->members = xrealloc(mgrp->members, mgrp->nmembers * sizeof(*mgrp->members));
	struct messenger_group_member *gmember = &mgrp->members[index];
	*gmember = (struct messenger_group_member){0};
	gmember->notify = notify;
	gmember->client_id = client_id;

	return mgrp->group_id;
}

int messenger_preempt_group(int client_id, const char *group_name,
			    messenger_notify_delegate notify) {
	// Doesn't make sense for an invalid client to preempt a group
	assert(client_id >= 0);
	struct messenger_group *mgrp = find_or_create_group(group_name);
	assert(mgrp != NULL);

	++mgrp->nmembers;
	mgrp->members = xrealloc(mgrp->members, mgrp->nmembers * sizeof(*mgrp->members));
	if (mgrp->nmembers > 1) {
		memmove(&mgrp->members[1], &mgrp->members[0], (mgrp->nmembers - 1) * sizeof(*mgrp->members));
	}
	struct messenger_group_member *gmember = &mgrp->members[0];
	*gmember = (struct messenger_group_member){0};
	gmember->notify = notify;
	gmember->client_id = client_id;

	return mgrp->group_id;
}

void messenger_leave_group(int group_id, int client_id) {
	if (group_id < 0 || group_id >= ngroups) {
		return;
	}
	struct messenger_group *mgrp = &groups[group_id];
	for (int i = 0; i < mgrp->nmembers; ++i) {
		struct messenger_group_member *gmember = &mgrp->members[i];
		if (gmember->client_id == client_id) {
			int nfollowing = mgrp->nmembers - i - 1;
			if (nfollowing > 0) {
				struct messenger_group_member *next = &mgrp->members[i+1];
				memmove(gmember, next, nfollowing * sizeof(*gmember));
			}
			--mgrp->nmembers;
			return;
		}
	}
}

void messenger_leave_group_by_name(const char *group, int client_id) {
	struct messenger_group *mgrp = find_group(group);
	if (mgrp) {
		int group_id = mgrp->group_id;
		messenger_leave_group(group_id, client_id);
	}
}

void messenger_send_message(int group_id, int client_id, int type, void *message) {
	if (group_id < 0 || group_id >= ngroups) {
		return;
	}
	struct messenger_group *mgrp = &groups[group_id];
	// Block recursive sending to group
	if (mgrp->sending) {
		return;
	}
	mgrp->sending = 1;
	for (int i = 0; i < mgrp->nmembers; ++i) {
		struct messenger_group_member *member = &mgrp->members[i];
		if (member->client_id != client_id) {
			DELEGATE_SAFE_CALL(member->notify, type, message);
		}
	}
	mgrp->sending = 0;
}

extern inline void messenger_send_message_to_all(int group_id, int type, void *message);
