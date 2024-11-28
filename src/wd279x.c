/** \file
 *
 *  \brief WD279x Floppy Drive Controller.
 *
 *  \copyright Copyright 2003-2024 Ciaran Anscomb
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
 *  \par Sources
 *
 *  - TMS279X datasheet, http://www.swtpc.com/mholley/DC_5/TMS279X_DataSheet.pdf
 */

#include "top-config.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"

#include "crc16.h"
#include "events.h"
#include "logging.h"
#include "part.h"
#include "serialise.h"
#include "vdrive.h"
#include "wd279x.h"
#include "xroar.h"

static const struct ser_struct ser_struct_wd279x[] = {
	SER_ID_STRUCT_ELEM(1,  struct WD279X, type),

	SER_ID_STRUCT_ELEM(2,  struct WD279X, status_register),
	SER_ID_STRUCT_ELEM(3,  struct WD279X, track_register),
	SER_ID_STRUCT_ELEM(4,  struct WD279X, sector_register),
	SER_ID_STRUCT_ELEM(5,  struct WD279X, data_register),
	SER_ID_STRUCT_ELEM(6,  struct WD279X, command_register),

	SER_ID_STRUCT_ELEM(7,  struct WD279X, state),
	SER_ID_STRUCT_TYPE(8,  ser_type_event,    struct WD279X, state_event),
	SER_ID_STRUCT_ELEM(9,  struct WD279X, direction),
	SER_ID_STRUCT_ELEM(10, struct WD279X, side),
	SER_ID_STRUCT_ELEM(11, struct WD279X, step_delay),
	SER_ID_STRUCT_ELEM(12, struct WD279X, double_density),
	SER_ID_STRUCT_ELEM(13, struct WD279X, ready_state),
	SER_ID_STRUCT_ELEM(14, struct WD279X, tr00_state),
	SER_ID_STRUCT_ELEM(15, struct WD279X, index_state),
	SER_ID_STRUCT_ELEM(16, struct WD279X, write_protect_state),
	SER_ID_STRUCT_ELEM(17, struct WD279X, status_type1),

	SER_ID_STRUCT_ELEM(18, struct WD279X, intrq_nready_to_ready),
	SER_ID_STRUCT_ELEM(19, struct WD279X, intrq_ready_to_nready),
	SER_ID_STRUCT_ELEM(20, struct WD279X, intrq_index_pulse),
	SER_ID_STRUCT_ELEM(21, struct WD279X, intrq_immediate),

	SER_ID_STRUCT_ELEM(22, struct WD279X, is_step_cmd),
	SER_ID_STRUCT_ELEM(23, struct WD279X, crc),
	SER_ID_STRUCT_ELEM(24, struct WD279X, dam),
	SER_ID_STRUCT_ELEM(25, struct WD279X, bytes_left),
	SER_ID_STRUCT_ELEM(26, struct WD279X, index_holes_count),
	SER_ID_STRUCT_ELEM(27, struct WD279X, track_register_tmp),
};

static const struct ser_struct_data wd279x_ser_struct_data = {
	.elems = ser_struct_wd279x,
	.num_elems = ARRAY_N_ELEMENTS(ser_struct_wd279x),
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

#define STATUS_NOT_READY     (1<<7)
#define STATUS_WRITE_PROTECT (1<<6)
#define STATUS_HEAD_LOADED   (1<<5)
#define STATUS_RECORD_TYPE   (1<<5)
#define STATUS_SEEK_ERROR    (1<<4)
#define STATUS_RNF           (1<<4)
#define STATUS_CRC_ERROR     (1<<3)
#define STATUS_TRACK_0       (1<<2)
#define STATUS_LOST_DATA     (1<<2)
#define STATUS_INDEX_PULSE   (1<<1)
#define STATUS_DRQ           (1<<1)
#define STATUS_BUSY          (1<<0)

#define W_BYTE_TIME (EVENT_TICK_RATE / 31250)

static int const stepping_rate[4] = { 6, 12, 20, 30 };
static int const sector_size[2][4] = {
	{ 256, 512, 1024, 128 },
	{ 128, 256, 512, 1024 }
};

static const char * const wd279x_type_name[4] = {
	"WD2791", "WD2793", "WD2795", "WD2797"
};

// Helper macros & functions

#define SET_DRQ(fdc) do { \
		(fdc)->status_register |= STATUS_DRQ; \
		DELEGATE_CALL((fdc)->set_drq, 1); \
	} while (0)

#define RESET_DRQ(fdc) do { \
		(fdc)->status_register &= ~(STATUS_DRQ); \
		DELEGATE_CALL((fdc)->set_drq, 0); \
	} while (0)

#define SET_INTRQ(fdc) do { \
		DELEGATE_CALL((fdc)->set_intrq, 1); \
	} while (0)

#define RESET_INTRQ(fdc) do { \
		DELEGATE_CALL((fdc)->set_intrq, 0); \
	} while (0)

#define NEXT_STATE(fdc,s,t) do { \
		(fdc)->state = (s); \
		(fdc)->state_event.at_tick = event_current_tick + (t); \
		event_queue(&MACHINE_EVENT_LIST, &(fdc)->state_event); \
	} while (0)

#define SET_STATE(fdc,s) (fdc)->state = (s)

#define IS_DOUBLE_DENSITY(fdc) ((fdc)->double_density)
#define IS_SINGLE_DENSITY(fdc) (!(fdc)->double_density)

#define SET_DIRECTION(fdc) do { \
		(fdc)->direction = 1; \
		DELEGATE_CALL((fdc)->set_dirc, 1); \
	} while (0)

#define RESET_DIRECTION(fdc) do { \
		(fdc)->direction = -1; \
		DELEGATE_CALL((fdc)->set_dirc, 0); \
	} while (0)

