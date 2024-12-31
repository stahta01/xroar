/** \file
 *
 *  \brief Cassette tape support.
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
 */

#include "top-config.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"
#include "delegate.h"
#include "intfuncs.h"
#include "sds.h"
#include "sdsx.h"
#include "xalloc.h"

#include "auto_kbd.h"
#include "breakpoint.h"
#include "crc16.h"
#include "events.h"
#include "fs.h"
#include "logging.h"
#include "machine.h"
#include "mc6801/mc6801.h"
#include "mc6809/mc6809.h"
#include "messenger.h"
#include "part.h"
#include "snapshot.h"
#include "sound.h"
#include "tape.h"
#include "ui.h"
#include "xroar.h"

// maximum number of pulses to buffer for frequency analysis:
#define PULSE_BUFFER_SIZE (400)

struct tape_interface_private {
	struct tape_interface public;

	// Messenger client id
	int msgr_client_id;

	struct machine *machine;
	struct ui_interface *ui;

	struct debug_cpu *debug_cpu;
	_Bool is_6809;
	_Bool is_6803;
	struct {
		uint16_t pwcount;
		uint16_t bcount;
		uint16_t bphase;
		uint16_t minpw1200;
		uint16_t maxpw1200;
		uint16_t mincw1200;
		uint16_t motor_delay;
	} addr;

	int short_leader_threshold;
	int initial_motor_delay;

	_Bool tape_fast;
	_Bool tape_pad_auto;
	_Bool tape_rewrite;
	_Bool short_leader;

	int in_pulse;
	int in_pulse_width;
	// When accelerating operations, accumulated number of simulated CPU cycles
	int skip_ncycles;

	uint8_t last_tape_output;
	_Bool playing;  // manual motor control
	_Bool motor;  // automatic motor control

	struct {
		// Whether a sync byte has been detected yet.  If false, we're still
		// reading the leader.
		_Bool have_sync;
		// Amount of leader bytes to write when the next sync byte is detected
		int leader_count;
		// Track number of bits written and keep things byte-aligned
		int bit_count;
		// Track the fractional sample part when rewriting to tape
		int sremain;
		// Was the last thing rewritten silence?  If so, any subsequent motor
		// events won't trigger a trailer byte.
		_Bool silence;

		// Input pulse buffer during sync.  When synced, contents will
		// be analysed for average pulse widths then writes will use
		// those widths.
		int pulse_buffer_index;
		int pulse_buffer[PULSE_BUFFER_SIZE];

		_Bool have_pulse_widths;
		int bit0_pwt;
		int bit1_pwt;
	} rewrite;

	struct event waggle_event;
	struct event flush_event;
};

static void waggle_bit(void *);
static void flush_output(void *);
static void tape_ui_set_playing(void *, int tag, void *smsg);
static void tape_ui_set_tape_flag(void *, int tag, void *smsg);
static void update_motor(struct tape_interface_private *tip);

static void rewrite_tape_desync(struct tape_interface_private *tip, int leader);
static void rewrite_sync(void *sptr);
static void rewrite_bitin(void *sptr);
static void rewrite_tape_on(void *sptr);
static void rewrite_end_of_block(void *sptr);

static void set_breakpoints(struct tape_interface_private *tip);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Special case autorun instructions based on filename block size and CRC16.

struct tape_file_autorun {
	const char *name;
	int size;
	uint16_t crc;
	const char *run;
};

static struct tape_file_autorun autorun_special[] = {

	// Programs with special load instructions

	{
		.name = "Dungeon Raid (1984 Microdeal)",
		.size = 36, .crc = 0x1822,
		.run = "\\025CLEAR0\\r\\0CLOADM\\r",
	},
	{
		.name = "Electronic Author (1985 Smithson Computing)",
		.size = 15, .crc = 0x8866,
		.run = "\\025CLEAR20\\r\\0CLOADM\\r",
	},
	{
		.name = "Galacticans (1983 Softek)",
		.size = 15, .crc = 0xd39b,
		.run = "\\025PCLEAR1\\r\\0CLEAR200,7777\\r\\0CLOADM\\r",
	},
	{
		.name = "Lucifer's Kingdom (1988 Orange Software)",
		.size = 15, .crc = 0x7f34,
		.run = "\\025CLEAR1,32767:CLOADM\\r",
	},
	{
		.name = "North-Sea Action (1988 Orange Software)",
		.size = 15, .crc = 0x9c2b,
		.run = "\\025CLEAR20\\r\\0CLOADM\\r\\0EXEC\\r",
	},
	{
		.name = "Speak Up! (1983 Classical Computing Inc)",
		.size = 15, .crc = 0x7bff,
		.run = "\\025CLEAR200,25448\\r\\0CLOADM\\r\\0EXEC\\r",
	},
	{
		.name = "Spy Against Spy (1987 Starship Software)",
		.size = 15, .crc = 0x48a0,
		.run = "\\025CLEAR20:CLOADM\\r",
	},
	{
		.name = "Tanglewood (1986 Microdeal)",
		.size = 115, .crc = 0x7e5e,
		.run = "\\025CLEAR10\\r\\0CLOADM\\r",
	},
	{
		.name = "Ultrapede (1983 Softek)",
		.size = 15, .crc = 0x337a,
		.run = "\\025CLOADM\\r",
	},
	{
		.name = "Utopia (1988 Starship Software)",
		.size = 15, .crc = 0xeb14,
		.run = "\\025CLEAR10:CLOADM\\r\\0EXEC\\r",
	},

	// Just acknowledge some more recent releases.  This isn't exhaustive,
	// of course, just what I have to hand.

	{
		.name = "3D Deathchase (2009 James McKay)",
		.size = 15, .crc = 0xc87c,
	},
	{
		.name = "Ball Dozer (1988 Kouga Software)",
		.size = 15, .crc = 0xda1e,
	},
	{
		.name = "Blockdown (2020 \"Seismic Brains\"; early release)",
		.size = 201, .crc = 0xc11f,
	},
	{
		.name = "Blockdown (2021 Teipen Mwnci)",
		.size = 217, .crc = 0x911b,
	},
	{
		.name = "Dunjunz (2017 Teipen Mwnci; early release)",
		.size = 137, .crc = 0x367d,
	},
	{
		.name = "Dunjunz (2018 Teipen Mwnci; Anniversary Edition)",
		.size = 166, .crc = 0x5847,
	},
	{
		.name = "Flagon Bird (2014 Bosco)",
		.size = 177, .crc = 0xf14d,
	},
	{
		.name = "Glove (2009 James McKay)",
		.size = 15, .crc = 0x4173,
	},
	{
		.name = "Jumping Joey (2022 Nickolas Marentes)",
		.size = 154, .crc = 0x1320,
	},
	{
		.name = "Pipes (2020 Nickolas Marentes)",
		.size = 159, .crc = 0x1443,
	},
	{
		.name = "Rally-SG (2020 Nickolas Marentes)",
		.size = 15, .crc = 0xc87e,
	},
	{
		.name = "Shanghai (1991 Burgin)",
		.size = 149, .crc = 0x89cd,
	},
	{
		.name = "ROTABB (1989 Kouga Software)",
		.size = 15, .crc = 0x400b,
	},

};

