/*

Tape support

Copyright 2003-2020 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

See COPYING.GPL for redistribution conditions.

*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"
#include "delegate.h"
#include "xalloc.h"

#include "breakpoint.h"
#include "crc16.h"
#include "events.h"
#include "fs.h"
#include "keyboard.h"
#include "logging.h"
#include "machine.h"
#include "mc6809.h"
#include "snapshot.h"
#include "sound.h"
#include "tape.h"
#include "ui.h"
#include "xroar.h"

struct tape_interface_private {
	struct tape_interface public;

	_Bool is_dragon;
	struct machine *machine;
	struct ui_interface *ui;
	struct keyboard_interface *keyboard_interface;
	struct MC6809 *cpu;

	_Bool tape_fast;
	_Bool tape_pad_auto;
	_Bool tape_rewrite;
	_Bool short_leader;

	int in_pulse;
	int in_pulse_width;
	// When accelerating operations, accumulated number of simulated CPU cycles
	int cpuskip;

	int ao_rate;

	uint8_t last_tape_output;
	_Bool motor;

	// Whether a sync byte has been detected yet.  If false, we're still
	// reading the leader.
	_Bool rewrite_have_sync;
	// Amount of leader bytes to write when the next sync byte is detected
	int rewrite_leader_count;
	// Track number of bits written and keep things byte-aligned
	int rewrite_bit_count;
	// Track the fractional sample part when rewriting to tape
	int rewrite_sremain;
	// Was the last thing rewritten silence?  If so, any subsequent motor
	// events won't trigger a trailer byte.
	_Bool rewrite_silence;

	struct event waggle_event;
	struct event flush_event;
};

static void waggle_bit(void *);
static void flush_output(void *);

static void tape_desync(struct tape_interface_private *tip, int leader);
static void rewrite_sync(void *sptr);
static void rewrite_bitin(void *sptr);
static void rewrite_tape_on(void *sptr);
static void rewrite_end_of_block(void *sptr);

static void set_breakpoints(struct tape_interface_private *tip);

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

// Special case autorun instructions based on filename block size and CRC16.

struct tape_file_autorun {
	const char *name;
	int size;
	uint16_t crc;
	const char *run;
};

static struct tape_file_autorun autorun_special[] = {
	{
	  .name = "Electronic Author",
	  .size = 15, .crc = 0x8866,
	  .run = "\\eCLEAR20\\r\\0CLOADM\\r",
	},
	{
	  .name = "Galacticans",
	  .size = 15, .crc = 0xd39b,
	  .run = "\\ePCLEAR1\\r\\0CLEAR200,7777\\r\\0CLOADM\\r",
	},
	{
	  .name = "Lucifer's Kingdom",
	  .size = 15, .crc = 0x7f34,
	  .run = "\\eCLEAR1,32767:CLOADM\\r",
	},
	{
	  .name = "North-Sea Action",
	  .size = 15, .crc = 0x9c2b,
	  .run = "\\eCLEAR20\\r\\0CLOADM\\r\\0EXEC\\r",
	},
	{
	  .name = "Speak Up!",
	  .size = 15, .crc = 0x7bff,
	  .run = "\\eCLEAR200,25448\\r\\0CLOADM\\r\\0EXEC\\r",
	},
	{
	  .name = "Spy Against Spy",
	  .size = 15, .crc = 0x48a0,
	  .run = "\\eCLEAR20:CLOADM\\r",
	},
	{
	  .name = "Tanglewood",
	  .size = 115, .crc = 0x7e5e,
	  .run = "\\eCLEAR10\\r\\0CLOADM\\r",
	},
	{
	  .name = "Ultrapede",
	  .size = 15, .crc = 0x337a,
	  .run = "\\eCLOADM\\r",
	},
	{
	  .name = "Utopia",
	  .size = 15, .crc = 0xeb14,
	  .run = "\\eCLEAR10:CLOADM\\r\\0EXEC\\r",
	},
};

/**************************************************************************/

struct tape_interface *tape_interface_new(struct ui_interface *ui) {
	struct tape_interface_private *tip = xmalloc(sizeof(*tip));
	*tip = (struct tape_interface_private){0};
	struct tape_interface *ti = &tip->public;

	tip->ui = ui;
	tip->in_pulse = -1;
	tip->ao_rate = 9600;
	tip->rewrite_leader_count = 256;
	tip->rewrite_silence = 1;

	tape_interface_disconnect_machine(ti);

	event_init(&tip->waggle_event, DELEGATE_AS0(void, waggle_bit, tip));
	event_init(&tip->flush_event, DELEGATE_AS0(void, flush_output, tip));

	return &tip->public;
}

void tape_interface_free(struct tape_interface *ti) {
	if (!ti)
		return;
	struct tape_interface_private *tip = (struct tape_interface_private *)ti;
	tape_close_reading(ti);
	tape_close_writing(ti);
	tape_reset(ti);
	free(tip);
}

// Connecting a machine allows breakpoints to be set on that machine to
// implement fast loading & tape rewriting.  It also allows driving the
// keyboard to type automatic load commands.  This should all probably be
// abstracted out.

void tape_interface_connect_machine(struct tape_interface *ti, struct machine *m) {
	struct tape_interface_private *tip = (struct tape_interface_private *)ti;
	switch (m->config->architecture) {
	case ARCH_DRAGON32:
	case ARCH_DRAGON64:
		tip->is_dragon = 1;
		break;
	default:
		tip->is_dragon = 0;
		 break;
	}
	tip->machine = m;
	tip->keyboard_interface = m->get_interface(m, "keyboard");
	tip->cpu = m->get_component(m, "CPU0");
	ti->update_audio = DELEGATE_AS1(void, float, m->get_interface(m, "tape-update-audio"), m);
	DELEGATE_CALL1(ti->update_audio, 0.5);
}