#define SET_SIDE(fdc,s) do { \
		(fdc)->side = (s) ? 1 : 0; \
		if ((fdc)->has_sso) { \
			DELEGATE_CALL((fdc)->set_sso, (fdc)->side); \
		} \
	} while (0)

#define VDRIVE_WRITE_CRC16(fdc) do { \
		uint16_t tmp = (fdc)->crc; \
		wd279x_write_byte((fdc), tmp >> 8); \
		wd279x_write_byte((fdc), tmp & 0xff); \
	} while (0)

static void wd279x_state_machine(void *);
static uint8_t wd279x_read_byte(struct WD279X *);
static void wd279x_write_byte(struct WD279X *, uint8_t b);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Debugging

static void wd279x_debug_state(const struct WD279X *);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// WD279X part creation

static struct part *wd279x_allocate(void);
static void wd279x_initialise(struct part *, void *options);
static _Bool wd279x_finish(struct part *);
static void wd279x_free(struct part *);

static _Bool wd279x_is_a(struct part *, const char *name);

static const struct partdb_entry_funcs wd279x_funcs = {
	.allocate = wd279x_allocate,
	.initialise = wd279x_initialise,
	.finish = wd279x_finish,
	.free = wd279x_free,

	.ser_struct_data = &wd279x_ser_struct_data,

	.is_a = wd279x_is_a,
};

const struct partdb_entry wd2791_part = { .name = "WD2791", .description = "Western Digital | WD2791 FDC", .funcs = &wd279x_funcs };
const struct partdb_entry wd2793_part = { .name = "WD2793", .description = "Western Digital | WD2793 FDC", .funcs = &wd279x_funcs };
const struct partdb_entry wd2795_part = { .name = "WD2795", .description = "Western Digital | WD2795 FDC", .funcs = &wd279x_funcs };
const struct partdb_entry wd2797_part = { .name = "WD2797", .description = "Western Digital | WD2797 FDC", .funcs = &wd279x_funcs };

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct part *wd279x_allocate(void) {
	struct WD279X *fdc = part_new(sizeof(*fdc));
	struct part *p = &fdc->part;

	*fdc = (struct WD279X){0};

	event_init(&fdc->state_event, DELEGATE_AS0(void, wd279x_state_machine, fdc));
	wd279x_disconnect(fdc);

	return p;
}

static void wd279x_initialise(struct part *p, void *options) {
	struct WD279X *fdc = (struct WD279X *)p;

	fdc->state = WD279X_state_accept_command;

	unsigned type = WD2797;
	if (options) {
		const char *type_str = options;
		for (unsigned i = 0; i < ARRAY_N_ELEMENTS(wd279x_type_name); i++) {
			if (strcmp(type_str, wd279x_type_name[i]) == 0) {
				type = i;
				break;
			}
		}
	}
	fdc->type = type;
}

static _Bool wd279x_finish(struct part *p) {
	struct WD279X *fdc = (struct WD279X *)p;

	fdc->has_sso = (fdc->type == WD2795 || fdc->type == WD2797);
	fdc->has_length_flag = (fdc->type == WD2795 || fdc->type == WD2797);
	fdc->invert_data = (fdc->type == WD2791 || fdc->type == WD2795) ? 0xff : 0;

	if (fdc->state_event.next == &fdc->state_event) {
		event_queue(&MACHINE_EVENT_LIST, &fdc->state_event);
	}

	return 1;
}

static void wd279x_free(struct part *p) {
	struct WD279X *fdc = (struct WD279X *)p;
	log_close(&fdc->log_rsec_hex);
	log_close(&fdc->log_wsec_hex);
	log_close(&fdc->log_wtrk_hex);
	event_dequeue(&fdc->state_event);
}

