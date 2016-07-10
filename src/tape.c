/*  Copyright 2003-2016 Ciaran Anscomb
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
	struct keyboard_interface *keyboard_interface;
	struct MC6809 *cpu;

	_Bool tape_fast;
	_Bool tape_pad;
	_Bool tape_pad_auto;
	_Bool tape_rewrite;

	int in_pulse;
	int in_pulse_width;

	int ao_rate;

	uint8_t last_tape_output;
	_Bool motor;

	_Bool input_skip_sync;
	_Bool rewrite_have_sync;
	int rewrite_leader_count;
	int rewrite_bit_count;

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

/* Special case autorun instructions based on filename block size and CRC16 */

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
	  .run = "\003CLEAR20\\r\\0CLOADM\\r",
	},
	{
	  .name = "Lucifer's Kingdom",
	  .size = 15, .crc = 0x7f34,
	  .run = "\003CLEAR1,32767:CLOADM\\r",
	},
	{
	  .name = "North-Sea Action",
	  .size = 15, .crc = 0x9c2b,
	  .run = "\003CLEAR20\\r\\0CLOADM\\r\\0EXEC\\r",
	},
	{
	  .name = "Speak Up!",
	  .size = 15, .crc = 0x7bff,
	  .run = "\003CLEAR200,25448\\r\\0CLOADM\\r\\0EXEC\\r",
	},
	{
	  .name = "Spy Against Spy",
	  .size = 15, .crc = 0x48a0,
	  .run = "\003CLEAR20:CLOADM\\r",
	},
	{
	  .name = "Tanglewood",
	  .size = 115, .crc = 0x7e5e,
	  .run = "\003CLEAR10\\r\\0CLOADM\\r",
	},
	{
	  .name = "Utopia",
	  .size = 15, .crc = 0xeb14,
	  .run = "\003CLEAR10:CLOADM\\r\\0EXEC\\r",
	},
};

/**************************************************************************/

/* For now, creating a tape interface requires a pointer to the CPU.  This
 * should probably become a pointer to the machine it's a part of. */

struct tape_interface *tape_interface_new(void) {
	struct tape_interface_private *tip = xmalloc(sizeof(*tip));
	*tip = (struct tape_interface_private){0};
	struct tape_interface *ti = &tip->public;

	tip->in_pulse = -1;
	tip->ao_rate = 9600;
	tip->rewrite_leader_count = 256;

	tape_interface_disconnect_machine(ti);

	event_init(&tip->waggle_event, DELEGATE_AS0(void, waggle_bit, tip));
	event_init(&tip->flush_event, DELEGATE_AS0(void, flush_output, tip));

	return &tip->public;
}

void tape_interface_free(struct tape_interface *ti) {
	struct tape_interface_private *tip = (struct tape_interface_private *)ti;
	tape_close_reading(ti);
	tape_reset(ti);
	free(tip);
}

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
	/* if seeking to beginning of tape, ensure any fake leader etc.
	 * is set up properly */
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

/* Tape waveform is similar to the one in ROM, but higher precision, offset
 * slightly, with peaks reduced. */

static uint8_t const bit_out_waveform[] = {
	0x82, 0x97, 0xab, 0xbd, 0xce, 0xdc, 0xe8, 0xf0,
	0xf5, 0xf6, 0xf4, 0xee, 0xe5, 0xd9, 0xca, 0xb9,
	0xa6, 0x92, 0x7e, 0x69, 0x55, 0x43, 0x32, 0x24,
	0x18, 0x10, 0x0b, 0x0a, 0x0c, 0x12, 0x1b, 0x27,
	0x36, 0x47, 0x5a, 0x6e
};