void tape_interface_disconnect_machine(struct tape_interface *ti) {
	struct tape_interface_private *tip = (struct tape_interface_private *)ti;
	tip->machine = NULL;
	tip->keyboard_interface = NULL;
	tip->cpu = NULL;
	ti->update_audio = DELEGATE_DEFAULT1(void, float);
}

int tape_seek(struct tape *t, long offset, int whence) {
	struct tape_interface *ti = t->tape_interface;
	struct tape_interface_private *tip = (struct tape_interface_private *)ti;
	int r = t->module->seek(t, offset, whence);
	tape_update_motor(ti, tip->motor);
	// If seeking to beginning of tape, ensure any fake leader etc.
	// is set up properly.
	if (r >= 0 && t->offset == 0) {
		tape_desync(tip, 256);
	}
	return r;
}

static int tape_pulse_in(struct tape *t, int *pulse_width) {
	if (!t) return -1;
	return t->module->pulse_in(t, pulse_width);
}

static int tape_bit_in(struct tape *t) {
	if (!t) return -1;
	int phase, pulse1_width, cycle_width;
	if (tape_pulse_in(t, &pulse1_width) == -1)
		return -1;
	do {
		int pulse0_width = pulse1_width;
		if ((phase = tape_pulse_in(t, &pulse1_width)) == -1)
			return -1;
		cycle_width = pulse0_width + pulse1_width;
	} while (!phase || (cycle_width < (TAPE_BIT1_LENGTH / 2))
	         || (cycle_width > (TAPE_BIT0_LENGTH * 2)));
	if (cycle_width < TAPE_AV_BIT_LENGTH) {
		return 1;
	}
	return 0;
}

static int tape_byte_in(struct tape *t) {
	int byte = 0;
	for (int i = 8; i; i--) {
		int bit = tape_bit_in(t);
		if (bit == -1) return -1;
		byte = (byte >> 1) | (bit ? 0x80 : 0);
	}
	return byte;
}

// Precalculated reasonably high resolution half sine wave.  Slightly offset so
// that there's always a zero crossing.

static const double half_sin[64] = {
	0.074, 0.122, 0.171, 0.219, 0.267, 0.314, 0.360, 0.405,
	0.450, 0.493, 0.535, 0.576, 0.615, 0.653, 0.690, 0.724,
	0.757, 0.788, 0.818, 0.845, 0.870, 0.893, 0.914, 0.933,
	0.950, 0.964, 0.976, 0.985, 0.992, 0.997, 1.000, 1.000,
	0.997, 0.992, 0.985, 0.976, 0.964, 0.950, 0.933, 0.914,
	0.893, 0.870, 0.845, 0.818, 0.788, 0.757, 0.724, 0.690,
	0.653, 0.615, 0.576, 0.535, 0.493, 0.450, 0.405, 0.360,
	0.314, 0.267, 0.219, 0.171, 0.122, 0.074, 0.025, -0.025
};

// Silence is close to zero, but held just over for half the duration, then
// just under for the rest.  This way, the first bit of any subsequent leader
// is recognised in its entirety if processed further.

static void write_silence(struct tape *t, int duration) {
	tape_sample_out(t, 0x81, duration / 2);
	tape_sample_out(t, 0x7f, duration / 2);
}

static void write_pulse(struct tape *t, int pulse_width, double scale) {
	struct tape_interface *ti = t->tape_interface;
	struct tape_interface_private *tip = (struct tape_interface_private *)ti;
	for (int i = 0; i < 64; i++) {
		unsigned sr = tip->rewrite_sremain + pulse_width;
		unsigned nticks = sr / 64;
		tip->rewrite_sremain = sr - (nticks * 64);
		int sample = (half_sin[i] * scale * 128.) + 128;
		tape_sample_out(t, sample, nticks);
	}
}

static void tape_bit_out(struct tape *t, int bit) {
	if (!t) return;
	struct tape_interface *ti = t->tape_interface;
	struct tape_interface_private *tip = (struct tape_interface_private *)ti;
	// Magic numbers?  These are the pulse widths (in SAM cycles) that fall
	// in the middle of what is recognised by the ROM read routines, and as
	// such should prove to be the most robust.
	if (bit) {
		write_pulse(t, 3984, 0.7855);
		write_pulse(t, 2992, -0.7855);
	} else {
		write_pulse(t, 6896, 0.7855);
		write_pulse(t, 5904, -0.7855);
	}
	tip->rewrite_bit_count = (tip->rewrite_bit_count + 1) & 7;
	tip->last_tape_output = 0;
	tip->rewrite_silence = 0;
}

static void tape_byte_out(struct tape *t, int byte) {
	if (!t) return;
	for (int i = 8; i; i--) {
		tape_bit_out(t, byte & 1);
		byte >>= 1;
	}
}

/**************************************************************************/

static int block_sync(struct tape *tape) {
	int byte = 0;
	for (;;) {
		int bit = tape_bit_in(tape);
		if (bit == -1) return -1;
		byte = (byte >> 1) | (bit ? 0x80 : 0);
		if (byte == 0x3c) {
			return 0;
		}
	}
}

/* read next block.  returns -1 on EOF/error, block type on success. */
/* *sum will be computed sum - checksum byte, which should be 0 */
static int block_in(struct tape *t, uint8_t *sum, long *offset, uint8_t *block) {
	int type, size, sumbyte;

	if (block_sync(t) == -1) return -1;
	if (offset) {
		*offset = tape_tell(t);
	}
	if ((type = tape_byte_in(t)) == -1) return -1;
	if (block) block[0] = type;
	if ((size = tape_byte_in(t)) == -1) return -1;
	if (block) block[1] = size;
	if (sum) *sum = type + size;
	for (int i = 0; i < size; i++) {
		int data;
		if ((data = tape_byte_in(t)) == -1) return -1;
		if (block) block[2+i] = data;
		if (sum) *sum += data;
	}
	if ((sumbyte = tape_byte_in(t)) == -1) return -1;
	if (block) block[2+size] = sumbyte;
	if (sum) *sum -= sumbyte;
	return type;
}

