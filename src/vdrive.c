/*  Copyright 2003-2017 Ciaran Anscomb
 *
 *  This file is part of XRoar.
 *
 *  XRoar is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  XRoar is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XRoar.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "xalloc.h"

#include "events.h"
#include "logging.h"
#include "vdisk.h"
#include "vdrive.h"
#include "xroar.h"

#define BYTE_TIME (EVENT_TICK_RATE / 31250)
#define MAX_DRIVES VDRIVE_MAX_DRIVES
#define MAX_SIDES (2)
#define MAX_TRACKS (256)

#define IDAM(vip,i) ((unsigned)((vip)->idamptr[i] & 0x3fff))

struct drive_data {
	struct vdisk *disk;
	unsigned current_cyl;
};

struct vdrive_interface_private {
	struct vdrive_interface public;

	_Bool ready_state;
	_Bool tr00_state;
	_Bool index_state;
	_Bool write_protect_state;

	struct drive_data drives[MAX_DRIVES];
	struct drive_data *current_drive;
	int cur_direction;
	unsigned cur_drive_number;
	unsigned cur_head;
	unsigned cur_density;
	unsigned head_incr;  // bytes per write - 2 in SD, 1 in DD
	uint8_t *track_base;  // updated to point to base addr of cur track
	uint16_t *idamptr;  // likewise, but different data size
	unsigned head_pos;  // index into current track for read/write

	event_ticks last_update_cycle;
	event_ticks track_start_cycle;
	struct event index_pulse_event;
	struct event reset_index_pulse_event;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/* Public methods */

static void vdrive_set_dirc(void *sptr, int direction);
static void vdrive_set_dden(void *sptr, _Bool dden);
static void vdrive_set_sso(void *sptr, unsigned head);
static void vdrive_set_drive(struct vdrive_interface *vi, unsigned drive);

static unsigned vdrive_get_head_pos(void *sptr);
static void vdrive_step(void *sptr);
static void vdrive_write(void *sptr, uint8_t data);
static void vdrive_skip(void *sptr);
static uint8_t vdrive_read(void *sptr);
static void vdrive_write_idam(void *sptr);
static unsigned vdrive_time_to_next_byte(void *sptr);
static unsigned vdrive_time_to_next_idam(void *sptr);
static uint8_t *vdrive_next_idam(void *sptr);
static void vdrive_update_connection(void *sptr);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/* Support */

static void set_ready_state(struct vdrive_interface_private *vip, _Bool state);
static void set_tr00_state(struct vdrive_interface_private *vip, _Bool state);
static void set_index_state(struct vdrive_interface_private *vip, _Bool state);
static void set_write_protect_state(struct vdrive_interface_private *vip, _Bool state);
static void update_signals(struct vdrive_interface_private *vip);
static int compar_idams(const void *aa, const void *bb);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/* Event handlers */

static void do_index_pulse(void *sptr);
static void do_reset_index_pulse(void *sptr);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct vdrive_interface *vdrive_interface_new(void) {
	struct vdrive_interface_private *vip = xmalloc(sizeof(*vip));
	*vip = (struct vdrive_interface_private){0};
	struct vdrive_interface *vi = &vip->public;

	vip->tr00_state = 1;
	vip->current_drive = &vip->drives[0];
	vip->cur_direction = 1;
	vip->cur_density = VDISK_SINGLE_DENSITY;
	vip->head_incr = 2;  // SD

	vdrive_disconnect(vi);

	vi->set_dirc = vdrive_set_dirc;
	vi->set_dden = vdrive_set_dden;
	vi->set_sso = vdrive_set_sso;
	vi->set_drive = vdrive_set_drive;

	vi->get_head_pos = vdrive_get_head_pos;
	vi->step = vdrive_step;
	vi->write = vdrive_write;
	vi->skip = vdrive_skip;
	vi->read = vdrive_read;
	vi->write_idam = vdrive_write_idam;
	vi->time_to_next_byte = vdrive_time_to_next_byte;
	vi->time_to_next_idam = vdrive_time_to_next_idam;
	vi->next_idam = vdrive_next_idam;
	vi->update_connection = vdrive_update_connection;

	vdrive_set_dden(vi, 1);
	vdrive_set_drive(vi, 0);
	event_init(&vip->index_pulse_event, DELEGATE_AS0(void, do_index_pulse, vip));
	event_init(&vip->reset_index_pulse_event, DELEGATE_AS0(void, do_reset_index_pulse, vip));
	return vi;
}