static void tape_bit_out(struct tape *t, int bit) {
	if (!t) return;
	struct tape_interface *ti = t->tape_interface;
	struct tape_interface_private *tip = (struct tape_interface_private *)ti;
	int sample_length = bit ? 176 : 352;
	for (unsigned i = 0; i < ARRAY_N_ELEMENTS(bit_out_waveform); i++) {
		tape_sample_out(t, bit_out_waveform[i], sample_length);
	}
	tip->rewrite_bit_count = (tip->rewrite_bit_count + 1) & 7;
	tip->last_tape_output = 0;
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
	struct tape *new = xzalloc(sizeof(*new));
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
	tip->input_skip_sync = 0;
	int type = xroar_filetype_by_ext(filename);
	switch (type) {
	case FILETYPE_CAS:
		if ((ti->tape_input = tape_cas_open(ti, filename, "rb")) == NULL) {
			LOG_WARN("Failed to open '%s'\n", filename);
			return -1;
		}
		if (tip->tape_pad_auto) {
			int flags = tape_get_state(ti) & ~TAPE_PAD;
			if (tip->is_dragon && ti->tape_input->leader_count < 114)
				flags |= TAPE_PAD;
			if (!tip->is_dragon && ti->tape_input->leader_count < 130)
				flags |= TAPE_PAD;
			tape_select_state(ti, flags);
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
		if (tip->tape_pad_auto) {
			int flags = tape_get_state(ti) & ~TAPE_PAD;
			tape_select_state(ti, flags);
		}
		tip->input_skip_sync = 1;
		break;
#else
		LOG_WARN("Failed to open '%s'\n", filename);
		return -1;
#endif
	}
	if (ti->tape_input->module->set_channel_mode)
		ti->tape_input->module->set_channel_mode(ti->tape_input, xroar_cfg.tape_channel_mode);

	tape_desync(tip, 256);
	tape_update_motor(ti, tip->motor);
	LOG_DEBUG(1, "Tape: Attached '%s' for reading\n", filename);
	return 0;
}

void tape_close_reading(struct tape_interface *ti) {
	if (ti->tape_input)
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
	LOG_DEBUG(1, "Tape: Attached '%s' for writing.\n", filename);
	return 0;
}

void tape_close_writing(struct tape_interface *ti) {
	struct tape_interface_private *tip = (struct tape_interface_private *)ti;
	if (tip->tape_rewrite) {
		tape_byte_out(ti->tape_output, 0x55);
		tape_byte_out(ti->tape_output, 0x55);
	}
	if (ti->tape_output) {
		event_dequeue(&tip->flush_event);
		tape_update_output(ti, tip->last_tape_output);
		tape_close(ti->tape_output);
	}
	ti->tape_output = NULL;
}

/* Close any currently-open tape file, open a new one and read the first
 * bufferful of data.  Tries to guess the filetype.  Returns -1 on error,
 * 0 for a BASIC program, 1 for data and 2 for M/C. */
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

	/* Check list of known programs */
	for (unsigned i = 0; i < ARRAY_N_ELEMENTS(autorun_special); i++) {
		if (autorun_special[i].size == f->fnblock_size
		    && autorun_special[i].crc == f->fnblock_crc) {
			LOG_DEBUG(1, "Using special load instructions for '%s'\n", autorun_special[i].name);
			keyboard_queue_basic(tip->keyboard_interface, autorun_special[i].run);
			done = 1;
		}
	}

	/* Otherwise, use a simple heuristic: */
	if (!done) {
		_Bool need_exec = (type == 2 && f->load_address >= 0x01a9);

		switch (type) {
			case 0:
				keyboard_queue_basic(tip->keyboard_interface, "\003CLOAD\\r\\0RUN\\r");
				break;
			case 2:
				if (need_exec) {
					keyboard_queue_basic(tip->keyboard_interface, "\003CLOADM:EXEC\\r");
				} else {
					keyboard_queue_basic(tip->keyboard_interface, "\003CLOADM\\r");
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

/* Called whenever the motor control line is written to. */
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
		if (tip->tape_pad || tip->tape_rewrite) {
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

/* Called whenever the DAC is written to. */
void tape_update_output(struct tape_interface *ti, uint8_t value) {
	struct tape_interface_private *tip = (struct tape_interface_private *)ti;
	if (tip->motor && ti->tape_output && !tip->tape_rewrite) {
		int length = event_current_tick - ti->tape_output->last_write_cycle;
		ti->tape_output->module->sample_out(ti->tape_output, tip->last_tape_output, length);
		ti->tape_output->last_write_cycle = event_current_tick;
	}
	tip->last_tape_output = value;
}

/* Read pulse & duration, schedule next read */
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

/* ensure any "pulse" over 1/2 second long is flushed to output, so it doesn't
 * overflow any counters */
static void flush_output(void *sptr) {
	struct tape_interface_private *tip = sptr;
	struct tape_interface *ti = &tip->public;
	tape_update_output(ti, tip->last_tape_output);
	if (tip->motor) {
		tip->flush_event.at_tick += EVENT_MS(500);
		event_queue(&MACHINE_EVENT_LIST, &tip->flush_event);
	}
}

/* Fast tape */

static int pskip = 0;

static void do_pulse_skip(struct tape_interface_private *tip, int skip) {
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

static int pulse_skip(struct tape_interface_private *tip) {
	do_pulse_skip(tip, pskip * EVENT_SAM_CYCLES(16));
	pskip = 0;
	return tip->in_pulse;
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

#define BSR(f) do { pskip += 7; f(tip); } while (0)
#define RTS()  do { pskip += 5; } while (0)
#define CLR(a) do { pskip += 6; tip->machine->write_byte(tip->machine, (a), 0); } while (0)
#define DEC(a) do { pskip += 6; tip->machine->write_byte(tip->machine, (a), tip->machine->read_byte(tip->machine, a) - 1); } while (0)
#define INC(a) do { pskip += 6; tip->machine->write_byte(tip->machine, (a), tip->machine->read_byte(tip->machine, a) + 1); } while (0)

static void motor_on(struct tape_interface_private *tip) {
	int delay = tip->is_dragon ? 0x95 : 0x8a;
	pskip += 5;  /* LDX <$95 */
	int i = (tip->machine->read_byte(tip->machine, delay) << 8) | tip->machine->read_byte(tip->machine, delay+1);
	if (tip->is_dragon)
		pskip += 5;  /* LBRA delay_X */
	for (; i; i--) {
		pskip += 5;  /* LEAX -1,X */
		pskip += 3;  /* BNE delay_X */
		/* periodically sync up tape position */
		if ((i & 63) == 0)
			pulse_skip(tip);
	}
	tip->cpu->reg_x = 0;
	tip->cpu->reg_cc |= 0x04;
	RTS();
}

static void sample_cas(struct tape_interface_private *tip) {
	int pwcount = tip->is_dragon ? 0x82 : 0x83;
	INC(pwcount);
	pskip += 5;  /* LDB >$FF20 */
	pulse_skip(tip);
	pskip += 2;  /* RORB */
	if (tip->in_pulse) {
		tip->cpu->reg_cc &= ~1;
	} else {
		tip->cpu->reg_cc |= 1;
	}
	RTS();
}

static void tape_wait_p0(struct tape_interface_private *tip) {
	do {
		BSR(sample_cas);
		if (tip->in_pulse < 0) return;
		pskip += 3;  /* BCS tape_wait_p0 */
	} while (tip->cpu->reg_cc & 0x01);
	RTS();
}

static void tape_wait_p1(struct tape_interface_private *tip) {
	do {
		BSR(sample_cas);
		if (tip->in_pulse < 0) return;
		pskip += 3;  /* BCC tape_wait_p1 */
	} while (!(tip->cpu->reg_cc & 0x01));
	RTS();
}

static void tape_wait_p0_p1(struct tape_interface_private *tip) {
	BSR(tape_wait_p0);
	if (tip->in_pulse < 0) return;
	tape_wait_p1(tip);
}

static void tape_wait_p1_p0(struct tape_interface_private *tip) {
	BSR(tape_wait_p1);
	if (tip->in_pulse < 0) return;
	tape_wait_p0(tip);
}

static void L_BDC3(struct tape_interface_private *tip) {
	int pwcount = tip->is_dragon ? 0x82 : 0x83;
	int bcount = tip->is_dragon ? 0x83 : 0x82;
	int minpw1200 = tip->is_dragon ? 0x93 : 0x91;
	int maxpw1200 = tip->is_dragon ? 0x94 : 0x90;
	pskip += 4;  /* LDB <$82 */
	pskip += 4;  /* CMPB <$94 */
	op_sub(tip->cpu, tip->machine->read_byte(tip->machine, pwcount), tip->machine->read_byte(tip->machine, maxpw1200));
	pskip += 3;  /* BHI L_BDCC */
	if (!(tip->cpu->reg_cc & 0x05)) {
		CLR(bcount);
		op_clr(tip->cpu);
		RTS();
		return;
	}
	pskip += 4;  /* CMPB <$93 */
	op_sub(tip->cpu, tip->machine->read_byte(tip->machine, pwcount), tip->machine->read_byte(tip->machine, minpw1200));
	RTS();
}

static void tape_cmp_p1_1200(struct tape_interface_private *tip) {
	int pwcount = tip->is_dragon ? 0x82 : 0x83;
	CLR(pwcount);
	BSR(tape_wait_p0);
	if (tip->in_pulse < 0) return;
	pskip += 3;  /* BRA L_BDC3 */
	L_BDC3(tip);
}

static void tape_cmp_p0_1200(struct tape_interface_private *tip) {
	int pwcount = tip->is_dragon ? 0x82 : 0x83;
	CLR(pwcount);
	BSR(tape_wait_p1);
	if (tip->in_pulse < 0) return;
	L_BDC3(tip);
}

static void sync_leader(struct tape_interface_private *tip) {
	int bcount = tip->is_dragon ? 0x83 : 0x82;
	int store;
L_BDED:
	BSR(tape_wait_p0_p1);
	if (tip->in_pulse < 0) return;
L_BDEF:
	BSR(tape_cmp_p1_1200);
	if (tip->in_pulse < 0) return;
	pskip += 3;  /* BHI L_BDFF */
	if (!(tip->cpu->reg_cc & 0x05))
		goto L_BDFF;
L_BDF3:
	BSR(tape_cmp_p0_1200);
	if (tip->in_pulse < 0) return;
	pskip += 3;  /* BCS L_BE03 */
	if (tip->cpu->reg_cc & 0x01)
		goto L_BE03;
	INC(bcount);
	pskip += 4;  /* LDA <$83 */
	pskip += 2;  /* CMPA #$60 */
	store = tip->machine->read_byte(tip->machine, bcount);
	op_sub(tip->cpu, store, 0x60);
	pskip += 3;  /* BRA L_BE0D */
	goto L_BE0D;
L_BDFF:
	BSR(tape_cmp_p0_1200);
	if (tip->in_pulse < 0) return;
	pskip += 3;  /* BHI L_BDEF */
	if (!(tip->cpu->reg_cc & 0x05))
		goto L_BDEF;
L_BE03:
	BSR(tape_cmp_p1_1200);
	if (tip->in_pulse < 0) return;
	pskip += 3;  /* BCS L_BDF3 */
	if (tip->cpu->reg_cc & 0x01)
		goto L_BDF3;
	DEC(bcount);
	pskip += 4;  /* LDA <$83 */
	pskip += 2;  /* ADDA #$60 */
	store = op_add(tip->cpu, tip->machine->read_byte(tip->machine, bcount), 0x60);
L_BE0D:
	pskip += 3;  /* BNE L_BDED */
	if (!(tip->cpu->reg_cc & 0x04))
		goto L_BDED;
	pskip += 4;  /* STA <$84 */
	tip->machine->write_byte(tip->machine, 0x84, store);
	RTS();
}

static void tape_wait_2p(struct tape_interface_private *tip) {
	int pwcount = tip->is_dragon ? 0x82 : 0x83;
	CLR(pwcount);
	pskip += 6;  /* TST <$84 */
	pskip += 3;  /* BNE tape_wait_p1_p0 */
	if (tip->machine->read_byte(tip->machine, 0x84)) {
		tape_wait_p1_p0(tip);
	} else {
		tape_wait_p0_p1(tip);
	}
}

static void bitin(struct tape_interface_private *tip) {
	int pwcount = tip->is_dragon ? 0x82 : 0x83;
	int mincw1200 = tip->is_dragon ? 0x92 : 0x8f;
	BSR(tape_wait_2p);
	pskip += 4;  /* LDB <$82 */
	pskip += 2;  /* DECB */
	pskip += 4;  /* CMPB <$92 */
	op_sub(tip->cpu, tip->machine->read_byte(tip->machine, pwcount) - 1, tip->machine->read_byte(tip->machine, mincw1200));
	RTS();
}

static void cbin(struct tape_interface_private *tip) {
	int bcount = tip->is_dragon ? 0x83 : 0x82;
	int bin = 0;
	pskip += 2;  /* LDA #$08 */
	pskip += 4;  /* STA <$83 */
	for (int i = 8; i; i--) {
		BSR(bitin);
		pskip += 2;  /* RORA */
		bin >>= 1;
		bin |= (tip->cpu->reg_cc & 0x01) ? 0x80 : 0;
		pskip += 6;  /* DEC <$83 */
		pskip += 3;  /* BNE $BDB1 */
	}
	RTS();
	MC6809_REG_A(tip->cpu) = bin;
	tip->machine->write_byte(tip->machine, bcount, 0);
}

static void update_pskip(struct tape_interface_private *tip) {
	event_ticks skip = tip->waggle_event.at_tick - event_current_tick;
	skip = tip->in_pulse_width - skip;
	if (skip <= (EVENT_TICK_MAX/2)) {
		do_pulse_skip(tip, skip);
	}
}

static void fast_motor_on(void *sptr) {
	struct tape_interface_private *tip = sptr;
	update_pskip(tip);
	if (!tip->tape_pad) {
		motor_on(tip);
	}
	tip->machine->op_rts(tip->machine);
	pulse_skip(tip);
}

static void fast_sync_leader(void *sptr) {
	struct tape_interface_private *tip = sptr;
	update_pskip(tip);
	if (tip->tape_pad) {
		tip->machine->write_byte(tip->machine, 0x84, 0);
	} else {
		sync_leader(tip);
	}
	tip->machine->op_rts(tip->machine);
	pulse_skip(tip);
}

static void fast_bitin(void *sptr) {
	struct tape_interface_private *tip = sptr;
	update_pskip(tip);
	bitin(tip);
	tip->machine->op_rts(tip->machine);
	pulse_skip(tip);
	if (tip->tape_rewrite) rewrite_bitin(tip);
}

static void fast_cbin(void *sptr) {
	struct tape_interface_private *tip = sptr;
	update_pskip(tip);
	cbin(tip);
	tip->machine->op_rts(tip->machine);
	pulse_skip(tip);
}

/* Leader padding & tape rewriting */

static void tape_desync(struct tape_interface_private *tip, int leader) {
	struct tape_interface *ti = &tip->public;
	if (tip->tape_rewrite) {
		/* complete last byte */
		while (tip->rewrite_bit_count)
			tape_bit_out(ti->tape_output, 0);
		/* desync writing - pick up at next sync byte */
		tip->rewrite_have_sync = 0;
		tip->rewrite_leader_count = leader;
	}
}

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

static void rewrite_bitin(void *sptr) {
	/* RTS from BITIN */
	struct tape_interface_private *tip = sptr;
	struct tape_interface *ti = &tip->public;
	if (tip->tape_rewrite && tip->rewrite_have_sync) {
		tape_bit_out(ti->tape_output, tip->cpu->reg_cc & 0x01);
	}
}

static void rewrite_tape_on(void *sptr) {
	/* CSRDON */
	struct tape_interface_private *tip = sptr;
	/* desync with long leader */
	tape_desync(tip, 256);
	/* for audio files, when padding leaders, assume a phase */
	if (tip->tape_pad && tip->input_skip_sync) {
		tip->machine->write_byte(tip->machine, 0x84, 0);  /* phase */
		tip->machine->op_rts(tip->machine);
	}
}

static void rewrite_end_of_block(void *sptr) {
	/* BLKIN, having confirmed checksum */
	struct tape_interface_private *tip = sptr;
	/* desync with short inter-block leader */
	tape_desync(tip, 2);
}

/* Configuring tape options */

static struct machine_bp bp_list_fast[] = {
	BP_DRAGON_ROM(.address = 0xbdd7, .handler = DELEGATE_AS0(void, fast_motor_on, NULL) ),
	BP_COCO_ROM(.address = 0xa7d1, .handler = DELEGATE_AS0(void, fast_motor_on, NULL) ),
	BP_DRAGON_ROM(.address = 0xbded, .handler = DELEGATE_AS0(void, fast_sync_leader, NULL) ),
	BP_COCO_ROM(.address = 0xa782, .handler = DELEGATE_AS0(void, fast_sync_leader, NULL) ),
	BP_DRAGON_ROM(.address = 0xbda5, .handler = DELEGATE_AS0(void, fast_bitin, NULL) ),
	BP_COCO_ROM(.address = 0xa755, .handler = DELEGATE_AS0(void, fast_bitin, NULL) ),
};

static struct machine_bp bp_list_fast_cbin[] = {
	BP_DRAGON_ROM(.address = 0xbdad, .handler = DELEGATE_AS0(void, fast_cbin, NULL) ),
	BP_COCO_ROM(.address = 0xa749, .handler = DELEGATE_AS0(void, fast_cbin, NULL) ),
};

static struct machine_bp bp_list_rewrite[] = {
	BP_DRAGON_ROM(.address = 0xb94d, .handler = DELEGATE_AS0(void, rewrite_sync, NULL) ),
	BP_COCO_ROM(.address = 0xa719, .handler = DELEGATE_AS0(void, rewrite_sync, NULL) ),
	BP_DRAGON_ROM(.address = 0xbdac, .handler = DELEGATE_AS0(void, rewrite_bitin, NULL) ),
	BP_COCO_ROM(.address = 0xa75c, .handler = DELEGATE_AS0(void, rewrite_bitin, NULL) ),
	BP_DRAGON_ROM(.address = 0xbdeb, .handler = DELEGATE_AS0(void, rewrite_tape_on, NULL) ),
	BP_COCO_ROM(.address = 0xa780, .handler = DELEGATE_AS0(void, rewrite_tape_on, NULL) ),
	BP_DRAGON_ROM(.address = 0xb97e, .handler = DELEGATE_AS0(void, rewrite_end_of_block, NULL) ),
	BP_COCO_ROM(.address = 0xa746, .handler = DELEGATE_AS0(void, rewrite_end_of_block, NULL) ),
};

static void set_breakpoints(struct tape_interface_private *tip) {
	/* clear any old breakpoints */
	machine_bp_remove_list(tip->machine, bp_list_fast);
	machine_bp_remove_list(tip->machine, bp_list_fast_cbin);
	machine_bp_remove_list(tip->machine, bp_list_rewrite);
	if (!tip->motor)
		return;
	/* add required breakpoints */
	if (tip->tape_fast) {
		machine_bp_add_list(tip->machine, bp_list_fast, tip);
		/* these are incompatible with the other flags */
		if (!tip->tape_pad && !tip->tape_rewrite) {
			machine_bp_add_list(tip->machine, bp_list_fast_cbin, tip);
		}
	}
	if (tip->tape_pad || tip->tape_rewrite) {
		machine_bp_add_list(tip->machine, bp_list_rewrite, tip);
	}
}

void tape_set_state(struct tape_interface *ti, int flags) {
	struct tape_interface_private *tip = (struct tape_interface_private *)ti;
	/* set flags */
	tip->tape_fast = flags & TAPE_FAST;
	tip->tape_pad = flags & TAPE_PAD;
	tip->tape_pad_auto = flags & TAPE_PAD_AUTO;
	tip->tape_rewrite = flags & TAPE_REWRITE;
	set_breakpoints(tip);
}

/* sets state and updates UI */
void tape_select_state(struct tape_interface *ti, int flags) {
	tape_set_state(ti, flags);
	ui_module->set_state(ui_tag_tape_flags, flags, NULL);
}

int tape_get_state(struct tape_interface *ti) {
	struct tape_interface_private *tip = (struct tape_interface_private *)ti;
	int flags = 0;
	if (tip->tape_fast) flags |= TAPE_FAST;
	if (tip->tape_pad) flags |= TAPE_PAD;
	if (tip->tape_pad_auto) flags |= TAPE_PAD_AUTO;
	if (tip->tape_rewrite) flags |= TAPE_REWRITE;
	return flags;
}