struct tape_file *tape_file_next(struct tape *t, int skip_bad) {
	struct tape_file *f;
	uint8_t block[258];
	uint8_t sum;
	long offset;

	for (;;) {
		long start = tape_tell(t);
		int type = block_in(t, &sum, &offset, block);
		if (type == -1)
			return NULL;
		/* If skip_bad set, this aggressively scans for valid header
		   blocks by seeking back to just after the last sync byte: */
		if (skip_bad && (type != 0 || sum != 0 || block[1] < 15)) {
			tape_seek(t, offset, SEEK_SET);
			continue;
		}
		if (type != 0 || block[1] < 15)
			continue;
		f = xmalloc(sizeof(*f));
		f->offset = start;
		memcpy(f->name, &block[2], 8);
		int i = 8;
		do {
			f->name[i--] = 0;
		} while (i >= 0 && f->name[i] == ' ');
		f->type = block[10];
		f->ascii_flag = block[11] ? 1 : 0;
		f->gap_flag = block[12] ? 1 : 0;
		f->start_address = (block[13] << 8) | block[14];
		f->load_address = (block[15] << 8) | block[16];
		f->checksum_error = sum ? 1 : 0;
		f->fnblock_size = block[1];
		f->fnblock_crc = crc16_block(CRC16_RESET, block + 2, f->fnblock_size);
		return f;
	}
}

void tape_seek_to_file(struct tape *t, struct tape_file const *f) {
	if (!t || !f) return;
	tape_seek(t, f->offset, SEEK_SET);
}

/**************************************************************************/

struct tape *tape_new(struct tape_interface *ti) {
	struct tape *new = xmalloc(sizeof(*new));
	*new = (struct tape){0};
	new->tape_interface = ti;
	return new;
}

void tape_free(struct tape *t) {
	free(t);
}

/**************************************************************************/

void tape_reset(struct tape_interface *ti) {
	struct tape_interface_private *tip = (struct tape_interface_private *)ti;
	tape_close_writing(ti);
	tip->motor = 0;
	event_dequeue(&tip->waggle_event);
}

void tape_set_ao_rate(struct tape_interface *ti, int rate) {
	struct tape_interface_private *tip = (struct tape_interface_private *)ti;
	if (rate > 0)
		tip->ao_rate = rate;
	else
		tip->ao_rate = 9600;
}

int tape_open_reading(struct tape_interface *ti, const char *filename) {
	struct tape_interface_private *tip = (struct tape_interface_private *)ti;
	tape_close_reading(ti);
	int type = xroar_filetype_by_ext(filename);
	tip->short_leader = 0;
	switch (type) {
	case FILETYPE_CAS:
		if ((ti->tape_input = tape_cas_open(ti, filename, "rb")) == NULL) {
			LOG_WARN("Failed to open '%s'\n", filename);
			return -1;
		}
		if (tip->tape_pad_auto) {
			if (tip->is_dragon && ti->tape_input->leader_count < 114)
				tip->short_leader = 1;
			if (!tip->is_dragon && ti->tape_input->leader_count < 130)
				tip->short_leader = 1;
			// leader padding needs breakpoints set
			set_breakpoints(tip);
		}
		break;
	case FILETYPE_ASC:
		if ((ti->tape_input = tape_asc_open(ti, filename, "rb")) == NULL) {
			LOG_WARN("Failed to open '%s'\n", filename);
			return -1;
		}
		break;
	default:
#ifdef HAVE_SNDFILE
		if ((ti->tape_input = tape_sndfile_open(ti, filename, "rb", -1)) == NULL) {
			LOG_WARN("Failed to open '%s'\n", filename);
			return -1;
		}
		break;
#else
		LOG_WARN("Failed to open '%s'\n", filename);
		return -1;
#endif
	}
	if (ti->tape_input->module->set_panning)
		ti->tape_input->module->set_panning(ti->tape_input, xroar_cfg.tape_pan);

	tape_desync(tip, 256);
	tape_update_motor(ti, tip->motor);
	LOG_DEBUG(1, "Tape: Attached '%s' for reading\n", filename);
	return 0;
}

void tape_close_reading(struct tape_interface *ti) {
	if (!ti->tape_input)
		return;
	tape_close(ti->tape_input);
	ti->tape_input = NULL;
}

int tape_open_writing(struct tape_interface *ti, const char *filename) {
	struct tape_interface_private *tip = (struct tape_interface_private *)ti;
	tape_close_writing(ti);
	int type = xroar_filetype_by_ext(filename);
	switch (type) {
	case FILETYPE_CAS:
	case FILETYPE_ASC:
		if ((ti->tape_output = tape_cas_open(ti, filename, "wb")) == NULL) {
			LOG_WARN("Failed to open '%s' for writing.", filename);
			return -1;
		}
		break;
	default:
#ifdef HAVE_SNDFILE
		if ((ti->tape_output = tape_sndfile_open(ti, filename, "wb", tip->ao_rate)) == NULL) {
			LOG_WARN("Failed to open '%s' for writing.", filename);
			return -1;
		}
#else
		LOG_WARN("Failed to open '%s' for writing.\n", filename);
		return -1;
#endif
		break;
	}

	tape_update_motor(ti, tip->motor);
	tip->rewrite_bit_count = 0;
	tip->rewrite_silence = 1;
	LOG_DEBUG(1, "Tape: Attached '%s' for writing.\n", filename);
	return 0;
}

void tape_close_writing(struct tape_interface *ti) {
	struct tape_interface_private *tip = (struct tape_interface_private *)ti;
	if (!ti->tape_output)
		return;
	if (tip->tape_rewrite) {
		// Writes a trailing byte where appropriate
		tape_desync(tip, 1);
		// Ensure the tape ends with a short duration of silence
		if (!tip->rewrite_silence) {
			write_silence(ti->tape_output, EVENT_MS(200));
			tip->rewrite_silence = 1;
		}
	}
	if (ti->tape_output) {
		event_dequeue(&tip->flush_event);
		tape_update_output(ti, tip->last_tape_output);
		tape_close(ti->tape_output);
	}
	ti->tape_output = NULL;
}