// ---------------------------------------------------------------------------

struct tape_interface *tape_interface_new(struct ui_interface *ui) {
	struct tape_interface_private *tip = xmalloc(sizeof(*tip));
	*tip = (struct tape_interface_private){0};
	struct tape_interface *ti = &tip->public;

	// Register with messenger
	tip->msgr_client_id = messenger_client_register();

	ui_messenger_preempt_group(tip->msgr_client_id, ui_tag_tape_playing, MESSENGER_NOTIFY_DELEGATE(tape_ui_set_playing, tip));
	ui_messenger_preempt_group(tip->msgr_client_id, ui_tag_tape_flag_fast, MESSENGER_NOTIFY_DELEGATE(tape_ui_set_tape_flag, tip));
	ui_messenger_preempt_group(tip->msgr_client_id, ui_tag_tape_flag_pad_auto, MESSENGER_NOTIFY_DELEGATE(tape_ui_set_tape_flag, tip));
	ui_messenger_preempt_group(tip->msgr_client_id, ui_tag_tape_flag_rewrite, MESSENGER_NOTIFY_DELEGATE(tape_ui_set_tape_flag, tip));

	tip->ui = ui;
	tip->in_pulse = -1;
	tip->rewrite.leader_count = xroar.cfg.tape.rewrite_leader;
	tip->rewrite.silence = 1;
	tip->rewrite.bit0_pwt = 6403;
	tip->rewrite.bit1_pwt = 3489;

	tape_interface_disconnect_machine(ti);

	event_init(&tip->waggle_event, DELEGATE_AS0(void, waggle_bit, tip));
	event_init(&tip->flush_event, DELEGATE_AS0(void, flush_output, tip));

	return &tip->public;
}

void tape_interface_free(struct tape_interface *ti) {
	if (!ti)
		return;
	struct tape_interface_private *tip = (struct tape_interface_private *)ti;
	messenger_client_unregister(tip->msgr_client_id);
	tape_close_reading(ti);
	tape_close_writing(ti);
	event_dequeue(&tip->flush_event);
	event_dequeue(&tip->waggle_event);
	free(tip);
}

// Connecting a machine allows breakpoints to be set on that machine to
// implement fast loading & tape rewriting.  This should all probably be
// abstracted out.

void tape_interface_connect_machine(struct tape_interface *ti, struct machine *m) {
	struct tape_interface_private *tip = (struct tape_interface_private *)ti;

	_Bool is_dragon = 0;
	if (strcmp(m->config->architecture, "dragon32") == 0
	    || strcmp(m->config->architecture, "dragon64") == 0) {
		is_dragon = 1;
	}

	tip->machine = m;

	tip->debug_cpu = (struct debug_cpu *)part_component_by_id_is_a((struct part *)m, "CPU", "DEBUG-CPU");
	tip->is_6809 = part_is_a(&tip->debug_cpu->part, "MC6809");
	tip->is_6803 = part_is_a(&tip->debug_cpu->part, "MC6803");

	tip->short_leader_threshold = is_dragon ? 114 : 130;

	if (tip->is_6809) {
		tip->addr.pwcount = is_dragon ? 0x0082 : 0x0083;
		tip->addr.bcount = is_dragon ? 0x0083 : 0x0082;
		tip->addr.bphase = 0x0084;
		tip->addr.minpw1200 = is_dragon ? 0x0093 : 0x0091;
		tip->addr.maxpw1200 = is_dragon ? 0x0094 : 0x0090;
		tip->addr.mincw1200 = is_dragon ? 0x0092 : 0x008f;
		tip->initial_motor_delay = is_dragon ? 5 : 0;
		tip->addr.motor_delay = is_dragon ? 0x0095 : 0x008a;
	} else if (tip->is_6803) {
		tip->addr.pwcount = 0x427d;
		tip->addr.bcount = 0x427c;
		tip->addr.bphase = 0x427e;
		tip->addr.minpw1200 = 0x422e;
		tip->addr.maxpw1200 = 0x422d;
		tip->addr.mincw1200 = 0x422c;
	}

	ti->update_audio = DELEGATE_AS1(void, float, m->get_interface(m, "tape-update-audio"), m);
	DELEGATE_CALL(ti->update_audio, 0.5);
}

void tape_interface_disconnect_machine(struct tape_interface *ti) {
	struct tape_interface_private *tip = (struct tape_interface_private *)ti;
	tip->machine = NULL;
	tip->debug_cpu = NULL;
	ti->update_audio = DELEGATE_DEFAULT1(void, float);
}

int tape_seek(struct tape *t, long offset, int whence) {
	struct tape_interface *ti = t->tape_interface;
	struct tape_interface_private *tip = (struct tape_interface_private *)ti;

	int r = t->module->seek(t, offset, whence);

	if (tip) {
		update_motor(tip);

		// XXX review this.  If seeking to the beginning of either tape,
		// desyncs rewriting.
		if (tip->tape_rewrite && r >= 0 && t->offset == 0) {
			rewrite_tape_desync(tip, xroar.cfg.tape.rewrite_leader);
		}
	}
	return r;
}

static int tape_pulse_in(struct tape *t, int *pulse_width) {
	if (!t) return -1;
	struct tape_interface *ti = t->tape_interface;
	struct tape_interface_private *tip = (struct tape_interface_private *)ti;
	int r = t->module->pulse_in(t, pulse_width);
	if (tip && tip->tape_rewrite) {
		tip->rewrite.pulse_buffer[tip->rewrite.pulse_buffer_index] = *pulse_width;
		tip->rewrite.pulse_buffer_index = (tip->rewrite.pulse_buffer_index + 1) % PULSE_BUFFER_SIZE;
	}
	return r;
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
	assert(tip != NULL);
	for (int i = 0; i < 64; ++i) {
		unsigned sr = tip->rewrite.sremain + pulse_width;
		unsigned nticks = sr / 64;
		tip->rewrite.sremain = sr - (nticks * 64);
		int sample = (half_sin[i] * scale * 128.) + 128;
		tape_sample_out(t, sample, nticks);
	}
}

static void tape_bit_out(struct tape *t, int bit) {
	if (!t) return;
	struct tape_interface *ti = t->tape_interface;
	struct tape_interface_private *tip = (struct tape_interface_private *)ti;
	assert(tip != NULL);
	// Magic numbers?  These are the pulse widths (in SAM cycles) that fall
	// in the middle of what is recognised by the ROM read routines, and as
	// such should prove to be the most robust.
	if (bit) {
		write_pulse(t, tip->rewrite.bit1_pwt + 496, 0.7855);
		write_pulse(t, tip->rewrite.bit1_pwt - 496, -0.7855);
	} else {
		write_pulse(t, tip->rewrite.bit0_pwt + 496, 0.7855);
		write_pulse(t, tip->rewrite.bit0_pwt - 496, -0.7855);
	}
	tip->rewrite.bit_count = (tip->rewrite.bit_count + 1) & 7;
	tip->last_tape_output = 0;
	tip->rewrite.silence = 0;
}

