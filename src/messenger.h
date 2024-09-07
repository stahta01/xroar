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
 *
 *  Simple message sending engine.  Code can register for a client id, join
 *  groups, send messages to groups.  Registering lets you send to "all but
 *  self".  Joining a group lets you receive messages.  You don't need to have
 *  done either to send messages, but you do need to register to receive them.
 *
 *  If a client preempts a group, it's the same as joining but ensures that
 *  this client gets the message first.  Useful if this is the handler that
 *  sanitises any values and acts on them, so the rest of the recipients get
 *  the rewritten message.
 *
 *  Messages are a simple (void *) passed to a notify delegate.  Messenger
 *  knows nothing about the data, certainly doesn't copy it, so group members
 *  need to manage the lifetime and know how to interpret them.
 *
 *  While a message is sent to a group, no other messages to the same group
 *  will be accepted until all registrants have been notified.
 *
 *  Message groups are currently not pruned when they run out of members, so
 *  don't create them willy-nilly.
 */

#ifndef XROAR_MESSENGER_H
#define XROAR_MESSENGER_H

#include "delegate.h"

// Message receiver delegate type.
//     int type;
//     struct messenger_message *mmsg;

typedef DELEGATE_S2(void, int, void *) messenger_notify_delegate;

#define MESSENGER_NOTIFY_DELEGATE(f,s) (messenger_notify_delegate)DELEGATE_INIT(f,s)
#define MESSENGER_NO_NOTIFY_DELEGATE MESSENGER_NOTIFY_DELEGATE(NULL, NULL)

// Free all messenger-related data.

void messenger_shutdown(void);

// Returns a client_id or -1 on error.

int messenger_client_register(void);

// Removes client from all message groups, frees any associated metadata.

void messenger_client_unregister(int client_id);

// Join client to a named message group.  Group will be created if
// it doesn't exist.  Returns a group id, or -1 on error.

int messenger_join_group(int client_id, const char *group,
			 messenger_notify_delegate notify);

// Join client, but ensure it's the first to receive the message.  It would be
// valid for this client to change the message received by others.

int messenger_preempt_group(int client_id, const char *group,
			    messenger_notify_delegate notify);

// Remove client from message group.

void messenger_leave_group(int group_id, int client_id);

// Remove client from a named message group.

void messenger_leave_group_by_name(const char *group, int client_id);

// Send a message to all group members, excluding self.

void messenger_send_message(int group_id, int client_id, int type, void *message);

// Send a message to all group members, including self.

inline void messenger_send_message_to_all(int group_id, int type, void *message) {
	messenger_send_message(group_id, -1, type, message);
}

#endif