// Close any currently-open tape file, open a new one and read the first
// bufferful of data.  Tries to guess the filetype.  Returns -1 on error, 0 for
// a BASIC program, 1 for data and 2 for M/C.

int tape_autorun(struct tape_interface *ti, const char *filename) {
	struct tape_interface_private *tip = (struct tape_interface_private *)ti;
	if (filename == NULL)
		return -1;
	keyboard_queue_basic(tip->keyboard_interface, NULL);
	if (tape_open_reading(ti, filename) == -1)
		return -1;
	struct tape_file *f = tape_file_next(ti->tape_input, 0);
	tape_rewind(ti->tape_input);
	if (!f) {
		return -1;
	}

	int type = f->type;
	_Bool done = 0;

	if (xroar_cfg.debug_file & XROAR_DEBUG_FILE_TAPE_FNBLOCK) {
		LOG_PRINT("\tname:  %s\n", f->name);
		LOG_PRINT("\ttype:  %d\n", f->type);
		LOG_PRINT("\tascii: %s\n", f->ascii_flag ? "true" : "false");
		LOG_PRINT("\tgap:   %s\n", f->gap_flag ? "true" : "false");
		LOG_PRINT("\tstart: %04x\n", f->start_address);
		LOG_PRINT("\tload:  %04x\n", f->load_address);
		LOG_PRINT("\tfnblock: .size = %d, .crc = %04x\n", f->fnblock_size, f->fnblock_crc);
	}

	// Check list of known programs:
	for (unsigned i = 0; i < ARRAY_N_ELEMENTS(autorun_special); i++) {
		if (autorun_special[i].size == f->fnblock_size
		    && autorun_special[i].crc == f->fnblock_crc) {
			LOG_DEBUG(1, "Using special load instructions for '%s'\n", autorun_special[i].name);
			keyboard_queue_basic(tip->keyboard_interface, autorun_special[i].run);
			done = 1;
		}
	}

	// Otherwise, use a simple heuristic:
	if (!done) {
		_Bool need_exec = (type == 2 && f->load_address >= 0x01a9);

		switch (type) {
			case 0:
				keyboard_queue_basic(tip->keyboard_interface, "\\eCLOAD\\r\\0RUN\\r");
				break;
			case 2:
				if (need_exec) {
					keyboard_queue_basic(tip->keyboard_interface, "\\eCLOADM:EXEC\\r");
				} else {
					keyboard_queue_basic(tip->keyboard_interface, "\\eCLOADM\\r");
				}
				break;
			default:
				break;
		}
	}

	free(f);

	return type;
}

static struct xroar_timeout *motoroff_timeout = NULL;

// Called whenever the motor control line is written to.

void tape_update_motor(struct tape_interface *ti, _Bool state) {
	struct tape_interface_private *tip = (struct tape_interface_private *)ti;
	if (state) {
		if (ti->tape_input && !tip->waggle_event.queued) {
			/* If motor turned on and tape file attached,
			 * enable the tape input bit waggler */
			tip->waggle_event.at_tick = event_current_tick;
			waggle_bit(tip);
		}
		if (ti->tape_output && !tip->flush_event.queued) {
			tip->flush_event.at_tick = event_current_tick + EVENT_MS(500);
			event_queue(&MACHINE_EVENT_LIST, &tip->flush_event);
			ti->tape_output->last_write_cycle = event_current_tick;
		}
	} else {
		event_dequeue(&tip->waggle_event);
		event_dequeue(&tip->flush_event);
		tape_update_output(ti, tip->last_tape_output);
		if (ti->tape_output && ti->tape_output->module->motor_off) {
			ti->tape_output->module->motor_off(ti->tape_output);
		}
		if (tip->tape_rewrite) {
			tape_desync(tip, 256);
		}
	}
	if (tip->motor != state) {
		if (motoroff_timeout) {
			xroar_cancel_timeout(motoroff_timeout);
			motoroff_timeout = NULL;
		}
		if (!state && xroar_cfg.timeout_motoroff) {
			motoroff_timeout = xroar_set_timeout(xroar_cfg.timeout_motoroff);
		}
		if (!state && xroar_cfg.snap_motoroff) {
			write_snapshot(xroar_cfg.snap_motoroff);
		}
		LOG_DEBUG(2, "Tape: motor %s\n", state ? "ON" : "OFF");
	}
	tip->motor = state;
	set_breakpoints(tip);
}

// Called whenever the DAC is written to.

void tape_update_output(struct tape_interface *ti, uint8_t value) {
	struct tape_interface_private *tip = (struct tape_interface_private *)ti;
	if (tip->motor && ti->tape_output && !tip->tape_rewrite) {
		int length = event_current_tick - ti->tape_output->last_write_cycle;
		ti->tape_output->module->sample_out(ti->tape_output, tip->last_tape_output, length);
		ti->tape_output->last_write_cycle = event_current_tick;
	}
	tip->last_tape_output = value;
}

// Read pulse & duration, schedule next read.

static void waggle_bit(void *sptr) {
	struct tape_interface_private *tip = sptr;
	struct tape_interface *ti = &tip->public;
	tip->in_pulse = tape_pulse_in(ti->tape_input, &tip->in_pulse_width);
	switch (tip->in_pulse) {
	default:
	case -1:
		DELEGATE_CALL1(ti->update_audio, 0.5);
		event_dequeue(&tip->waggle_event);
		return;
	case 0:
		DELEGATE_CALL1(ti->update_audio, 0.0);
		break;
	case 1:
		DELEGATE_CALL1(ti->update_audio, 1.0);
		break;
	}
	tip->waggle_event.at_tick += tip->in_pulse_width;
	event_queue(&MACHINE_EVENT_LIST, &tip->waggle_event);
}

// Ensure any "pulse" over 1/2 second long is flushed to output, so it doesn't
// overflow any counters.