static void tape_byte_out(struct tape *t, int byte) {
	if (!t) return;
	for (int i = 8; i; i--) {
		tape_bit_out(t, byte & 1);
		byte >>= 1;
	}
}

// ---------------------------------------------------------------------------

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
	for (int i = 0; i < size; ++i) {
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
		f->fnblock_crc = crc16_ccitt_block(CRC16_CCITT_RESET, block + 2, f->fnblock_size);
		return f;
	}
}

void tape_seek_to_file(struct tape *t, struct tape_file const *f) {
	if (!t || !f) return;
	tape_seek(t, f->offset, SEEK_SET);
}

// ---------------------------------------------------------------------------

struct tape *tape_new(void) {
	struct tape *new = xmalloc(sizeof(*new));
	*new = (struct tape){0};
	return new;
}

void tape_free(struct tape *t) {
	free(t);
}

// ---------------------------------------------------------------------------

void tape_reset(struct tape_interface *ti) {
	struct tape_interface_private *tip = (struct tape_interface_private *)ti;
	tape_close_writing(ti);
	tip->motor = 0;
	event_dequeue(&tip->waggle_event);
	ui_update_state(-1, ui_tag_tape_playing, !ti->default_paused, NULL);
}

struct tape *tape_open(const char *filename, const char *mode) {
	if (!filename || !*filename || !mode) {
		return NULL;
	}
	int ao_rate = xroar.cfg.tape.ao_rate > 0 ? xroar.cfg.tape.ao_rate : 9600;
	int type = xroar_filetype_by_ext(filename);
	// Read-only filetypes
	if (type == FILETYPE_K7 || type == FILETYPE_ASC) {
		if (*mode != 'r') {
			return NULL;
		}
	}
	struct tape *t = NULL;
	switch (type) {
	case FILETYPE_CAS:
		t = tape_cas_open(filename, mode);
		break;
	case FILETYPE_K7:
		t = tape_k7_open(filename, mode);
		break;
	case FILETYPE_ASC:
		t = tape_asc_open(filename, mode);
		break;
	default:
		t = tape_sndfile_open(filename, mode, ao_rate);
		break;
	}
	if (t) {
		t->type = type;
	}
	return t;
}

int tape_open_reading(struct tape_interface *ti, const char *filename) {
	struct tape_interface_private *tip = (struct tape_interface_private *)ti;

	tape_close_reading(ti);

	tip->short_leader = 0;
	if ((ti->tape_input = tape_open(filename, "rb")) == NULL) {
		return -1;
	}
	if (ti->tape_input->type == FILETYPE_CAS) {
		if (ti->tape_input->leader_count < tip->short_leader_threshold) {
			tip->short_leader = 1;
		}
		// leader padding needs breakpoints set
		set_breakpoints(tip);
	}
	ti->tape_input->tape_interface = ti;

	if (ti->tape_input->module->set_panning) {
		ti->tape_input->module->set_panning(ti->tape_input, xroar.cfg.tape.pan);
	}
	if (ti->tape_input->module->set_hysteresis) {
		ti->tape_input->module->set_hysteresis(ti->tape_input, xroar.cfg.tape.hysteresis);
	}
	if (tip->tape_rewrite) {
		rewrite_tape_desync(tip, xroar.cfg.tape.rewrite_leader);
	}
	ui_update_state(-1, ui_tag_tape_playing, !ti->default_paused, NULL);

	if (logging.level >= 1) {
		LOG_MOD_PRINT("tape", "reading: %s", filename);
		LOG_DEBUG(2, " [%s]", tip->playing ? "PLAYING" : "PAUSED");
		LOG_PRINT("\n");
	}
	return 0;
}

void tape_close_reading(struct tape_interface *ti) {
	if (!ti->tape_input) {
		return;
	}
	tape_close(ti->tape_input);
	ti->tape_input = NULL;
}

int tape_open_writing(struct tape_interface *ti, const char *filename) {
	struct tape_interface_private *tip = (struct tape_interface_private *)ti;
	tape_close_writing(ti);
	if ((ti->tape_output = tape_open(filename, "wb")) == NULL) {
		return -1;
	}
	ti->tape_output->tape_interface = ti;

	ui_update_state(-1, ui_tag_tape_playing, !ti->default_paused, NULL);
	tip->rewrite.bit_count = 0;
	tip->rewrite.silence = 1;
	if (logging.level >= 1) {
		LOG_MOD_PRINT("tape", "writing: %s", filename);
		LOG_DEBUG(2, " [%s]", tip->playing ? "PLAYING" : "PAUSED");
		LOG_PRINT("\n");
	}
	return 0;
}