void vdrive_interface_free(struct vdrive_interface *vi) {
	if (!vi)
		return;
	struct vdrive_interface_private *vip = (struct vdrive_interface_private *)vi;
	for (unsigned i = 0; i < MAX_DRIVES; i++) {
		if (vip->drives[i].disk) {
			vdrive_eject_disk(vi, i);
		}
	}
}

void vdrive_disconnect(struct vdrive_interface *vi) {
	if (!vi)
		return;
	vi->ready = DELEGATE_DEFAULT1(void, bool);
	vi->tr00 = DELEGATE_DEFAULT1(void, bool);
	vi->index_pulse = DELEGATE_DEFAULT1(void, bool);
	vi->write_protect = DELEGATE_DEFAULT1(void, bool);
}

void vdrive_insert_disk(struct vdrive_interface *vi, unsigned drive, struct vdisk *disk) {
	struct vdrive_interface_private *vip = (struct vdrive_interface_private *)vi;
	assert(drive < MAX_DRIVES);
	if (vip->drives[drive].disk) {
		vdrive_eject_disk(vi, drive);
	}
	if (disk == NULL)
		return;
	vip->drives[drive].disk = disk;
	update_signals(vip);
}

void vdrive_eject_disk(struct vdrive_interface *vi, unsigned drive) {
	struct vdrive_interface_private *vip = (struct vdrive_interface_private *)vi;
	assert(drive < MAX_DRIVES);
	if (!vip->drives[drive].disk)
		return;
	vdisk_save(vip->drives[drive].disk, 0);
	vdisk_destroy(vip->drives[drive].disk);
	vip->drives[drive].disk = NULL;
	update_signals(vip);
}