static void flush_output(void *sptr) {
	struct tape_interface_private *tip = sptr;
	struct tape_interface *ti = &tip->public;
	tape_update_output(ti, tip->last_tape_output);
	if (tip->motor) {
		tip->flush_event.at_tick += EVENT_MS(500);
		event_queue(&MACHINE_EVENT_LIST, &tip->flush_event);
	}
}

/* Fast tape reading
 *
 * A set of breakpoint handlers that replace ROM routines, avoiding the need to
 * emulate the CPU. */

// When we accelerate tape read operations, the system clock won't have
// changed, but we need to pretend it did.  This reads pulses from the tape to
// simulate the elapsed time and reschedules tape events accordingly.  It has
// no effect on the output tape, as we may be rewriting to that.

static void advance_read_time(struct tape_interface_private *tip, int skip) {
	struct tape_interface *ti = &tip->public;
	while (skip >= tip->in_pulse_width) {
		skip -= tip->in_pulse_width;
		tip->in_pulse = tape_pulse_in(ti->tape_input, &tip->in_pulse_width);
		if (tip->in_pulse < 0) {
			event_dequeue(&tip->waggle_event);
			return;
		}
	}
	tip->in_pulse_width -= skip;
	tip->waggle_event.at_tick = event_current_tick + tip->in_pulse_width;
	event_queue(&MACHINE_EVENT_LIST, &tip->waggle_event);
	DELEGATE_CALL1(ti->update_audio, tip->in_pulse ? 1.0 : 0.0);
}

// Apply accumulated time skip to read state

static void do_skip_read_time(struct tape_interface_private *tip) {
	advance_read_time(tip, tip->cpuskip * EVENT_SAM_CYCLES(16));
	tip->cpuskip = 0;
}

// Update read time based on how far into current pulse we are

static void update_read_time(struct tape_interface_private *tip) {
	event_ticks skip = tip->waggle_event.at_tick - event_current_tick;
	int s = event_tick_delta(tip->in_pulse_width, skip);
	if (s >= 0) {
		advance_read_time(tip, s);
	}
}

static uint8_t op_add(struct MC6809 *cpu, uint8_t v1, uint8_t v2) {
	unsigned int v = v1 + v2;
	cpu->reg_cc &= ~0x2f;  /* clear HNZVC */
	if (v & 0x80) cpu->reg_cc |= 0x08;  /* set N */
	if ((v & 0xff) == 0) cpu->reg_cc |= 0x04;  /* set Z */
	if ((v1^v2^v^(v>>1)) & 0x80) cpu->reg_cc |= 0x02;  /* set V */
	if (v & 0x100) cpu->reg_cc |= 0x01;  /* set C */
	if ((v1^v2^v) & 0x10) cpu->reg_cc |= 0x20;  /* set H */
	return v;
}

static uint8_t op_sub(struct MC6809 *cpu, uint8_t v1, uint8_t v2) {
	unsigned int v = v1 - v2;
	cpu->reg_cc &= ~0x0f;  /* clear NZVC */
	if (v & 0x80) cpu->reg_cc |= 0x08;  /* set N */
	if ((v & 0xff) == 0) cpu->reg_cc |= 0x04;  /* set Z */
	if ((v1^v2^v^(v>>1)) & 0x80) cpu->reg_cc |= 0x02;  /* set V */
	if (v & 0x100) cpu->reg_cc |= 0x01;  /* set C */
	return v;
}

static uint8_t op_clr(struct MC6809 *cpu) {
	cpu->reg_cc &= ~0x0b;  /* clear NVC */
	cpu->reg_cc |= 0x04;  /* set Z */
	return 0;
}

// CPU operation equivalents

#define CC(tip) ((tip)->cpu->reg_cc)
#define CPUSKIP(tip,n) (tip->cpuskip += (n))

#define BHI(tip) (CPUSKIP(tip, 3), !(CC(tip) & 0x05))
#define BLS(tip) (!BHI(tip))
#define BCC(tip) (CPUSKIP(tip, 3), !(CC(tip) & 0x01))
#define BHS(tip) (BCC(tip))
#define BCS(tip) (!BCC(tip))
#define BLO(tip) (BCS(tip))
#define BNE(tip) (CPUSKIP(tip, 3), !(CC(tip) & 0x04))
#define BRA(tip) (CPUSKIP(tip, 3))

#define BSR(tip,f) do { CPUSKIP(tip, 7); f(tip); } while (0)
#define RTS(tip)  do { CPUSKIP(tip, 5); } while (0)
#define CLR(tip,a) do { CPUSKIP(tip, 6); tip->machine->write_byte(tip->machine, (a), 0); } while (0)
#define DEC(tip,a) do { CPUSKIP(tip, 6); tip->machine->write_byte(tip->machine, (a), tip->machine->read_byte(tip->machine, a) - 1); } while (0)
#define INC(tip,a) do { CPUSKIP(tip, 6); tip->machine->write_byte(tip->machine, (a), tip->machine->read_byte(tip->machine, a) + 1); } while (0)

static void motor_on_delay(struct tape_interface_private *tip) {
	int delay = tip->is_dragon ? 0x95 : 0x8a;
	CPUSKIP(tip, 5);  /* LDX <$95 */
	int i = (tip->machine->read_byte(tip->machine, delay) << 8) | tip->machine->read_byte(tip->machine, delay+1);
	if (tip->is_dragon)
		CPUSKIP(tip, 5);  /* LBRA delay_X */
	for (; i; i--) {
		CPUSKIP(tip, 5);  /* LEAX -1,X */
		CPUSKIP(tip, 3);  /* BNE delay_X */
		/* periodically sync up tape position */
		if ((i & 63) == 0)
			do_skip_read_time(tip);
	}
	tip->cpu->reg_x = 0;
	tip->cpu->reg_cc |= 0x04;
	RTS(tip);
}

// Sample the cassette port input.  The input is inverted so a positive signal
// results in CC.C clear, or set if negative.