void tape_close_writing(struct tape_interface *ti) {
	struct tape_interface_private *tip = (struct tape_interface_private *)ti;
	if (!ti->tape_output)
		return;
	if (tip->tape_rewrite) {
		// Writes a trailing byte where appropriate
		rewrite_tape_desync(tip, 1);
		// Ensure the tape ends with a short duration of silence
		if (!tip->rewrite.silence) {
			write_silence(ti->tape_output, EVENT_MS(200));
			tip->rewrite.silence = 1;
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

// MC-10 has no remote control, so for autorun, support a rom hook to press
// play at an appropriate time.

static void press_play(void *sptr);

static struct machine_bp bp_list_press_play[] = {
	BP_MC10_ROM(.address = 0xff4e, .handler = DELEGATE_INIT(press_play, NULL) ),
};

static void press_play(void *sptr) {
	struct tape_interface_private *tip = sptr;
	ui_update_state(-1, ui_tag_tape_playing, 1, NULL);
	machine_bp_remove_list(tip->machine, bp_list_press_play);
}

int tape_autorun(struct tape_interface *ti, const char *filename) {
	struct tape_interface_private *tip = (struct tape_interface_private *)ti;
	if (filename == NULL)
		return -1;
	ak_parse_type_string(xroar.auto_kbd, NULL);
	if (tape_open_reading(ti, filename) == -1)
		return -1;
	struct tape_file *f = tape_file_next(ti->tape_input, 0);
	tape_rewind(ti->tape_input);
	if (!f) {
		return -1;
	}

	int type = f->type;
	_Bool done = 0;

	if (logging.debug_file & LOG_FILE_TAPE_FNBLOCK) {
		sds name = sdsx_quote_str(f->name);
		LOG_PRINT("\tname %s\n", name);
		sdsfree(name);
		struct sdsx_list *optlist = sdsx_list_new(NULL);
		switch (f->type) {
		case 0: optlist = sdsx_list_push(optlist, "basic"); break;
		case 1: optlist = sdsx_list_push(optlist, "data"); break;
		case 2: optlist = sdsx_list_push(optlist, "binary"); break;
		default: break;
		}
		if (f->ascii_flag)
			optlist = sdsx_list_push(optlist, "ascii");
		if (f->gap_flag)
			optlist = sdsx_list_push(optlist, "gap");
		if (optlist->len) {
			sds opts = sdsx_join_str(optlist, ",");
			LOG_PRINT("\toptions %s\n", opts);
			sdsfree(opts);
		}
		sdsx_list_free(optlist);
		LOG_PRINT("\tload 0x%04x\n", f->load_address);
		LOG_PRINT("\texec 0x%04x\n", f->start_address);
		LOG_PRINT("\tsize %d\n", f->fnblock_size);
		LOG_PRINT("\tcrc 0x%04x\n", f->fnblock_crc);
	}

	// Check list of known programs:
	for (unsigned i = 0; i < ARRAY_N_ELEMENTS(autorun_special); ++i) {
		if (autorun_special[i].size == f->fnblock_size
		    && autorun_special[i].crc == f->fnblock_crc) {
			if (autorun_special[i].run) {
				LOG_MOD_DEBUG(1, "tape", "%s: using built-in load instructions\n", autorun_special[i].name);
				ak_parse_type_string(xroar.auto_kbd, autorun_special[i].run);
				done = 1;
			} else {
				LOG_MOD_DEBUG(1, "tape", "%s\n", autorun_special[i].name);
				break;
			}
		}
	}

	// Otherwise, use a simple heuristic:
	if (!done) {
		_Bool need_exec = (type == 2 && f->load_address >= 0x01a9);

		switch (type) {
			case 0:
				ak_parse_type_string(xroar.auto_kbd, "\\025CLOAD\\r");
				ak_parse_type_string(xroar.auto_kbd, "RUN\\r");
				break;
			case 2:
				if (need_exec) {
					ak_parse_type_string(xroar.auto_kbd, "\\025CLOADM:EXEC\\r");
				} else {
					ak_parse_type_string(xroar.auto_kbd, "\\025CLOADM\\r");
				}
				break;
			default:
				break;
		}
	}

	machine_bp_add_list(tip->machine, bp_list_press_play, tip);

	free(f);

	return type;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Automatic motor control.  Simulates cassette relay.

void tape_set_motor(struct tape_interface *ti, _Bool motor) {
	struct tape_interface_private *tip = (struct tape_interface_private *)ti;
	_Bool prev_state = tip->motor;
	tip->motor = motor;
	update_motor(tip);
	if (motor != prev_state) {
		LOG_MOD_DEBUG(2, "tape", "MOTOR %s\n", motor ? "ON" : "OFF");
	}
	ui_update_state(tip->msgr_client_id, ui_tag_tape_motor, tip->motor, NULL);
}

// Manual motor control.  UI-triggered play/pause.  Call with play=0 to pause.

static void tape_ui_set_playing(void *sptr, int tag, void *smsg) {
	struct tape_interface_private *tip = sptr;
	struct ui_state_message *uimsg = smsg;
	assert(tag == ui_tag_tape_playing);
	_Bool play = ui_msg_adjust_value_range(uimsg, tip->playing, 0, 0, 1,
					       UI_ADJUST_FLAG_CYCLE);
	if (logging.level >= 2) {
		if (play != tip->playing) {
			LOG_MOD_PRINT("tape", "[%s] -> [%s]\n",
				      tip->playing ? "PLAYING" : "PAUSED",
				      play ? "PLAYING" : "PAUSED");
		}
	}
	tip->playing = play;
	update_motor(tip);
}

// Called when machine's tape output level changes.

void tape_update_output(struct tape_interface *ti, uint8_t value) {
	struct tape_interface_private *tip = (struct tape_interface_private *)ti;
	if (tip->motor && tip->playing && ti->tape_output && !tip->tape_rewrite) {
		int length = event_current_tick - ti->tape_output->last_write_cycle;
		ti->tape_output->module->sample_out(ti->tape_output, tip->last_tape_output, length);
		ti->tape_output->last_write_cycle = event_current_tick;
	}
	tip->last_tape_output = value;
}

// Update flags

static void tape_ui_set_tape_flag(void *sptr, int tag, void *smsg) {
	struct tape_interface_private *tip = sptr;
	struct ui_state_message *uimsg = smsg;
	switch (tag) {
	case ui_tag_tape_flag_fast:
		tip->tape_fast = ui_msg_adjust_value_range(uimsg, tip->tape_fast, 1,
							   0, 1, UI_ADJUST_FLAG_CYCLE);
		break;
	case ui_tag_tape_flag_pad_auto:
		tip->tape_pad_auto = ui_msg_adjust_value_range(uimsg, tip->tape_pad_auto, 1,
							       0, 1, UI_ADJUST_FLAG_CYCLE);
		break;
	case ui_tag_tape_flag_rewrite:
		tip->tape_rewrite = ui_msg_adjust_value_range(uimsg, tip->tape_rewrite, 0,
							      0, 1, 0);
		break;
	default: break;
	}
	set_breakpoints(tip);
}

// Update events & breakpoints based on current motor state.

static void update_motor(struct tape_interface_private *tip) {
	struct tape_interface *ti = &tip->public;
	_Bool running = tip->motor && tip->playing;
	if (running) {
		if (ti->tape_input && !tip->waggle_event.queued) {
			// If motor running and tape file attached, enable the
			// tape input bit waggler.
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
			rewrite_tape_desync(tip, xroar.cfg.tape.rewrite_leader);
		}
	}
	set_breakpoints(tip);
}

// Read pulse & duration, schedule next read.

static void waggle_bit(void *sptr) {
	struct tape_interface_private *tip = sptr;
	struct tape_interface *ti = &tip->public;
	tip->in_pulse = tape_pulse_in(ti->tape_input, &tip->in_pulse_width);
	switch (tip->in_pulse) {
	default:
	case -1:
		DELEGATE_CALL(ti->update_audio, 0.5);
		event_dequeue(&tip->waggle_event);
		ui_update_state(tip->msgr_client_id, ui_tag_tape_motor, -1, NULL);
		if (ti->default_paused) {
			ui_update_state(-1, ui_tag_tape_playing, 0, NULL);
		}
		return;
	case 0:
		DELEGATE_CALL(ti->update_audio, 0.0);
		break;
	case 1:
		DELEGATE_CALL(ti->update_audio, 1.0);
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
	if (tip->motor && tip->playing) {
		tip->flush_event.at_tick += EVENT_MS(500);
		event_queue(&MACHINE_EVENT_LIST, &tip->flush_event);
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Fake CPU helpers.  For fast loading, we set up ROM intercepts and then run
// equivalent code outside emulation.  These functions let us get the
// appropriate behaviour by dummying CPU operations without actually running
// the CPU.

// CPU operation equivalents

#define CC_H (0x20)
#define CC_N (0x08)
#define CC_Z (0x04)
#define CC_V (0x02)
#define CC_C (0x01)

// AND with...

#define M_HNZVC ( ~(CC_H | CC_N | CC_Z | CC_V | CC_C) )
#define M_NZ    ( ~(CC_N | CC_Z) )
#define M_NZV   ( ~(CC_N | CC_Z | CC_V) )
#define M_NZVC  ( ~(CC_N | CC_Z | CC_V | CC_C) )
#define M_Z     ( ~(CC_Z) )
#define M_NZC   ( ~(CC_N | CC_Z | CC_C) )
#define M_NVC   ( ~(CC_N | CC_V | CC_C) )
#define M_ZC    ( ~(CC_Z | CC_C) )

// Then OR with...

#define T_Z8(r)         ( (0 == ((r) & 0xff))   ? CC_Z : 0 )
#define T_Z16(r)        ( (0 == ((r) & 0xffff)) ? CC_Z : 0 )
#define T_N8(r)         ( (((r) >> 4)  & CC_N) )
#define T_N16(r)        ( (((r) >> 12) & CC_N) )
#define T_H(a,b,r)      ( ((((a) ^ (b) ^ (r)) << 1) & CC_H) )
#define T_C8(r)         ( (((r) >> 8)  & CC_C) )
#define T_C16(r)        ( (((r) >> 16) & CC_C) )
#define T_V8(a,b,r)     ( ((((a) ^ (b) ^ (r) ^ ((r) >> 1)) >> 6)  & CC_V) )
#define T_V16(a,b,r)    ( ((((a) ^ (b) ^ (r) ^ ((r) >> 1)) >> 14) & CC_V) )
#define T_NZ8(r)        ( T_N8(r)  | T_Z8( (r) & 0xff) )
#define T_NZ16(r)       ( T_N16(r) | T_Z16((r) & 0xffff) )
#define T_NZC8(r)       ( T_N8(r)  | T_Z8( (r) & 0xff)   | T_C8(r) )
#define T_NZC16(r)      ( T_N16(r) | T_Z16((r) & 0xffff) | T_C16(r) )
#define T_NZV8(a,b,r)   ( T_N8(r)  | T_Z8( (r) & 0xff)   | T_V8(a,b,r) )
#define T_NZVC8(a,b,r)  ( T_N8(r)  | T_Z8( (r) & 0xff)   | T_V8(a,b,r)  | T_C8(r) )
#define T_NZVC16(a,b,r) ( T_N16(r) | T_Z16((r) & 0xffff) | T_V16(a,b,r) | T_C16(r) )

// Register access

static inline uint8_t GET_CC(struct tape_interface_private *tip) {
	if (tip->is_6809) {
		return ((struct MC6809 *)tip->debug_cpu)->reg_cc;
	}
	if (tip->is_6803) {
		return ((struct MC6801 *)tip->debug_cpu)->reg_cc;
	}
	return 0;
}

static inline uint8_t SET_CC(struct tape_interface_private *tip, uint8_t v) {
	if (tip->is_6809) {
		((struct MC6809 *)tip->debug_cpu)->reg_cc = v;
	}
	if (tip->is_6803) {
		((struct MC6801 *)tip->debug_cpu)->reg_cc = v | 0xc0;
	}
	return 0;
}

static inline uint8_t SET_A(struct tape_interface_private *tip, uint8_t v) {
	if (tip->is_6809) {
		MC6809_REG_A(((struct MC6809 *)tip->debug_cpu)) = v;
	}
	if (tip->is_6803) {
		MC6801_REG_A(((struct MC6801 *)tip->debug_cpu)) = v;
	}
	return 0;
}

// Operations affecting flags

static uint8_t op_add(struct tape_interface_private *tip, uint8_t v1, uint8_t v2) {
	uint8_t cc = GET_CC(tip);
	unsigned int v = v1 + v2;
	cc = (cc & M_HNZVC) | T_NZVC8(v1, v2, v) | T_H(v1, v2, v);
	SET_CC(tip, cc);
	return v;
}

static uint8_t op_sub(struct tape_interface_private *tip, uint8_t v1, uint8_t v2) {
	uint8_t cc = GET_CC(tip);
	unsigned int v = v1 - v2;
	cc = (cc & M_NZVC) | T_NZVC8(v1, v2, v);
	SET_CC(tip, cc);
	return v;
}

static uint8_t op_clr(struct tape_interface_private *tip) {
	uint8_t cc = GET_CC(tip);
	cc = (cc & M_NVC) | CC_Z;
	SET_CC(tip, cc);
	return 0;
}

#define SKIP_CPU_CYCLES(tip,n) (tip->skip_ncycles += (n))

#define FAKE_BHI(tip) (SKIP_CPU_CYCLES(tip, 3), !(GET_CC(tip) & (CC_Z | CC_C)))
#define FAKE_BLS(tip) (!FAKE_BHI(tip))
#define FAKE_BCC(tip) (SKIP_CPU_CYCLES(tip, 3), !(GET_CC(tip) & CC_C))
#define FAKE_BHS(tip) (FAKE_BCC(tip))
#define FAKE_BCS(tip) (!FAKE_BCC(tip))
#define FAKE_BLO(tip) (FAKE_BCS(tip))
#define FAKE_BNE(tip) (SKIP_CPU_CYCLES(tip, 3), !(GET_CC(tip) & CC_Z))
#define FAKE_BRA(tip) (SKIP_CPU_CYCLES(tip, 3))

static void FAKE_BSR(struct tape_interface_private *tip,
		void (*f)(struct tape_interface_private *)) {
	SKIP_CPU_CYCLES(tip, tip->is_6809 ? 7 : 6);
	f(tip);
}

// Memory accesses

static uint8_t mem_read8(struct tape_interface_private *tip, uint16_t A) {
	return tip->machine->read_byte(tip->machine, A, 0);
}

static uint16_t mem_read16(struct tape_interface_private *tip, uint16_t A) {
	uint16_t r = mem_read8(tip, A) << 8;
	r |= mem_read8(tip, A + 1);
	return r;
}

static void mem_write8(struct tape_interface_private *tip, uint16_t A, uint8_t D) {
	tip->machine->write_byte(tip->machine, A, D);
}

// Timing-only instruction equivalents

#define FAKE_RTS(tip) do { \
		SKIP_CPU_CYCLES(tip, 5); \
	} while (0)

#define FAKE_CLR_DIRECT(tip,a) do { \
		SKIP_CPU_CYCLES(tip, 6); \
		mem_write8((tip), (a), 0); \
	} while (0)

#define FAKE_DEC_DIRECT(tip,a) do { \
		SKIP_CPU_CYCLES(tip, 6); \
		uint8_t D = mem_read8((tip), (a)) - 1; \
		mem_write8((tip), (a), D); \
	} while (0)

#define FAKE_INC_DIRECT(tip,a) do { \
		SKIP_CPU_CYCLES(tip, 6); \
		uint8_t D = mem_read8((tip), (a)) + 1; \
		mem_write8((tip), (a), D); \
	} while (0)

// ---------------------------------------------------------------------------

// Fast tape reading

// A set of breakpoint handlers that replace ROM routines, avoiding the need to
// emulate the CPU.
//
// The MC-10 ROM performs pretty much the same operations - at least, they're
// similar enough that just subbing in MC-10 addresses makes this work for that
// machine too.

// Helpers used by ROM intercepts.

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
			ui_update_state(tip->msgr_client_id, ui_tag_tape_motor, -1, NULL);
			return;
		}
	}
	tip->in_pulse_width -= skip;
	tip->waggle_event.at_tick = event_current_tick + tip->in_pulse_width;
	event_queue(&MACHINE_EVENT_LIST, &tip->waggle_event);
	DELEGATE_CALL(ti->update_audio, tip->in_pulse ? 1.0 : 0.0);
}