static _Bool wd279x_is_a(struct part *p, const char *name) {
	if (!p)
		return 0;
	struct WD279X *fdc = (struct WD279X *)p;
	if (fdc->type >= ARRAY_N_ELEMENTS(wd279x_type_name))
		fdc->type = WD2797;
	return (strcmp(name, wd279x_type_name[fdc->type]) == 0);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static uint8_t wd279x_read_byte(struct WD279X *fdc) {
	uint8_t b = DELEGATE_CALL(fdc->read);
	fdc->crc = crc16_ccitt_byte(fdc->crc, b);
	return b;
}

static void wd279x_write_byte(struct WD279X *fdc, uint8_t b) {
	DELEGATE_CALL(fdc->write, b);
	fdc->crc = crc16_ccitt_byte(fdc->crc, b);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void wd279x_disconnect(struct WD279X *fdc) {
	if (!fdc)
		return;
	fdc->set_dirc = DELEGATE_DEFAULT1(void, bool);
	fdc->set_dden = DELEGATE_DEFAULT1(void, bool);
	fdc->set_sso = DELEGATE_DEFAULT1(void, unsigned);
	fdc->set_drq = DELEGATE_DEFAULT1(void, bool);
	fdc->set_intrq = DELEGATE_DEFAULT1(void, bool);
	fdc->step = DELEGATE_DEFAULT0(void);
	fdc->write = DELEGATE_DEFAULT1(void, uint8);
	fdc->skip = DELEGATE_DEFAULT0(void);
	fdc->read = DELEGATE_DEFAULT0(uint8);
	fdc->write_idam = DELEGATE_DEFAULT0(void);
	fdc->time_to_next_byte = DELEGATE_DEFAULT0(unsigned);
	fdc->time_to_next_idam = DELEGATE_DEFAULT0(unsigned);
	fdc->next_idam = DELEGATE_DEFAULT0(uint8p);
	fdc->update_connection = DELEGATE_DEFAULT0(void);
}

void wd279x_reset(struct WD279X *fdc) {
	assert(fdc != NULL);
	event_dequeue(&fdc->state_event);
	fdc->status_register = 0;
	fdc->track_register = 0;
	fdc->sector_register = 0;
	fdc->data_register = 0;
	fdc->command_register = 0;
	RESET_DIRECTION(fdc);
	SET_SIDE(fdc, 0);
}

void wd279x_ready(void *sptr, _Bool state) {
	struct WD279X *fdc = sptr;
	if (fdc->ready_state == state)
		return;
	fdc->ready_state = state;
	if (state && fdc->intrq_nready_to_ready) {
		event_dequeue(&fdc->state_event);
		SET_INTRQ(fdc);
	}
	if (!state && fdc->intrq_ready_to_nready) {
		event_dequeue(&fdc->state_event);
		SET_INTRQ(fdc);
	}
}

void wd279x_tr00(void *sptr, _Bool state) {
	struct WD279X *fdc = sptr;
	if (fdc->tr00_state == state)
		return;
	fdc->tr00_state = state;
}

void wd279x_index_pulse(void *sptr, _Bool state) {
	struct WD279X *fdc = sptr;
	if (fdc->index_state == state)
		return;
	fdc->index_state = state;
	if (state) {
		fdc->index_holes_count++;
		if (fdc->intrq_index_pulse) {
			event_dequeue(&fdc->state_event);
			SET_INTRQ(fdc);
		}
	}
}

void wd279x_write_protect(void *sptr, _Bool state) {
	struct WD279X *fdc = sptr;
	if (fdc->write_protect_state == state)
		return;
	fdc->write_protect_state = state;
}

void wd279x_set_dden(struct WD279X *fdc, _Bool dden) {
	fdc->double_density = dden;
	DELEGATE_CALL(fdc->set_dden, dden);
}

void wd279x_update_connection(struct WD279X *fdc) {
	DELEGATE_CALL(fdc->set_dden, fdc->double_density);
	if (fdc->has_sso)
		DELEGATE_CALL(fdc->set_sso, fdc->side);
	DELEGATE_CALL(fdc->set_dirc, fdc->direction >= 0);
	DELEGATE_CALL(fdc->update_connection);
}

uint8_t wd279x_read(struct WD279X *fdc, uint16_t A) {
	uint8_t D;
	switch (A & 3) {
		default:
		case 0:
			if (!fdc->intrq_immediate)
				RESET_INTRQ(fdc);
			if (fdc->ready_state)
				fdc->status_register &= ~STATUS_NOT_READY;
			else
				fdc->status_register |= STATUS_NOT_READY;
			if (fdc->status_type1) {
				fdc->status_register &= ~(STATUS_TRACK_0|STATUS_INDEX_PULSE);
				if (fdc->tr00_state)
					fdc->status_register |= STATUS_TRACK_0;
				if (fdc->index_state)
					fdc->status_register |= STATUS_INDEX_PULSE;
			}
			D = fdc->status_register;
			break;
		case 1:
			D = fdc->track_register;
			break;
		case 2:
			D = fdc->sector_register;
			break;
		case 3:
			RESET_DRQ(fdc);
			D = fdc->data_register;
			break;
	}
	return D ^ fdc->invert_data;
}

void wd279x_write(struct WD279X *fdc, uint16_t A, uint8_t D) {
	D ^= fdc->invert_data;
	switch (A & 3) {
	default:
	case 0:
		fdc->command_register = D;
		// FORCE INTERRUPT
		if ((D & 0xf0) == 0xd0) {
			if (logging.debug_fdc & LOG_FDC_STATE) {
				wd279x_debug_state(fdc);
			}
			fdc->intrq_nready_to_ready = D & 1;
			fdc->intrq_ready_to_nready = D & 2;
			fdc->intrq_index_pulse = D & 4;
			// TODO: Data sheet wording implies that *only* 0xd0 can
			// clear this.  Needs testing...
			//
			// TODO: DarrenA posted a clip from the "Data Storage Products Handbook" that says this:
			//
			// Wait 8 micro sec (double density) or 16 micro sec
			// (single density) before issuing a new command after
			// issuing a forced interrupt (times double when clock
			// = 1 MHz).  Loading a new command sooner than this
			// will nullify the forced interrupt.
			fdc->intrq_immediate = D & 8;
			if (!(fdc->status_register & STATUS_BUSY)) {
				fdc->status_type1 = 1;
			}
			event_dequeue(&fdc->state_event);
			fdc->status_register &= ~(STATUS_BUSY);
			if (fdc->intrq_immediate)
				SET_INTRQ(fdc);
			return;
		}
		// Ignore any other command if busy
		if (fdc->status_register & STATUS_BUSY) {
			LOG_MOD_DEBUG_FDC(LOG_FDC_EVENTS, "wd279x", "command received while busy!\n");
			return;
		}
		if (!fdc->intrq_immediate)
			RESET_INTRQ(fdc);
		fdc->state = WD279X_state_accept_command;
		wd279x_state_machine(fdc);
		break;
	case 1:
		fdc->track_register = D;
		break;
	case 2:
		fdc->sector_register = D;
		break;
	case 3:
		RESET_DRQ(fdc);
		fdc->data_register = D;
		break;
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/** \brief WD279X state machine
 *
 * This is called from an event dispatch and from the write command function.
 */

static void wd279x_state_machine(void *sptr) {
	struct WD279X *fdc = (struct WD279X *)sptr;

	for (;;) {

		// Log new states if requested:
		if (logging.debug_fdc & LOG_FDC_STATE) {
			static enum WD279X_state last_state = WD279X_state_invalid;
			if (fdc->state != last_state) {
				wd279x_debug_state(fdc);
				last_state = fdc->state;
			}
		}

		switch (fdc->state) {

		case WD279X_state_accept_command:
			// 0xxxxxxx = RESTORE / SEEK / STEP / STEP-IN / STEP-OUT
			if ((fdc->command_register & 0x80) == 0x00) {
				fdc->status_type1 = 1;
				fdc->status_register |= STATUS_BUSY;
				fdc->status_register &= ~(STATUS_CRC_ERROR|STATUS_SEEK_ERROR);
				RESET_DRQ(fdc);
				fdc->step_delay = stepping_rate[fdc->command_register & 3];
				fdc->is_step_cmd = 0;
				if ((fdc->command_register & 0xe0) == 0x20) {
					fdc->is_step_cmd = 1;
				} else if ((fdc->command_register & 0xe0) == 0x40) {
					fdc->is_step_cmd = 1;
					SET_DIRECTION(fdc);
				} else if ((fdc->command_register & 0xe0) == 0x60) {
					fdc->is_step_cmd = 1;
					RESET_DIRECTION(fdc);
				}
				if (fdc->is_step_cmd) {
					if (fdc->command_register & 0x10) {
						SET_STATE(fdc, WD279X_state_type1_2);
						continue;
					}
					SET_STATE(fdc, WD279X_state_type1_3);
					continue;
				}
				if ((fdc->command_register & 0xf0) == 0x00) {
					fdc->track_register = 0xff;
					fdc->data_register = 0x00;
				}
				SET_STATE(fdc, WD279X_state_type1_1);
				continue;
			}

			// 10xxxxxx = READ/WRITE SECTOR
			if ((fdc->command_register & 0xc0) == 0x80) {
				fdc->status_type1 = 0;
				fdc->status_register |= STATUS_BUSY;
				fdc->status_register &= ~(STATUS_LOST_DATA|STATUS_RNF|(1<<5)|(1<<6));
				RESET_DRQ(fdc);
				if (!fdc->ready_state) {
					fdc->status_register &= ~(STATUS_BUSY);
					SET_INTRQ(fdc);
					return;
				}
				if (fdc->has_sso)
					SET_SIDE(fdc, fdc->command_register & 0x02);  // 'U'
				else
					SET_SIDE(fdc, fdc->command_register & 0x08);  // 'S'
				if (fdc->command_register & 0x04) {  // 'E' set
					NEXT_STATE(fdc, WD279X_state_type2_1, EVENT_MS(30));
					return;
				}
				SET_STATE(fdc, WD279X_state_type2_1);
				continue;
			}

			// 11000xx0 = READ ADDRESS
			// 11100xx0 = READ TRACK
			// 11110xx0 = WRITE TRACK
			if (((fdc->command_register & 0xf9) == 0xc0)
			    || ((fdc->command_register & 0xf9) == 0xe0)
			    || ((fdc->command_register & 0xf9) == 0xf0)) {
				fdc->status_type1 = 0;
				fdc->status_register |= STATUS_BUSY;
				fdc->status_register &= ~(STATUS_LOST_DATA|(1<<4)|(1<<5));
				if ((fdc->command_register & 0xf0) == 0xf0)
					RESET_DRQ(fdc);
				if (!fdc->ready_state) {
					fdc->status_register &= ~(STATUS_BUSY);
					SET_INTRQ(fdc);
					return;
				}
				if (fdc->has_sso)
					SET_SIDE(fdc, fdc->command_register & 0x02);  // 'U'
				else
					SET_SIDE(fdc, fdc->command_register & 0x08);  // 'S'
				if (fdc->command_register & 0x04) {  // 'E' set
					NEXT_STATE(fdc, WD279X_state_type3_1, EVENT_MS(30));
					return;
				}
				SET_STATE(fdc, WD279X_state_type3_1);
				continue;
			}
			LOG_MOD_WARN("wd279x", "CMD: unknown command %02x\n", fdc->command_register);
			return;


		case WD279X_state_type1_1:
			if (fdc->data_register == fdc->track_register) {
				SET_STATE(fdc, WD279X_state_verify_track_1);
				continue;
			}
			if (fdc->data_register > fdc->track_register)
				SET_DIRECTION(fdc);
			else
				RESET_DIRECTION(fdc);
			SET_STATE(fdc, WD279X_state_type1_2);
			continue;


		case WD279X_state_type1_2:
			fdc->track_register += fdc->direction;
			SET_STATE(fdc, WD279X_state_type1_3);
			continue;


		case WD279X_state_type1_3:
			if (fdc->tr00_state && fdc->direction == -1) {
				LOG_MOD_DEBUG_FDC(LOG_FDC_EVENTS, "wd279x", "TR00!\n");
				fdc->track_register = 0;
				// The WD279x flow chart implies this delay is
				// not incurred in this situation, but real
				// code fails without it.
				NEXT_STATE(fdc, WD279X_state_verify_track_1, EVENT_MS(fdc->step_delay));
				return;
			}
			DELEGATE_CALL(fdc->step);
			if (fdc->is_step_cmd) {
				NEXT_STATE(fdc, WD279X_state_verify_track_1, EVENT_MS(fdc->step_delay));
				return;
			}
			NEXT_STATE(fdc, WD279X_state_type1_1, EVENT_MS(fdc->step_delay));
			return;


		case WD279X_state_verify_track_1:
			if (!(fdc->command_register & 0x04)) {
				fdc->status_register &= ~(STATUS_BUSY);
				SET_INTRQ(fdc);
				return;
			}
			fdc->index_holes_count = 0;
			NEXT_STATE(fdc, WD279X_state_verify_track_2, DELEGATE_CALL(fdc->time_to_next_idam));
			return;


		case WD279X_state_verify_track_2: {
			const uint8_t *idam = DELEGATE_CALL(fdc->next_idam);
			if (fdc->index_holes_count >= 5) {
				LOG_MOD_DEBUG_FDC(LOG_FDC_EVENTS, "wd279x", "index_holes_count >= 5: seek error\n");
				fdc->status_register &= ~(STATUS_BUSY);
				fdc->status_register |= STATUS_SEEK_ERROR;
				SET_INTRQ(fdc);
				return;
			}
			if (idam == NULL) {
				LOG_MOD_DEBUG_FDC(LOG_FDC_EVENTS, "wd279x", "null IDAM: -> WD279X_state_verify_track_2\n");
				NEXT_STATE(fdc, WD279X_state_verify_track_2, DELEGATE_CALL(fdc->time_to_next_idam));
				return;
			}
			fdc->crc = CRC16_CCITT_RESET;
			if (IS_DOUBLE_DENSITY(fdc)) {
				fdc->crc = crc16_ccitt_byte(fdc->crc, 0xa1);
				fdc->crc = crc16_ccitt_byte(fdc->crc, 0xa1);
				fdc->crc = crc16_ccitt_byte(fdc->crc, 0xa1);
			}
			(void)wd279x_read_byte(fdc);  // Include IDAM in CRC
			if (fdc->track_register != wd279x_read_byte(fdc)) {
				LOG_MOD_DEBUG_FDC(LOG_FDC_EVENTS, "wd279x", "track_register != idam[1]: -> WD279X_state_verify_track_2\n");
				NEXT_STATE(fdc, WD279X_state_verify_track_2, DELEGATE_CALL(fdc->time_to_next_idam));
				return;
			}
			// Include rest of ID field - should result in computed CRC = 0
			for (int i = 0; i < 5; i++)
				(void)wd279x_read_byte(fdc);
			if (fdc->crc != 0) {
				LOG_MOD_DEBUG_FDC(LOG_FDC_EVENTS, "wd279x", "verify track %d CRC16 error: $%04x != 0\n", fdc->track_register, fdc->crc);
				fdc->status_register |= STATUS_CRC_ERROR;
				NEXT_STATE(fdc, WD279X_state_verify_track_2, DELEGATE_CALL(fdc->time_to_next_idam));
				return;
			}
			fdc->status_register &= ~(STATUS_CRC_ERROR|STATUS_BUSY);
			SET_INTRQ(fdc);
		} return;


		case WD279X_state_type2_1:
			if ((fdc->command_register & 0x20) && fdc->write_protect_state) {
				fdc->status_register &= ~(STATUS_BUSY);
				fdc->status_register |= STATUS_WRITE_PROTECT;
				SET_INTRQ(fdc);
				return;
			}
			fdc->index_holes_count = 0;
			NEXT_STATE(fdc, WD279X_state_type2_2, DELEGATE_CALL(fdc->time_to_next_idam));
			return;


		case WD279X_state_type2_2: {
			const uint8_t *idam = DELEGATE_CALL(fdc->next_idam);
			if (fdc->index_holes_count >= 5) {
				fdc->status_register &= ~(STATUS_BUSY);
				fdc->status_register |= STATUS_RNF;
				SET_INTRQ(fdc);
				return;
			}
			if (idam == NULL) {
				NEXT_STATE(fdc, WD279X_state_type2_2, DELEGATE_CALL(fdc->time_to_next_idam));
				return;
			}
			fdc->crc = CRC16_CCITT_RESET;
			if (IS_DOUBLE_DENSITY(fdc)) {
				fdc->crc = crc16_ccitt_byte(fdc->crc, 0xa1);
				fdc->crc = crc16_ccitt_byte(fdc->crc, 0xa1);
				fdc->crc = crc16_ccitt_byte(fdc->crc, 0xa1);
			}
			(void)wd279x_read_byte(fdc);  // Include IDAM in CRC
			if (fdc->track_register != wd279x_read_byte(fdc)) {
				NEXT_STATE(fdc, WD279X_state_type2_2, DELEGATE_CALL(fdc->time_to_next_idam));
				return;
			}
			if (fdc->side != (int)wd279x_read_byte(fdc)) {
				// No error if no SSO or 'C' not set
				if (fdc->has_sso || fdc->command_register & 0x02) {
					NEXT_STATE(fdc, WD279X_state_type2_2, DELEGATE_CALL(fdc->time_to_next_idam));
					return;
				}
			}
			if (fdc->sector_register != wd279x_read_byte(fdc)) {
				NEXT_STATE(fdc, WD279X_state_type2_2, DELEGATE_CALL(fdc->time_to_next_idam));
				return;
			}
			{
				int i = wd279x_read_byte(fdc);
				if (fdc->has_length_flag)
					fdc->bytes_left = sector_size[(fdc->command_register & 0x08)?1:0][i&3];
				else
					fdc->bytes_left = sector_size[1][i&3];
			}
			// Including CRC bytes should result in computed CRC = 0
			(void)wd279x_read_byte(fdc);
			(void)wd279x_read_byte(fdc);
			if (fdc->crc != 0) {
				fdc->status_register |= STATUS_CRC_ERROR;
				LOG_MOD_DEBUG_FDC(LOG_FDC_EVENTS, "wd279x", "type 2 tr %d se %d CRC16 error: $%04x != 0\n", fdc->track_register, fdc->sector_register, fdc->crc);
				NEXT_STATE(fdc, WD279X_state_type2_2, DELEGATE_CALL(fdc->time_to_next_idam));
				return;
			}

			if ((fdc->command_register & 0x20) == 0) {
				int bytes_to_scan, j;
				if (IS_SINGLE_DENSITY(fdc))
					bytes_to_scan = 30;
				else
					bytes_to_scan = 43;
				j = 0;
				fdc->dam = 0;
				do {
					fdc->crc = CRC16_CCITT_RESET;
					if (IS_DOUBLE_DENSITY(fdc)) {
						fdc->crc = crc16_ccitt_byte(fdc->crc, 0xa1);
						fdc->crc = crc16_ccitt_byte(fdc->crc, 0xa1);
						fdc->crc = crc16_ccitt_byte(fdc->crc, 0xa1);
					}
					int tmp = wd279x_read_byte(fdc);
					if (tmp == 0xfb || tmp == 0xf8)
						fdc->dam = tmp;
					j++;
				} while (j < bytes_to_scan && fdc->dam == 0);
				if (fdc->dam == 0) {
					NEXT_STATE(fdc, WD279X_state_type2_2, DELEGATE_CALL(fdc->time_to_next_byte));
					return;
				}
				NEXT_STATE(fdc, WD279X_state_read_sector_1, DELEGATE_CALL(fdc->time_to_next_byte));
				return;
			}
			DELEGATE_CALL(fdc->skip);
			DELEGATE_CALL(fdc->skip);
			NEXT_STATE(fdc, WD279X_state_write_sector_1, DELEGATE_CALL(fdc->time_to_next_byte));
		} return;


		case WD279X_state_read_sector_1:
			LOG_MOD_DEBUG_FDC(LOG_FDC_EVENTS, "wd279x", "reading %d-byte sector (Tr %d, Se %d)\n", fdc->bytes_left, fdc->track_register, fdc->sector_register);
			if (logging.debug_fdc & LOG_FDC_DATA)
				log_open_hexdump(&fdc->log_rsec_hex, "[wd279x] rd-sector");
			fdc->status_register |= ((~fdc->dam & 1) << 5);
			fdc->data_register = wd279x_read_byte(fdc);
			if (logging.debug_fdc & LOG_FDC_DATA)
				log_hexdump_byte(fdc->log_rsec_hex, fdc->data_register);
			fdc->bytes_left--;
			SET_DRQ(fdc);
			NEXT_STATE(fdc, WD279X_state_read_sector_2, DELEGATE_CALL(fdc->time_to_next_byte));
			return;


		case WD279X_state_read_sector_2:
			if (fdc->status_register & STATUS_DRQ) {
				fdc->status_register |= STATUS_LOST_DATA;
				if (logging.debug_fdc & LOG_FDC_DATA)
					log_hexdump_flag(fdc->log_rsec_hex);
				//RESET_DRQ(fdc);  // XXX
			}
			if (fdc->bytes_left > 0) {
				fdc->data_register = wd279x_read_byte(fdc);
				if (logging.debug_fdc & LOG_FDC_DATA)
					log_hexdump_byte(fdc->log_rsec_hex, fdc->data_register);
				fdc->bytes_left--;
				SET_DRQ(fdc);
				NEXT_STATE(fdc, WD279X_state_read_sector_2, DELEGATE_CALL(fdc->time_to_next_byte));
				return;
			}
			log_close(&fdc->log_rsec_hex);
			// Including CRC bytes should result in computed CRC = 0
			(void)wd279x_read_byte(fdc);
			(void)wd279x_read_byte(fdc);
			NEXT_STATE(fdc, WD279X_state_read_sector_3, DELEGATE_CALL(fdc->time_to_next_byte));
			return;


		case WD279X_state_read_sector_3:
			if (fdc->crc != 0) {
				LOG_MOD_DEBUG_FDC(LOG_FDC_EVENTS, "wd279x", "read sector data tr %d se %d CRC16 error: $%04x != 0\n", fdc->track_register, fdc->sector_register, fdc->crc);
				fdc->status_register |= STATUS_CRC_ERROR;
			}
			if (fdc->command_register & 0x10) {
				// XXX what happens on overflow here?
				fdc->sector_register++;
				SET_STATE(fdc, WD279X_state_type2_1);
				continue;
			}
			fdc->status_register &= ~(STATUS_BUSY);
			SET_INTRQ(fdc);
			return;


		case WD279X_state_write_sector_1:
			SET_DRQ(fdc);
			for (int i = 0; i < 8; i++)
				DELEGATE_CALL(fdc->skip);
			NEXT_STATE(fdc, WD279X_state_write_sector_2, DELEGATE_CALL(fdc->time_to_next_byte));
			return;


		case WD279X_state_write_sector_2:
			if (fdc->status_register & STATUS_DRQ) {
				fdc->status_register &= ~(STATUS_BUSY);
				RESET_DRQ(fdc);  // XXX
				fdc->status_register |= STATUS_LOST_DATA;
				SET_INTRQ(fdc);
				return;
			}
			DELEGATE_CALL(fdc->skip);
			NEXT_STATE(fdc, WD279X_state_write_sector_3, DELEGATE_CALL(fdc->time_to_next_byte));
			return;


		case WD279X_state_write_sector_3:
			if (logging.debug_fdc & LOG_FDC_DATA)
				log_open_hexdump(&fdc->log_wsec_hex, "[wd279x] wr-sector");
			if (IS_DOUBLE_DENSITY(fdc)) {
				for (int i = 0; i < 11; i++)
					DELEGATE_CALL(fdc->skip);
				for (int i = 0; i < 12; i++)
					wd279x_write_byte(fdc, 0);
				NEXT_STATE(fdc, WD279X_state_write_sector_4, DELEGATE_CALL(fdc->time_to_next_byte));
				return;
			}
			for (int i = 0; i < 6; i++)
				wd279x_write_byte(fdc, 0);
			NEXT_STATE(fdc, WD279X_state_write_sector_4, DELEGATE_CALL(fdc->time_to_next_byte));
			return;


		case WD279X_state_write_sector_4:
			fdc->crc = CRC16_CCITT_RESET;
			if (IS_DOUBLE_DENSITY(fdc)) {
				wd279x_write_byte(fdc, 0xa1);
				wd279x_write_byte(fdc, 0xa1);
				wd279x_write_byte(fdc, 0xa1);
			}
			if (fdc->command_register & 1)
				wd279x_write_byte(fdc, 0xf8);
			else
				wd279x_write_byte(fdc, 0xfb);
			NEXT_STATE(fdc, WD279X_state_write_sector_5, DELEGATE_CALL(fdc->time_to_next_byte));
			return;


		case WD279X_state_write_sector_5: {
			uint8_t data = fdc->data_register;
			if (fdc->status_register & STATUS_DRQ) {
				data = 0;
				fdc->status_register |= STATUS_LOST_DATA;
				if (logging.debug_fdc & LOG_FDC_DATA)
					log_hexdump_flag(fdc->log_wsec_hex);
				RESET_DRQ(fdc);  // XXX
			}
			if (logging.debug_fdc & LOG_FDC_DATA)
				log_hexdump_byte(fdc->log_wsec_hex, data);
			wd279x_write_byte(fdc, data);
			fdc->bytes_left--;
			if (fdc->bytes_left > 0) {
				SET_DRQ(fdc);
				NEXT_STATE(fdc, WD279X_state_write_sector_5, DELEGATE_CALL(fdc->time_to_next_byte));
				return;
			}
			log_close(&fdc->log_wsec_hex);
			VDRIVE_WRITE_CRC16(fdc);
			NEXT_STATE(fdc, WD279X_state_write_sector_6, DELEGATE_CALL(fdc->time_to_next_byte) + EVENT_US(20));
		} return;


		case WD279X_state_write_sector_6:
			wd279x_write_byte(fdc, 0xfe);
			if (fdc->command_register & 0x10) {
				// XXX what happens on overflow here?
				fdc->sector_register++;
				SET_STATE(fdc, WD279X_state_type2_1);
				continue;
			}
			fdc->status_register &= ~(STATUS_BUSY);
			SET_INTRQ(fdc);
			return;


		case WD279X_state_type3_1:
			switch (fdc->command_register & 0xf0) {
			case 0xc0:
				fdc->index_holes_count = 0;
				NEXT_STATE(fdc, WD279X_state_read_address_1, DELEGATE_CALL(fdc->time_to_next_idam));
				return;
			case 0xe0:
				LOG_MOD_WARN("wd279x", "CMD: read track not implemented\n");
				SET_INTRQ(fdc);
				break;
			case 0xf0:
				SET_STATE(fdc, WD279X_state_write_track_1);
				continue;
			default:
				break;
			}
			return;


		case WD279X_state_read_address_1: {
			const uint8_t *idam = DELEGATE_CALL(fdc->next_idam);
			if (fdc->index_holes_count >= 6) {
				fdc->status_register &= ~(STATUS_BUSY);
				fdc->status_register |= STATUS_RNF;
				SET_INTRQ(fdc);
				return;
			}
			if (idam == NULL) {
				NEXT_STATE(fdc, WD279X_state_read_address_1, DELEGATE_CALL(fdc->time_to_next_idam));
				return;
			}
			fdc->crc = CRC16_CCITT_RESET;
			if (IS_DOUBLE_DENSITY(fdc)) {
				fdc->crc = crc16_ccitt_byte(fdc->crc, 0xa1);
				fdc->crc = crc16_ccitt_byte(fdc->crc, 0xa1);
				fdc->crc = crc16_ccitt_byte(fdc->crc, 0xa1);
			}
			(void)wd279x_read_byte(fdc);
			NEXT_STATE(fdc, WD279X_state_read_address_2, DELEGATE_CALL(fdc->time_to_next_byte));
		} return;


		case WD279X_state_read_address_2:
			fdc->bytes_left = 5;
			fdc->data_register = wd279x_read_byte(fdc);
			// At end of command, this is transferred to the sector register:
			fdc->track_register_tmp = fdc->data_register;
			SET_DRQ(fdc);
			NEXT_STATE(fdc, WD279X_state_read_address_3, DELEGATE_CALL(fdc->time_to_next_byte));
			return;


		case WD279X_state_read_address_3:
			// Lost data not mentioned in data sheet, so not
			// checking for now
			if (fdc->bytes_left > 0) {
				fdc->data_register = wd279x_read_byte(fdc);
				fdc->bytes_left--;
				SET_DRQ(fdc);
				NEXT_STATE(fdc, WD279X_state_read_address_3, DELEGATE_CALL(fdc->time_to_next_byte));
				return;
			}
			fdc->sector_register = fdc->track_register_tmp;
			if (fdc->crc != 0) {
				fdc->status_register |= STATUS_CRC_ERROR;
			}
			fdc->status_register &= ~(STATUS_BUSY);
			SET_INTRQ(fdc);
			return;


		case WD279X_state_write_track_1:
			if (fdc->write_protect_state) {
				fdc->status_register &= ~(STATUS_BUSY);
				fdc->status_register |= STATUS_WRITE_PROTECT;
				SET_INTRQ(fdc);
				return;
			}
			SET_DRQ(fdc);
			// Data sheet says 3 byte times, but CoCo NitrOS9 fails
			// unless I set this delay higher.
			NEXT_STATE(fdc, WD279X_state_write_track_2, 6 * W_BYTE_TIME);
			return;


		case WD279X_state_write_track_2:
			if (fdc->status_register & STATUS_DRQ) {
				RESET_DRQ(fdc);  // XXX
				fdc->status_register |= STATUS_LOST_DATA;
				fdc->status_register &= ~(STATUS_BUSY);
				SET_INTRQ(fdc);
				return;
			}
			fdc->index_holes_count = 0;
			NEXT_STATE(fdc, WD279X_state_write_track_2b, DELEGATE_CALL(fdc->time_to_next_idam));
			return;


		case WD279X_state_write_track_2b:
			if (fdc->index_holes_count == 0) {
				NEXT_STATE(fdc, WD279X_state_write_track_2b, DELEGATE_CALL(fdc->time_to_next_idam));
				return;
			}
			fdc->index_holes_count = 0;
			if (logging.debug_fdc & LOG_FDC_DATA)
				log_open_hexdump(&fdc->log_wtrk_hex, "[wd279x] wr-track");
			SET_STATE(fdc, WD279X_state_write_track_3);
			continue;


		case WD279X_state_write_track_3: {
			uint8_t data = fdc->data_register;
			if (fdc->index_holes_count > 0) {
				if (logging.debug_fdc & LOG_FDC_DATA)
					log_close(&fdc->log_wtrk_hex);
				RESET_DRQ(fdc);  // XXX
				fdc->status_register &= ~(STATUS_BUSY);
				SET_INTRQ(fdc);
				return;
			}
			if (logging.debug_fdc & LOG_FDC_DATA)
				log_hexdump_byte(fdc->log_wtrk_hex, fdc->data_register);
			if (fdc->status_register & STATUS_DRQ) {
				data = 0;
				fdc->status_register |= STATUS_LOST_DATA;
				if (logging.debug_fdc & LOG_FDC_DATA)
					log_hexdump_flag(fdc->log_wtrk_hex);
			}
			SET_DRQ(fdc);
			if (IS_SINGLE_DENSITY(fdc)) {
				// Single density
				if (data == 0xf5 || data == 0xf6) {
					LOG_MOD_DEBUG_FDC(LOG_FDC_EVENTS, "wd279x", "illegal value in single-density track write: %02x\n", data);
				}
				if (data == 0xf7) {
					VDRIVE_WRITE_CRC16(fdc);
					NEXT_STATE(fdc, WD279X_state_write_track_3, DELEGATE_CALL(fdc->time_to_next_byte));
					return;
				}
				if (data >= 0xf8 && data <= 0xfb) {
					fdc->crc = CRC16_CCITT_RESET;
					wd279x_write_byte(fdc, data);
					NEXT_STATE(fdc, WD279X_state_write_track_3, DELEGATE_CALL(fdc->time_to_next_byte));
					return;
				}
				if (data == 0xfe) {
					fdc->crc = CRC16_CCITT_RESET;
					DELEGATE_CALL(fdc->write_idam);
					fdc->crc = crc16_ccitt_byte(fdc->crc, 0xfe);
					NEXT_STATE(fdc, WD279X_state_write_track_3, DELEGATE_CALL(fdc->time_to_next_byte));
					return;
				}
				wd279x_write_byte(fdc, data);
				NEXT_STATE(fdc, WD279X_state_write_track_3, DELEGATE_CALL(fdc->time_to_next_byte));
				return;
			}
			// Double density
			if (data == 0xf7) {
				VDRIVE_WRITE_CRC16(fdc);
				NEXT_STATE(fdc, WD279X_state_write_track_3, DELEGATE_CALL(fdc->time_to_next_byte));
				return;
			}
			if (data == 0xfe) {
				DELEGATE_CALL(fdc->write_idam);
				fdc->crc = crc16_ccitt_byte(fdc->crc, 0xfe);
				NEXT_STATE(fdc, WD279X_state_write_track_3, DELEGATE_CALL(fdc->time_to_next_byte));
				return;
			}
			if (data == 0xf5) {
				fdc->crc = CRC16_CCITT_RESET;
				fdc->crc = crc16_ccitt_byte(fdc->crc, 0xa1);
				fdc->crc = crc16_ccitt_byte(fdc->crc, 0xa1);
				wd279x_write_byte(fdc, 0xa1);
				NEXT_STATE(fdc, WD279X_state_write_track_3, DELEGATE_CALL(fdc->time_to_next_byte));
				return;
			}
			if (data == 0xf6) {
				data = 0xc2;
			}
			wd279x_write_byte(fdc, data);
			NEXT_STATE(fdc, WD279X_state_write_track_3, DELEGATE_CALL(fdc->time_to_next_byte));
		} return;

		default:
			return;

		}
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Debugging

static char const * const debug_state_name[] = {
	"accept_command",
	"type1_1",
	"type1_2",
	"type1_3",
	"verify_track_1",
	"verify_track_2",
	"type2_1",
	"type2_2",
	"read_sector_1",
	"read_sector_2",
	"read_sector_3",
	"write_sector_1",
	"write_sector_2",
	"write_sector_3",
	"write_sector_4",
	"write_sector_5",
	"write_sector_6",
	"type3_1",
	"read_address_1",
	"read_address_2",
	"read_address_3",
	"write_track_1",
	"write_track_2",
	"write_track_2b",
	"write_track_3",
};

static char const * const debug_command[] = {
	"restore",
	"seek",
	"step",
	"step",
	"step-in",
	"step-in",
	"step-out",
	"step-out",
	"read-sector",
	"read-sector",
	"write-sector",
	"write-sector",
	"read-address",
	"force-interrupt",
	"read-track N/A",
	"write-track",
};

static void wd279x_debug_state(const struct WD279X *fdc) {
	assert(fdc != NULL);
	assert((unsigned)fdc->state < WD279X_state_invalid);
	unsigned level = logging.debug_fdc & LOG_FDC_STATE;
	if (level == 0)
		return;
	_Bool forced_interrupt = ((fdc->command_register & 0xf0) == 0xd0);
	if (fdc->state == WD279X_state_accept_command || forced_interrupt) {
		// command (incl. forced interrupt)
		unsigned type = ((fdc->command_register) >> 4) & 15;
		LOG_MOD_PRINT("wd279x", "CR=%02x ST=%02x TR=%02x SR=%02x DR=%02x state=%s [%s]\n",
			      fdc->command_register, fdc->status_register,
			      fdc->track_register, fdc->sector_register, fdc->data_register,
			      debug_state_name[fdc->state], debug_command[type]);
	} else if (level >= 2) {
		// any other state
		LOG_MOD_PRINT("wd279x", "CR=%02x ST=%02x TR=%02x SR=%02x DR=%02x state=%s\n",
			      fdc->command_register, fdc->status_register,
			      fdc->track_register, fdc->sector_register, fdc->data_register,
			      debug_state_name[fdc->state]);
	}
}