static void sample_cas(struct tape_interface_private *tip) {
	int pwcount = tip->is_dragon ? 0x82 : 0x83;
	INC(tip, pwcount);
	CPUSKIP(tip, 5);  /* LDB >$FF20 */
	do_skip_read_time(tip);
	CPUSKIP(tip, 2);  /* RORB */
	if (tip->in_pulse) {
		tip->cpu->reg_cc &= ~1;
	} else {
		tip->cpu->reg_cc |= 1;
	}
	RTS(tip);
}

static void tape_wait_p0(struct tape_interface_private *tip) {
	do {
		BSR(tip, sample_cas);
		if (tip->in_pulse < 0) return;
		CPUSKIP(tip, 3);  /* BCS tape_wait_p0 */
	} while (tip->cpu->reg_cc & 0x01);
	RTS(tip);
}

static void tape_wait_p1(struct tape_interface_private *tip) {
	do {
		BSR(tip, sample_cas);
		if (tip->in_pulse < 0) return;
		CPUSKIP(tip, 3);  /* BCC tape_wait_p1 */
	} while (!(tip->cpu->reg_cc & 0x01));
	RTS(tip);
}

static void tape_wait_p0_p1(struct tape_interface_private *tip) {
	BSR(tip, tape_wait_p0);
	if (tip->in_pulse < 0) return;
	tape_wait_p1(tip);
}

static void tape_wait_p1_p0(struct tape_interface_private *tip) {
	BSR(tip, tape_wait_p1);
	if (tip->in_pulse < 0) return;
	tape_wait_p0(tip);
}

// Check measured cycle width against thresholds.  Clears bcount (number of
// leader bits so far) if too long.  Otherwise flags set as result of comparing
// against minpw1200.

static void L_BDC3(struct tape_interface_private *tip) {
	int pwcount = tip->is_dragon ? 0x82 : 0x83;
	int bcount = tip->is_dragon ? 0x83 : 0x82;
	int minpw1200 = tip->is_dragon ? 0x93 : 0x91;
	int maxpw1200 = tip->is_dragon ? 0x94 : 0x90;
	CPUSKIP(tip, 4);  /* LDB <$82 */
	CPUSKIP(tip, 4);  /* CMPB <$94 */
	op_sub(tip->cpu, tip->machine->read_byte(tip->machine, pwcount), tip->machine->read_byte(tip->machine, maxpw1200));
	if (BHI(tip)) {  // BHI L_BDCC
		CLR(tip, bcount);
		op_clr(tip->cpu);
		RTS(tip);
		return;
	}
	CPUSKIP(tip, 4);  /* CMPB <$93 */
	op_sub(tip->cpu, tip->machine->read_byte(tip->machine, pwcount), tip->machine->read_byte(tip->machine, minpw1200));
	RTS(tip);
}

static void tape_cmp_p1_1200(struct tape_interface_private *tip) {
	int pwcount = tip->is_dragon ? 0x82 : 0x83;
	CLR(tip, pwcount);
	BSR(tip, tape_wait_p0);
	if (tip->in_pulse < 0) return;
	BRA(tip);  /* BRA L_BDC3 */
	L_BDC3(tip);
}

static void tape_cmp_p0_1200(struct tape_interface_private *tip) {
	int pwcount = tip->is_dragon ? 0x82 : 0x83;
	CLR(tip, pwcount);
	BSR(tip, tape_wait_p1);
	if (tip->in_pulse < 0) return;
	// fall through to L_BDC3
	L_BDC3(tip);
}

// Replicates the ROM routine that detects leaders.  Waits for two
// complementary bits in sequence.  Also detects inverted phase.

enum {
	L_BDED,
	L_BDEF,
	L_BDF3,
	L_BDFF,
	L_BE03,
	L_BE0D
};

static void sync_leader(struct tape_interface_private *tip) {
	int bcount = tip->is_dragon ? 0x83 : 0x82;
	int phase = 0;
	int state = L_BDED;
	_Bool done = 0;

	while (!done && tip->in_pulse >= 0) {
		switch (state) {
		case L_BDED:
			BSR(tip, tape_wait_p0_p1);
			state = L_BDEF;  // fall through to L_BDEF
			break;

		case L_BDEF:
			BSR(tip, tape_cmp_p1_1200);
			if (BHI(tip)) {
				state = L_BDFF;  // BHI L_BDFF
				break;
			}
			state = L_BDF3;  // fall through to L_BDF3
			break;

		case L_BDF3:
			BSR(tip, tape_cmp_p0_1200);
			if (BLO(tip)) {
				state = L_BE03;  // BLO L_BE03
				break;
			}
			INC(tip, bcount);
			CPUSKIP(tip, 4);  // LDA <$83
			CPUSKIP(tip, 2);  // CMPA #$60
			phase = tip->machine->read_byte(tip->machine, bcount);
			op_sub(tip->cpu, phase, 0x60);
			BRA(tip);
			state = L_BE0D;  // BRA L_BE0D
			break;

		case L_BDFF:
			BSR(tip, tape_cmp_p0_1200);
			if (BHI(tip)) {
				state = L_BDEF;  // BHI L_BDEF
				break;
			}
			state = L_BE03;  // fall through to L_BE03
			break;

		case L_BE03:
			BSR(tip, tape_cmp_p1_1200);
			if (BCS(tip)) {
				state = L_BDF3;  // BCS L_BDF3
				break;
			}
			DEC(tip, bcount);
			CPUSKIP(tip, 4);  // LDA <$83
			CPUSKIP(tip, 2);  // ADDA #$60
			phase = op_add(tip->cpu, tip->machine->read_byte(tip->machine, bcount), 0x60);
			state = L_BE0D;
			break;

		case L_BE0D:
			if (BNE(tip)) {
				state = L_BDED;  // BNE L_BDED
				break;
			}
			CPUSKIP(tip, 4);  // STA <$84
			tip->machine->write_byte(tip->machine, 0x84, phase);
			RTS(tip);
			done = 1;
			break;
		}
	}
}