// Apply accumulated time skip to read state

static void do_skip_read_time(struct tape_interface_private *tip) {
	advance_read_time(tip, tip->skip_ncycles * EVENT_TICKS_14M31818(16));
	tip->skip_ncycles = 0;
}

// Update read time based on how far into current pulse we are

static void update_read_time(struct tape_interface_private *tip) {
	event_ticks skip = tip->waggle_event.at_tick - event_current_tick;
	int s = event_tick_delta(tip->in_pulse_width, skip);
	if (s >= 0) {
		advance_read_time(tip, s);
	}
}

// Called by fast_motor_on().  Skipped if a short leader is detected.

static void motor_on_delay(struct tape_interface_private *tip) {
	struct MC6809 *cpu09 = (struct MC6809 *)tip->debug_cpu;
	SKIP_CPU_CYCLES(tip, 5);  /* LDX <$95 */
	uint16_t X = mem_read16(tip, tip->addr.motor_delay);
	SKIP_CPU_CYCLES(tip, tip->initial_motor_delay);  /* LBRA delay_X */
	for (; X; --X) {
		SKIP_CPU_CYCLES(tip, 5);  /* LEAX -1,X */
		SKIP_CPU_CYCLES(tip, 3);  /* BNE delay_X */
		/* periodically sync up tape position */
		if ((X & 63) == 0)
			do_skip_read_time(tip);
	}
	cpu09->reg_x = 0;
	cpu09->reg_cc |= CC_Z;
	FAKE_RTS(tip);
}