struct vdisk *vdrive_disk_in_drive(struct vdrive_interface *vi, unsigned drive) {
	struct vdrive_interface_private *vip = (struct vdrive_interface_private *)vi;
	assert(drive < MAX_DRIVES);
	return vip->drives[drive].disk;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/* Signals to all drives */

static void vdrive_set_dirc(void *sptr, int direction) {
	struct vdrive_interface_private *vip = sptr;
	vip->cur_direction = (direction > 0) ? 1 : -1;
}

static void vdrive_set_dden(void *sptr, _Bool dden) {
	struct vdrive_interface_private *vip = sptr;
	vip->cur_density = dden ? VDISK_DOUBLE_DENSITY : VDISK_SINGLE_DENSITY;
	vip->head_incr = dden ? 1 : 2;
}

static void vdrive_set_sso(void *sptr, unsigned head) {
	struct vdrive_interface_private *vip = sptr;
	if (head >= MAX_SIDES)
		return;
	vip->cur_head = head;
	update_signals(vip);
}

/* Drive select */

static void vdrive_set_drive(struct vdrive_interface *vi, unsigned drive) {
	struct vdrive_interface_private *vip = (struct vdrive_interface_private *)vi;
	if (drive >= MAX_DRIVES) return;
	vip->cur_drive_number = drive;
	vip->current_drive = &vip->drives[drive];
	update_signals(vip);
}

/* Operations on selected drive */

static unsigned vdrive_get_head_pos(void *sptr) {
	struct vdrive_interface_private *vip = sptr;
	return vip->head_pos;
}

void vdrive_step(void *sptr) {
	struct vdrive_interface_private *vip = sptr;
	if (vip->ready_state) {
		if (vip->cur_direction > 0 || vip->current_drive->current_cyl > 0)
			vip->current_drive->current_cyl += vip->cur_direction;
		if (vip->current_drive->current_cyl >= MAX_TRACKS)
			vip->current_drive->current_cyl = MAX_TRACKS - 1;
	}
	update_signals(vip);
}

void vdrive_write(void *sptr, uint8_t data) {
	struct vdrive_interface_private *vip = sptr;
	if (!vip->ready_state) return;
	if (!vip->track_base) {
		vip->idamptr = vdisk_extend_disk(vip->current_drive->disk, vip->current_drive->current_cyl, vip->cur_head);
		vip->track_base = (uint8_t *)vip->idamptr;
	}
	for (unsigned i = vip->head_incr; i; i--) {
		if (vip->track_base && vip->head_pos < vip->current_drive->disk->track_length) {
			vip->track_base[vip->head_pos] = data;
			for (unsigned j = 0; j < 64; j++) {
				if (vip->head_pos == (vip->idamptr[j] & 0x3fff)) {
					vip->idamptr[j] = 0;
					qsort(vip->idamptr, 64, sizeof(uint16_t), compar_idams);
				}
			}
		}
		vip->head_pos++;
	}
	if (vip->head_pos >= vip->current_drive->disk->track_length) {
		set_index_state(vip, 1);
	}
}

void vdrive_skip(void *sptr) {
	struct vdrive_interface_private *vip = sptr;
	if (!vip->ready_state) return;
	vip->head_pos += vip->head_incr;
	if (vip->head_pos >= vip->current_drive->disk->track_length) {
		set_index_state(vip, 1);
	}
}

uint8_t vdrive_read(void *sptr) {
	struct vdrive_interface_private *vip = sptr;
	uint8_t ret = 0;
	if (!vip->ready_state) return 0;
	if (vip->track_base && vip->head_pos < vip->current_drive->disk->track_length) {
		ret = vip->track_base[vip->head_pos] & 0xff;
	}
	vip->head_pos += vip->head_incr;
	if (vip->head_pos >= vip->current_drive->disk->track_length) {
		set_index_state(vip, 1);
	}
	return ret;
}

void vdrive_write_idam(void *sptr) {
	struct vdrive_interface_private *vip = sptr;
	if (!vip->track_base) {
		vip->idamptr = vdisk_extend_disk(vip->current_drive->disk, vip->current_drive->current_cyl, vip->cur_head);
		vip->track_base = (uint8_t *)vip->idamptr;
	}
	if (vip->track_base && (vip->head_pos+vip->head_incr) < vip->current_drive->disk->track_length) {
		/* Write 0xfe and remove old IDAM ptr if it exists */
		for (unsigned i = 0; i < 64; i++) {
			for (unsigned j = 0; j < vip->head_incr; j++) {
				vip->track_base[vip->head_pos + j] = 0xfe;
				if ((vip->head_pos + j) == IDAM(vip, j)) {
					vip->idamptr[i] = 0;
				}
			}
		}
		/* Add to end of idam list and sort */
		vip->idamptr[63] = vip->head_pos | vip->cur_density;
		qsort(vip->idamptr, 64, sizeof(uint16_t), compar_idams);
	}
	vip->head_pos += vip->head_incr;
	if (vip->head_pos >= vip->current_drive->disk->track_length) {
		set_index_state(vip, 1);
	}
}

unsigned vdrive_time_to_next_byte(void *sptr) {
	struct vdrive_interface_private *vip = sptr;
	event_ticks next_cycle = vip->track_start_cycle + (vip->head_pos - 128) * BYTE_TIME;
	unsigned to_time = next_cycle - event_current_tick;
	if (to_time > (UINT_MAX/2)) {
		LOG_DEBUG(3, "Negative time to next byte!\n");
		return 1;
	}
	return to_time;
}

/* Calculates the number of cycles it would take to get from the current head
 * position to the next IDAM or next index pulse, whichever comes first. */

unsigned vdrive_time_to_next_idam(void *sptr) {
	struct vdrive_interface_private *vip = sptr;
	event_ticks next_cycle;
	if (!vip->ready_state)
		return EVENT_MS(200);
	/* Update head_pos based on time elapsed since track start */
	vip->head_pos = 128 + ((event_current_tick - vip->track_start_cycle) / BYTE_TIME);
	unsigned next_head_pos = vip->current_drive->disk->track_length;
	if (vip->idamptr) {
		for (unsigned i = 0; i < 64; i++) {
			if ((unsigned)(vip->idamptr[i] & 0x8000) == vip->cur_density) {
				unsigned tmp = vip->idamptr[i] & 0x3fff;
				if (vip->head_pos < tmp && tmp < next_head_pos)
					next_head_pos = tmp;
			}
		}
	}
	if (next_head_pos >= vip->current_drive->disk->track_length) {
		return vip->index_pulse_event.at_tick - event_current_tick;
	}
	next_cycle = vip->track_start_cycle + (next_head_pos - 128) * BYTE_TIME;
	unsigned to_time = next_cycle - event_current_tick;
	if (to_time > (UINT_MAX/2)) {
		LOG_DEBUG(3, "Negative time to next IDAM!\n");
		return 1;
	}
	return to_time;
}

/* Updates head_pos to next IDAM and returns a pointer to it.  If no valid
 * IDAMs are present, an index pulse is generated and the head left at the
 * beginning of the track. */

uint8_t *vdrive_next_idam(void *sptr) {
	struct vdrive_interface_private *vip = sptr;
	unsigned next_head_pos;
	if (!vip->ready_state) return NULL;
	next_head_pos = vip->current_drive->disk->track_length;
	if (vip->idamptr) {
		for (unsigned i = 0; i < 64; i++) {
			if ((vip->idamptr[i] & 0x8000) == vip->cur_density) {
				unsigned tmp = vip->idamptr[i] & 0x3fff;
				if (vip->head_pos < tmp && tmp < next_head_pos)
					next_head_pos = tmp;
			}
		}
	}
	if (next_head_pos >= vip->current_drive->disk->track_length) {
		set_index_state(vip, 1);
		return NULL;
	}
	vip->head_pos = next_head_pos;
	return vip->track_base + next_head_pos;
}

static void vdrive_update_connection(void *sptr) {
	struct vdrive_interface_private *vip = sptr;
	struct vdrive_interface *vi = &vip->public;
	DELEGATE_CALL1(vi->ready, vip->ready_state);
	DELEGATE_CALL1(vi->tr00, vip->tr00_state);
	DELEGATE_CALL1(vi->index_pulse, vip->index_state);
	DELEGATE_CALL1(vi->write_protect, vip->write_protect_state);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/* Support */

static void set_ready_state(struct vdrive_interface_private *vip, _Bool state) {
	struct vdrive_interface *vi = &vip->public;
	if (vip->ready_state == state)
		return;
	vip->ready_state = state;
	DELEGATE_CALL1(vi->ready, state);
}

static void set_tr00_state(struct vdrive_interface_private *vip, _Bool state) {
	struct vdrive_interface *vi = &vip->public;
	if (vip->tr00_state == state)
		return;
	vip->tr00_state = state;
	DELEGATE_CALL1(vi->tr00, state);
}

static void set_index_state(struct vdrive_interface_private *vip, _Bool state) {
	struct vdrive_interface *vi = &vip->public;
	if (vip->index_state == state)
		return;
	vip->index_state = state;
	DELEGATE_CALL1(vi->index_pulse, state);
}

static void set_write_protect_state(struct vdrive_interface_private *vip, _Bool state) {
	struct vdrive_interface *vi = &vip->public;
	if (vip->write_protect_state == state)
		return;
	vip->write_protect_state = state;
	DELEGATE_CALL1(vi->write_protect, state);
}

static void update_signals(struct vdrive_interface_private *vip) {
	struct vdrive_interface *vi = &vip->public;
	set_ready_state(vip, vip->current_drive->disk != NULL);
	set_tr00_state(vip, vip->current_drive->current_cyl == 0);
	DELEGATE_SAFE_CALL3(vi->update_drive_cyl_head, vip->cur_drive_number, vip->current_drive->current_cyl, vip->cur_head);
	if (!vip->ready_state) {
		set_write_protect_state(vip, 0);
		vip->track_base = NULL;
		vip->idamptr = NULL;
		return;
	}
	set_write_protect_state(vip, vip->current_drive->disk->write_protect);
	if (vip->cur_head < vip->current_drive->disk->num_heads) {
		vip->idamptr = vdisk_track_base(vip->current_drive->disk, vip->current_drive->current_cyl, vip->cur_head);
	} else {
		vip->idamptr = NULL;
	}
	vip->track_base = (uint8_t *)vip->idamptr;
	if (!vip->index_pulse_event.queued) {
		vip->head_pos = 128;
		vip->track_start_cycle = event_current_tick;
		vip->index_pulse_event.at_tick = vip->track_start_cycle + (vip->current_drive->disk->track_length - 128) * BYTE_TIME;
		event_queue(&MACHINE_EVENT_LIST, &vip->index_pulse_event);
	}
}

/* Compare IDAM pointers - normal int comparison with 0 being a special case
 * to come after everything else */
static int compar_idams(const void *aa, const void *bb) {
	uint16_t a = *((const uint16_t *)aa) & 0x3fff;
	uint16_t b = *((const uint16_t *)bb) & 0x3fff;
	if (a == b) return 0;
	if (a == 0) return 1;
	if (b == 0) return -1;
	if (a < b) return -1;
	return 1;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/* Event handlers */

static void do_index_pulse(void *sptr) {
	struct vdrive_interface_private *vip = sptr;
	if (!vip->ready_state) {
		set_index_state(vip, 1);
		return;
	}
	set_index_state(vip, 1);
	vip->head_pos = 128;
	vip->last_update_cycle = vip->index_pulse_event.at_tick;
	vip->track_start_cycle = vip->index_pulse_event.at_tick;
	vip->index_pulse_event.at_tick = vip->track_start_cycle + (vip->current_drive->disk->track_length - 128) * BYTE_TIME;
	event_queue(&MACHINE_EVENT_LIST, &vip->index_pulse_event);
	vip->reset_index_pulse_event.at_tick = vip->track_start_cycle + ((vip->current_drive->disk->track_length - 128)/100) * BYTE_TIME;
	event_queue(&MACHINE_EVENT_LIST, &vip->reset_index_pulse_event);
}

static void do_reset_index_pulse(void *sptr) {
	struct vdrive_interface_private *vip = sptr;
	set_index_state(vip, 0);
}