static void tape_wait_2p(struct tape_interface_private *tip) {
	int pwcount = tip->is_dragon ? 0x82 : 0x83;
	CLR(tip, pwcount);
	CPUSKIP(tip, 6);  /* TST <$84 */
	CPUSKIP(tip, 3);  /* BNE tape_wait_p1_p0 */
	if (tip->machine->read_byte(tip->machine, 0x84)) {
		tape_wait_p1_p0(tip);
	} else {
		tape_wait_p0_p1(tip);
	}
}

static void bitin(struct tape_interface_private *tip) {
	int pwcount = tip->is_dragon ? 0x82 : 0x83;
	int mincw1200 = tip->is_dragon ? 0x92 : 0x8f;
	BSR(tip, tape_wait_2p);
	CPUSKIP(tip, 4);  /* LDB <$82 */
	CPUSKIP(tip, 2);  /* DECB */
	CPUSKIP(tip, 4);  /* CMPB <$92 */
	op_sub(tip->cpu, tip->machine->read_byte(tip->machine, pwcount) - 1, tip->machine->read_byte(tip->machine, mincw1200));
	RTS(tip);
}

static void cbin(struct tape_interface_private *tip) {
	int bcount = tip->is_dragon ? 0x83 : 0x82;
	int bin = 0;
	CPUSKIP(tip, 2);  /* LDA #$08 */
	CPUSKIP(tip, 4);  /* STA <$83 */
	for (int i = 8; i; i--) {
		BSR(tip, bitin);
		CPUSKIP(tip, 2);  // RORA
		bin >>= 1;
		bin |= (CC(tip) & 0x01) ? 0x80 : 0;
		CPUSKIP(tip, 6);  // DEC <$83
		CPUSKIP(tip, 3);  // BNE $BDB1
	}
	RTS(tip);
	MC6809_REG_A(tip->cpu) = bin;
	tip->machine->write_byte(tip->machine, bcount, 0);
}

// fast_motor_on() skips the standard delay if a short leader was detected
// (usually old CAS files).

static void fast_motor_on(void *sptr) {
	struct tape_interface_private *tip = sptr;
	update_read_time(tip);
	if (!tip->short_leader) {
		motor_on_delay(tip);
	} else {
		tip->cpu->reg_x = 0;
		tip->cpu->reg_cc |= 0x04;
	}
	tip->machine->op_rts(tip->machine);
	do_skip_read_time(tip);
}

// Similarly, fast_sync_leader() just assumes leader has been sensed

static void fast_sync_leader(void *sptr) {
	struct tape_interface_private *tip = sptr;
	update_read_time(tip);
	if (!tip->short_leader) {
		sync_leader(tip);
	}
	tip->machine->op_rts(tip->machine);
	do_skip_read_time(tip);
}

static void fast_bitin(void *sptr) {
	struct tape_interface_private *tip = sptr;
	update_read_time(tip);
	bitin(tip);
	tip->machine->op_rts(tip->machine);
	do_skip_read_time(tip);
	if (tip->tape_rewrite) rewrite_bitin(tip);
}

static void fast_cbin(void *sptr) {
	struct tape_interface_private *tip = sptr;
	update_read_time(tip);
	cbin(tip);
	tip->machine->op_rts(tip->machine);
	do_skip_read_time(tip);
}

/* Leader padding & tape rewriting */

// Flags tape rewrite stream as desynced.  At this point, some amount of leader
// bytes are expected followed by a sync byte, at which point rewriting will
// continue.  Long leader follows motor off/on, short leader follows normal
// data block.

static void tape_desync(struct tape_interface_private *tip, int leader) {
	struct tape_interface *ti = &tip->public;
	if (tip->tape_rewrite) {
		// pad last byte with trailer pattern
		while (tip->rewrite_bit_count) {
			tape_bit_out(ti->tape_output, ~tip->rewrite_bit_count & 1);
		}
		// one byte of trailer before any silence (but not following silence)
		if (leader > 0 && !tip->rewrite_silence) {
			tape_byte_out(ti->tape_output, 0x55);
			leader--;
		}
		// desync tape rewrite - will continue once a sync byte is read
		tip->rewrite_have_sync = 0;
		tip->rewrite_leader_count = leader;
	}
}

// When a sync byte is encountered, rewrite an appropriate length of leader
// followed by the sync byte.  Flag stream as in sync - subsequent bits will be
// rewritten verbatim.

static void rewrite_sync(void *sptr) {
	/* BLKIN, having read sync byte $3C */
	struct tape_interface_private *tip = sptr;
	struct tape_interface *ti = &tip->public;
	if (tip->rewrite_have_sync) return;
	if (tip->tape_rewrite) {
		for (int i = 0; i < tip->rewrite_leader_count; i++)
			tape_byte_out(ti->tape_output, 0x55);
		tape_byte_out(ti->tape_output, 0x3c);
		tip->rewrite_have_sync = 1;
	}
}

// Rewrites bits returned by the BITIN routine, but only while flagged as
// synced.

static void rewrite_bitin(void *sptr) {
	/* RTS from BITIN */
	struct tape_interface_private *tip = sptr;
	struct tape_interface *ti = &tip->public;
	if (tip->tape_rewrite && tip->rewrite_have_sync) {
		tape_bit_out(ti->tape_output, tip->cpu->reg_cc & 0x01);
	}
}

// When tape motor turned on, rewrite a standard duration of silence and flag
// the stream as desynced, expecting a long leader before the next block.

static void rewrite_tape_on(void *sptr) {
	/* CSRDON */
	struct tape_interface_private *tip = sptr;
	struct tape_interface *ti = &tip->public;
	/* desync with long leader */
	tape_desync(tip, 256);
	if (tip->tape_rewrite && ti->tape_output) {
		tape_sample_out(ti->tape_output, 0x81, EVENT_MS(250));
		tape_sample_out(ti->tape_output, 0x7f, EVENT_MS(250));
		tip->rewrite_silence = 1;
	}
}