// Sample the cassette port input.  The input is inverted so a positive signal
// results in CC.C clear, or set if negative.

static void sample_cas(struct tape_interface_private *tip) {
	FAKE_INC_DIRECT(tip, tip->addr.pwcount);
	SKIP_CPU_CYCLES(tip, 5);  /* LDB >$FF20 */
	do_skip_read_time(tip);
	SKIP_CPU_CYCLES(tip, 2);  /* RORB */
	FAKE_RTS(tip);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// ROM intercepts.  Generally running the same algorithms outside emulation.
// We maintain a counter of skipped CPU cycles to sync the tape to the right
// position without actually running the CPU.

static void tape_wait_p0(struct tape_interface_private *tip) {
	do {
		FAKE_BSR(tip, sample_cas);
		if (tip->in_pulse < 0) return;
		SKIP_CPU_CYCLES(tip, 3);  /* BCS tape_wait_p0 */
	} while (!tip->in_pulse);
	FAKE_RTS(tip);
}

static void tape_wait_p1(struct tape_interface_private *tip) {
	do {
		FAKE_BSR(tip, sample_cas);
		if (tip->in_pulse < 0) return;
		SKIP_CPU_CYCLES(tip, 3);  /* BCC tape_wait_p1 */
	} while (tip->in_pulse);
	FAKE_RTS(tip);
}

static void tape_wait_p0_p1(struct tape_interface_private *tip) {
	FAKE_BSR(tip, tape_wait_p0);
	if (tip->in_pulse < 0) return;
	tape_wait_p1(tip);
}

static void tape_wait_p1_p0(struct tape_interface_private *tip) {
	FAKE_BSR(tip, tape_wait_p1);
	if (tip->in_pulse < 0) return;
	tape_wait_p0(tip);
}

// Check measured cycle width against thresholds.  Clears bcount (number of
// leader bits so far) if too long.  Otherwise flags set as result of comparing
// against minpw1200.

static void L_BDC3(struct tape_interface_private *tip) {
	SKIP_CPU_CYCLES(tip, 4);  /* LDB <$82 */
	uint8_t B = mem_read8(tip, tip->addr.pwcount);
	SKIP_CPU_CYCLES(tip, 4);  /* CMPB <$94 */
	op_sub(tip, B, mem_read8(tip, tip->addr.maxpw1200));
	if (FAKE_BHI(tip)) {  // BHI L_BDCC
		FAKE_CLR_DIRECT(tip, tip->addr.bcount);
		op_clr(tip);
		FAKE_RTS(tip);
		return;
	}
	SKIP_CPU_CYCLES(tip, 4);  /* CMPB <$93 */
	op_sub(tip, B, mem_read8(tip, tip->addr.minpw1200));
	FAKE_RTS(tip);
}

static void tape_cmp_p1_1200(struct tape_interface_private *tip) {
	FAKE_CLR_DIRECT(tip, tip->addr.pwcount);
	FAKE_BSR(tip, tape_wait_p0);
	if (tip->in_pulse < 0) return;
	FAKE_BRA(tip);  /* BRA L_BDC3 */
	L_BDC3(tip);
}

static void tape_cmp_p0_1200(struct tape_interface_private *tip) {
	FAKE_CLR_DIRECT(tip, tip->addr.pwcount);
	FAKE_BSR(tip, tape_wait_p1);
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
	int phase = 0;
	int state = L_BDED;
	_Bool done = 0;

	while (!done && tip->in_pulse >= 0) {
		switch (state) {
		case L_BDED:
			FAKE_BSR(tip, tape_wait_p0_p1);
			state = L_BDEF;  // fall through to L_BDEF
			break;

		case L_BDEF:
			FAKE_BSR(tip, tape_cmp_p1_1200);
			if (FAKE_BHI(tip)) {
				state = L_BDFF;  // BHI L_BDFF
				break;
			}
			state = L_BDF3;  // fall through to L_BDF3
			break;

		case L_BDF3:
			FAKE_BSR(tip, tape_cmp_p0_1200);
			if (FAKE_BLO(tip)) {
				state = L_BE03;  // BLO L_BE03
				break;
			}
			FAKE_INC_DIRECT(tip, tip->addr.bcount);
			SKIP_CPU_CYCLES(tip, 4);  // LDA <$83
			SKIP_CPU_CYCLES(tip, 2);  // CMPA #$60
			phase = mem_read8(tip, tip->addr.bcount);
			op_sub(tip, phase, 0x60);
			FAKE_BRA(tip);
			state = L_BE0D;  // BRA L_BE0D
			break;

		case L_BDFF:
			FAKE_BSR(tip, tape_cmp_p0_1200);
			if (FAKE_BHI(tip)) {
				state = L_BDEF;  // BHI L_BDEF
				break;
			}
			state = L_BE03;  // fall through to L_BE03
			break;

		case L_BE03:
			FAKE_BSR(tip, tape_cmp_p1_1200);
			if (FAKE_BCS(tip)) {
				state = L_BDF3;  // BCS L_BDF3
				break;
			}
			FAKE_DEC_DIRECT(tip, tip->addr.bcount);
			SKIP_CPU_CYCLES(tip, 4);  // LDA <$83
			SKIP_CPU_CYCLES(tip, 2);  // ADDA #$60
			phase = op_add(tip, mem_read8(tip, tip->addr.bcount), 0x60);
			state = L_BE0D;
			break;

		case L_BE0D:
			if (FAKE_BNE(tip)) {
				state = L_BDED;  // BNE L_BDED
				break;
			}
			SKIP_CPU_CYCLES(tip, 4);  // STA <$84
			mem_write8(tip, tip->addr.bphase, phase);
			FAKE_RTS(tip);
			done = 1;
			break;
		}
	}
}

static void tape_wait_2p(struct tape_interface_private *tip) {
	FAKE_CLR_DIRECT(tip, tip->addr.pwcount);
	SKIP_CPU_CYCLES(tip, 6);  /* TST <$84 */
	SKIP_CPU_CYCLES(tip, 3);  /* BNE tape_wait_p1_p0 */
	if (mem_read8(tip, tip->addr.bphase)) {
		tape_wait_p1_p0(tip);
	} else {
		tape_wait_p0_p1(tip);
	}
}

static void bitin(struct tape_interface_private *tip) {
	FAKE_BSR(tip, tape_wait_2p);
	SKIP_CPU_CYCLES(tip, 4);  /* LDB <$82 */
	uint8_t B = mem_read8(tip, tip->addr.pwcount);
	SKIP_CPU_CYCLES(tip, 2);  /* DECB */
	--B;
	SKIP_CPU_CYCLES(tip, 4);  /* CMPB <$92 */
	op_sub(tip, B, mem_read8(tip, tip->addr.mincw1200));
	FAKE_RTS(tip);
}

static void cbin(struct tape_interface_private *tip) {
	int bin = 0;
	SKIP_CPU_CYCLES(tip, 2);  /* LDA #$08 */
	SKIP_CPU_CYCLES(tip, 4);  /* STA <$83 */
	for (int i = 8; i; i--) {
		FAKE_BSR(tip, bitin);
		SKIP_CPU_CYCLES(tip, 2);  // RORA
		bin >>= 1;
		bin |= (GET_CC(tip) & 0x01) ? 0x80 : 0;
		SKIP_CPU_CYCLES(tip, 6);  // DEC <$83
		SKIP_CPU_CYCLES(tip, 3);  // BNE $BDB1
	}
	FAKE_RTS(tip);
	SET_A(tip, bin);
	mem_write8(tip, tip->addr.bcount, 0);
}

// fast_motor_on() skips the standard delay if a short leader was detected
// (usually old CAS files).

static void fast_motor_on(void *sptr) {
	// Breakpoint: CASON, after switching on, before delay
	struct tape_interface_private *tip = sptr;
	struct MC6809 *cpu09 = (struct MC6809 *)tip->debug_cpu;

	update_read_time(tip);
	if (!tip->short_leader) {
		motor_on_delay(tip);
	} else {
		cpu09->reg_x = 0;
		cpu09->reg_cc |= CC_Z;
	}
	tip->machine->op_rts(tip->machine);
	do_skip_read_time(tip);
}

// Similarly, fast_sync_leader() just assumes leader has been sensed

static void fast_sync_leader(void *sptr) {
	// Breakpoint: CSRDON, after calling CASON
	struct tape_interface_private *tip = sptr;

	update_read_time(tip);
	if (!tip->short_leader) {
		sync_leader(tip);
	}
	tip->machine->op_rts(tip->machine);
	do_skip_read_time(tip);
}

// If tape was paused, breakpoints would not have been in place, meaning this
// code can be reached during initial silence.

static void fast_tape_p0_wait_p1(void *sptr) {
	// Breakpoint: waiting for high pulse
	struct tape_interface_private *tip = sptr;

	update_read_time(tip);
	tape_wait_p1(tip);
	tip->machine->op_rts(tip->machine);
	do_skip_read_time(tip);
}

static void fast_bitin(void *sptr) {
	// Breakpoint: BITIN
	struct tape_interface_private *tip = sptr;

	update_read_time(tip);
	bitin(tip);
	tip->machine->op_rts(tip->machine);
	do_skip_read_time(tip);
	if (tip->tape_rewrite) {
		rewrite_bitin(tip);
	}
}

static void fast_cbin(void *sptr) {
	// Breakpoint: CBIN
	struct tape_interface_private *tip = sptr;

	if (tip->tape_rewrite) {
		// If rewriting, we allow the ROM to keep calling BITIN and
		// rely on fast_bitin() to do the work
		return;
	}

	update_read_time(tip);
	cbin(tip);
	tip->machine->op_rts(tip->machine);
	do_skip_read_time(tip);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Tape rewriting

// These functions are only ever called after checking that rewrite is enabled,
// or from a breakpoint only set for rewrite.

// Flags tape rewrite stream as desynced.  At this point, some amount of leader
// bytes are expected followed by a sync byte, at which point rewriting will
// continue.  Long leader follows motor off/on, short leader follows normal
// data block.

static void rewrite_tape_desync(struct tape_interface_private *tip, int leader) {
	struct tape_interface *ti = &tip->public;

	// pad last byte with trailer pattern
	while (tip->rewrite.bit_count) {
		tape_bit_out(ti->tape_output, ~tip->rewrite.bit_count & 1);
	}

	// one byte of trailer before any silence (but not following silence)
	if (leader > 0 && !tip->rewrite.silence) {
		tape_byte_out(ti->tape_output, 0x55);
		leader--;
	}

	// desync tape rewrite - will continue once a sync byte is read
	tip->rewrite.have_sync = 0;
	tip->rewrite.leader_count = leader;
	if (leader > 2) {
		tip->rewrite.have_pulse_widths = 0;
	}
}

// When a sync byte is encountered, rewrite an appropriate length of leader
// followed by the sync byte.  Flag stream as in sync - subsequent bits will be
// rewritten verbatim.

static void rewrite_sync(void *sptr) {
	// Breakpoint: BLKIN, having read sync byte $3C
	struct tape_interface_private *tip = sptr;
	struct tape_interface *ti = &tip->public;

	// Already synced?  Nothing to do.
	if (tip->rewrite.have_sync) {
		return;
	}

	// Scan pulse buffer to determine average pulse widths.
	if (!tip->rewrite.have_pulse_widths) {
		int bit0_pwt;
		int bit1_pwt;
		// Can use int_split_inplace here, as we don't care about the
		// contents of this buffer (it's all leader pulses that will be
		// rewritten consistently anyway).
		int_split_inplace(tip->rewrite.pulse_buffer, PULSE_BUFFER_SIZE, &bit1_pwt, &bit0_pwt);
		if (bit0_pwt <= 0)
			bit0_pwt = (bit1_pwt > 0) ? bit1_pwt * 2 : 6403;
		if (bit1_pwt <= 0)
			bit1_pwt = (bit0_pwt >= 2) ? bit0_pwt / 2 : 3489;

		tip->rewrite.bit0_pwt = bit0_pwt;
		tip->rewrite.bit1_pwt = bit1_pwt;
		tip->rewrite.have_pulse_widths = 1;
	}

	for (int i = 0; i < tip->rewrite.leader_count; ++i) {
		tape_byte_out(ti->tape_output, 0x55);
	}
	tape_byte_out(ti->tape_output, 0x3c);
	tip->rewrite.have_sync = 1;
}

// Rewrites bits returned by the BITIN routine, but only while flagged as
// synced.

static void rewrite_bitin(void *sptr) {
	// Breakpoint: RTS from BITIN
	struct tape_interface_private *tip = sptr;
	struct tape_interface *ti = &tip->public;

	// Must by synced
	if (!tip->rewrite.have_sync) {
		return;
	}

	tape_bit_out(ti->tape_output, GET_CC(tip) & 0x01);
}

// When tape motor turned on, rewrite a standard duration of silence and flag
// the stream as desynced, expecting a long leader before the next block.

static void rewrite_tape_on(void *sptr) {
	// Breakpoint: CSRDON
	struct tape_interface_private *tip = sptr;
	struct tape_interface *ti = &tip->public;

	// Desync with long leader
	rewrite_tape_desync(tip, xroar.cfg.tape.rewrite_leader);

	if (ti->tape_output) {
		tape_sample_out(ti->tape_output, 0x80, EVENT_MS(xroar.cfg.tape.rewrite_gap_ms));
		tip->rewrite.silence = 1;
	}
}

// When finished reading a block, flag the stream as desynced expecting a short
// intra-block leader before the next.

static void rewrite_end_of_block(void *sptr) {
	// Breakpoint: BLKIN, having confirmed checksum
	struct tape_interface_private *tip = sptr;

	// desync with short inter-block leader
	rewrite_tape_desync(tip, 2);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Update applied breakpoints

// These fast loading intercepts are needed for "short leader" padding.

static struct machine_bp bp_list_pad[] = {
	BP_DRAGON_ROM(.address = 0xbdd7, .handler = DELEGATE_INIT(fast_motor_on, NULL) ),
	BP_COCO_ROM(.address = 0xa7d1, .handler = DELEGATE_INIT(fast_motor_on, NULL) ),
	BP_COCO3_ROM(.address = 0xa7d1, .handler = DELEGATE_INIT(fast_motor_on, NULL) ),
	BP_DRAGON_ROM(.address = 0xbded, .handler = DELEGATE_INIT(fast_sync_leader, NULL) ),
	BP_COCO_ROM(.address = 0xa782, .handler = DELEGATE_INIT(fast_sync_leader, NULL) ),
	BP_COCO3_ROM(.address = 0xa782, .handler = DELEGATE_INIT(fast_sync_leader, NULL) ),
	BP_MC10_ROM(.address = 0xff53, .handler = DELEGATE_INIT(fast_sync_leader, NULL) ),
};

// Fast tape loading intercepts various ROM calls and uses equivalents provided
// here to bypass the need for CPU emulation.

static struct machine_bp bp_list_fast[] = {
	BP_DRAGON_ROM(.address = 0xbd99, .handler = DELEGATE_INIT(fast_tape_p0_wait_p1, NULL) ),
	BP_COCO_ROM(.address = 0xa769, .handler = DELEGATE_INIT(fast_tape_p0_wait_p1, NULL) ),
	BP_COCO3_ROM(.address = 0xa769, .handler = DELEGATE_INIT(fast_tape_p0_wait_p1, NULL) ),
	BP_MC10_ROM(.address = 0xff38, .handler = DELEGATE_INIT(fast_tape_p0_wait_p1, NULL) ),
	BP_DRAGON_ROM(.address = 0xbda5, .handler = DELEGATE_INIT(fast_bitin, NULL) ),
	BP_COCO_ROM(.address = 0xa755, .handler = DELEGATE_INIT(fast_bitin, NULL) ),
	BP_COCO3_ROM(.address = 0xa755, .handler = DELEGATE_INIT(fast_bitin, NULL) ),
	BP_MC10_ROM(.address = 0xff22, .handler = DELEGATE_INIT(fast_bitin, NULL) ),
	BP_DRAGON_ROM(.address = 0xbdad, .handler = DELEGATE_INIT(fast_cbin, NULL) ),
	BP_COCO_ROM(.address = 0xa749, .handler = DELEGATE_INIT(fast_cbin, NULL) ),
	BP_COCO3_ROM(.address = 0xa749, .handler = DELEGATE_INIT(fast_cbin, NULL) ),
	BP_MC10_ROM(.address = 0xff14, .handler = DELEGATE_INIT(fast_cbin, NULL) ),
};

// Tape rewriting intercepts the returns from various ROM calls to interpret
// the loading state - whether a leader is expected, etc.

static struct machine_bp bp_list_rewrite[] = {
	BP_DRAGON_ROM(.address = 0xb94d, .handler = DELEGATE_INIT(rewrite_sync, NULL) ),
	BP_COCO_ROM(.address = 0xa719, .handler = DELEGATE_INIT(rewrite_sync, NULL) ),
	BP_COCO3_ROM(.address = 0xa719, .handler = DELEGATE_INIT(rewrite_sync, NULL) ),
	BP_MC10_ROM(.address = 0xfecc, .handler = DELEGATE_INIT(rewrite_sync, NULL) ),
	BP_DRAGON_ROM(.address = 0xbdac, .handler = DELEGATE_INIT(rewrite_bitin, NULL) ),
	BP_COCO_ROM(.address = 0xa75c, .handler = DELEGATE_INIT(rewrite_bitin, NULL) ),
	BP_COCO3_ROM(.address = 0xa75c, .handler = DELEGATE_INIT(rewrite_bitin, NULL) ),
	BP_MC10_ROM(.address = 0xff2b, .handler = DELEGATE_INIT(rewrite_bitin, NULL) ),
	BP_DRAGON_ROM(.address = 0xbdeb, .handler = DELEGATE_INIT(rewrite_tape_on, NULL) ),
	BP_COCO_ROM(.address = 0xa780, .handler = DELEGATE_INIT(rewrite_tape_on, NULL) ),
	BP_COCO3_ROM(.address = 0xa780, .handler = DELEGATE_INIT(rewrite_tape_on, NULL) ),
	BP_MC10_ROM(.address = 0xff50, .handler = DELEGATE_INIT(rewrite_tape_on, NULL) ),
	BP_DRAGON_ROM(.address = 0xb97e, .handler = DELEGATE_INIT(rewrite_end_of_block, NULL) ),
	BP_COCO_ROM(.address = 0xa746, .handler = DELEGATE_INIT(rewrite_end_of_block, NULL) ),
	BP_COCO3_ROM(.address = 0xa746, .handler = DELEGATE_INIT(rewrite_end_of_block, NULL) ),
	BP_MC10_ROM(.address = 0xff10, .handler = DELEGATE_INIT(rewrite_end_of_block, NULL) ),
};

static void set_breakpoints(struct tape_interface_private *tip) {
	if (!tip->debug_cpu)
		return;
	// clear any old breakpoints
	machine_bp_remove_list(tip->machine, bp_list_pad);
	machine_bp_remove_list(tip->machine, bp_list_fast);
	machine_bp_remove_list(tip->machine, bp_list_rewrite);
	if (!tip->motor || !tip->playing) {
		return;
	}
	// Don't intercept calls if there's no input tape.  The optimisations
	// are only for reading.  Also, this helps work around missing
	// silences...
	if (!tip->public.tape_input) {
		return;
	}
	// Add required breakpoints
	if ((tip->short_leader && tip->tape_pad_auto) || tip->tape_fast) {
		machine_bp_add_list(tip->machine, bp_list_pad, tip);
		if (tip->tape_fast) {
			machine_bp_add_list(tip->machine, bp_list_fast, tip);
		}
	}
	if (tip->tape_rewrite) {
		machine_bp_add_list(tip->machine, bp_list_rewrite, tip);
	}
}