// When finished reading a block, flag the stream as desynced expecting a short
// intra-block leader before the next.

static void rewrite_end_of_block(void *sptr) {
	/* BLKIN, having confirmed checksum */
	struct tape_interface_private *tip = sptr;
	/* desync with short inter-block leader */
	tape_desync(tip, 2);
}

/* Configuring tape options */

// Fast tape loading intercepts various ROM calls and uses equivalents provided
// here to bypass the need for CPU emulation.

static struct machine_bp bp_list_fast[] = {
	BP_DRAGON_ROM(.address = 0xbdd7, .handler = DELEGATE_INIT(fast_motor_on, NULL) ),
	BP_COCO_ROM(.address = 0xa7d1, .handler = DELEGATE_INIT(fast_motor_on, NULL) ),
	BP_DRAGON_ROM(.address = 0xbded, .handler = DELEGATE_INIT(fast_sync_leader, NULL) ),
	BP_COCO_ROM(.address = 0xa782, .handler = DELEGATE_INIT(fast_sync_leader, NULL) ),
	BP_DRAGON_ROM(.address = 0xbda5, .handler = DELEGATE_INIT(fast_bitin, NULL) ),
	BP_COCO_ROM(.address = 0xa755, .handler = DELEGATE_INIT(fast_bitin, NULL) ),
};

// Need to enable these two aspects of fast loading if tape padding turned on
// and short leader detected.

static struct machine_bp bp_list_pad[] = {
	BP_DRAGON_ROM(.address = 0xbdd7, .handler = DELEGATE_INIT(fast_motor_on, NULL) ),
	BP_COCO_ROM(.address = 0xa7d1, .handler = DELEGATE_INIT(fast_motor_on, NULL) ),
	BP_DRAGON_ROM(.address = 0xbded, .handler = DELEGATE_INIT(fast_sync_leader, NULL) ),
	BP_COCO_ROM(.address = 0xa782, .handler = DELEGATE_INIT(fast_sync_leader, NULL) ),
};

// Intercepting CBIN for fast loading isn't compatible with tape-rewriting, so
// listed separately.

static struct machine_bp bp_list_fast_cbin[] = {
	BP_DRAGON_ROM(.address = 0xbdad, .handler = DELEGATE_INIT(fast_cbin, NULL) ),
	BP_COCO_ROM(.address = 0xa749, .handler = DELEGATE_INIT(fast_cbin, NULL) ),
};


// Tape rewriting intercepts the returns from various ROM calls to interpret
// the loading state - whether a leader is expected, etc.

static struct machine_bp bp_list_rewrite[] = {
	BP_DRAGON_ROM(.address = 0xb94d, .handler = DELEGATE_INIT(rewrite_sync, NULL) ),
	BP_COCO_ROM(.address = 0xa719, .handler = DELEGATE_INIT(rewrite_sync, NULL) ),
	BP_DRAGON_ROM(.address = 0xbdac, .handler = DELEGATE_INIT(rewrite_bitin, NULL) ),
	BP_COCO_ROM(.address = 0xa75c, .handler = DELEGATE_INIT(rewrite_bitin, NULL) ),
	BP_DRAGON_ROM(.address = 0xbdeb, .handler = DELEGATE_INIT(rewrite_tape_on, NULL) ),
	BP_COCO_ROM(.address = 0xa780, .handler = DELEGATE_INIT(rewrite_tape_on, NULL) ),
	BP_DRAGON_ROM(.address = 0xb97e, .handler = DELEGATE_INIT(rewrite_end_of_block, NULL) ),
	BP_COCO_ROM(.address = 0xa746, .handler = DELEGATE_INIT(rewrite_end_of_block, NULL) ),
};

static void set_breakpoints(struct tape_interface_private *tip) {
	/* clear any old breakpoints */
	machine_bp_remove_list(tip->machine, bp_list_fast);
	machine_bp_remove_list(tip->machine, bp_list_fast_cbin);
	machine_bp_remove_list(tip->machine, bp_list_rewrite);
	if (!tip->motor)
		return;
	// Don't intercept calls if there's no input tape.  The optimisations
	// are only for reading.  Also, this helps works around missing
	// silences...
	if (!tip->public.tape_input)
		return;
	// Add required breakpoints
	if (tip->tape_fast) {
		machine_bp_add_list(tip->machine, bp_list_fast, tip);
		// CBIN intercept is incompatible with tape rewriting
		if (!tip->tape_rewrite) {
			machine_bp_add_list(tip->machine, bp_list_fast_cbin, tip);
		}
	} else if (tip->short_leader) {
		machine_bp_add_list(tip->machine, bp_list_pad, tip);
	}
	if (tip->tape_rewrite) {
		machine_bp_add_list(tip->machine, bp_list_rewrite, tip);
	}
}

// Updates flags and sets appropriate breakpoints.

void tape_set_state(struct tape_interface *ti, int flags) {
	struct tape_interface_private *tip = (struct tape_interface_private *)ti;
	tip->tape_fast = flags & TAPE_FAST;
	tip->tape_pad_auto = flags & TAPE_PAD_AUTO;
	tip->tape_rewrite = flags & TAPE_REWRITE;
	set_breakpoints(tip);
}

// Sets tape flags and updates UI.

void tape_select_state(struct tape_interface *ti, int flags) {
	struct tape_interface_private *tip = (struct tape_interface_private *)ti;
	tape_set_state(ti, flags);
	DELEGATE_CALL3(tip->ui->set_state, ui_tag_tape_flags, flags, NULL);
}

// Get current tape flags.

int tape_get_state(struct tape_interface *ti) {
	struct tape_interface_private *tip = (struct tape_interface_private *)ti;
	int flags = 0;
	if (tip->tape_fast) flags |= TAPE_FAST;
	if (tip->tape_pad_auto) flags |= TAPE_PAD_AUTO;
	if (tip->tape_rewrite) flags |= TAPE_REWRITE;
	return flags;
}
