/** \file
 *
 *  \brief CAS format tape images.
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
 *  Includes CUE support, as proposed by Roberto CFG, here:
 *
 *  https://archive.worldofdragon.org/phpBB3/viewtopic.php?f=8&t=5621
 */

#include "top-config.h"

// for off_t based functions
#define _POSIX_C_SOURCE 200112L

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "intfuncs.h"
#include "slist.h"
#include "xalloc.h"

#include "fs.h"
#include "logging.h"
#include "mc6883.h"
#include "tape.h"

// CAS simulated sample rate chosen such that:
// - converting to SAM ticks is trivial
// - quantizing sample counts per pulse is still pretty accurate
// - 2^31 samples still enough to represent more than a C90 could hold

#define TICKS_PER_SECOND (14318180)
#define TICKS_PER_SAMPLE (64)
#define CAS_RATE (TICKS_PER_SECOND / TICKS_PER_SAMPLE)
#define SAMPLES_PER_MS (CAS_RATE / 1000.0)
#define TICKS_PER_MS (TICKS_PER_SECOND / 1000.0)

// maximum number of pulses to buffer for frequency analysis:

#define PULSE_BUFFER_SIZE (400)

// Input CUE chunk types:

#define CUE_TIMING (0xf0)
#define CUE_SILENCE (0x00)
#define CUE_DATA (0x0d)

// Internal CUE entry types:

enum cue_entry_type {
	cue_silence,
	cue_leader,
	cue_raw_section,
	cue_block,
};

struct cue_builder {
	off_t end_offset;  // stop reading here

	int bit0_cw;
	int bit1_cw;
	int bit_av_pw;
	int time;
};

struct cue_entry {
	enum cue_entry_type type;

	int time;  // time at start of entry, for seeking
	int nsamples;  // computed total for entry

	struct {
		// cycle widths for section data
		int bit0_cw;
		int bit1_cw;
		int bit_av_pw;
		off_t offset;  // offset into file
		int size;  // for synthetic sections
		uint8_t *data;  // for synthetic blocks
	} section;

	// block metadata used to encapsulate a section
	struct {
		_Bool ascii;
		uint8_t type;
		uint8_t checksum;
	} block;
};

struct tape_cas {
	FILE *fd;
	_Bool writing;
	off_t size;  // file size minus CUE data
	_Bool rewind;  // writing and rewound - drop CUE data

	// CUE data
	struct {
		struct cue_builder *builder;
		struct slist *list;
		struct slist *next;
		struct cue_entry *entry;
		int byte_offset;  // entry-relative offset of next byte
	} cue;

	// input-specific:
	struct {
		int byte;  // current input byte
		int bit;   // current input bit
		int nbits;  // 0-7, next bit to be read
		int npulses;  // 0-1, next pulse within bit
	} input;

	struct {
		int num_buffered_pulses;
		int pulse_buffer[PULSE_BUFFER_SIZE];
		int sense;  // 0 = -ve, 1 = +ve, -1 = unknown
		int silence_ticks;
		int pulse_ticks;  // current pulse
		int last_pulse_ticks;  // previous pulse
		int byte;  // current output byte
		int nbits;  // in current output byte
		int data_start;
	} output;
};

static void cas_close(struct tape *t);
static long cas_tell(struct tape const *t);
static int cas_seek(struct tape *t, long offset, int whence);
static int cas_to_ms(struct tape const *t, long pos);
static long cas_ms_to(struct tape const *t, int ms);
static int cas_pulse_in(struct tape *t, int *pulse_width);
static int cas_sample_out(struct tape *t, uint8_t sample, int ticks);
static void cas_motor_off(struct tape *t);

struct tape_module tape_cas_module = {
	.close = cas_close, .tell = cas_tell, .seek = cas_seek,
	.to_ms = cas_to_ms, .ms_to = cas_ms_to,
	.pulse_in = cas_pulse_in, .sample_out = cas_sample_out,
	.motor_off = cas_motor_off,
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static int read_byte(struct tape_cas *cas);
static void bit_out(struct tape *t, int bit);
static void commit_silence(struct tape *t);
static void commit_data(struct tape *t);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct cue_entry *cue_entry_new(int type);

static void cue_set_timing(struct tape_cas *cas, int bit0_cw, int bit1_cw);
static void cue_add_silence(struct tape_cas *cas, int ms);
static void cue_add_leader(struct tape_cas *cas, int size);
static void cue_add_raw_section(struct tape_cas *cas, int size, off_t offset, uint8_t *data);
static void cue_add_block(struct tape_cas *cas, int type, int size, _Bool ascii,
			  off_t offset, uint8_t *data);

static void cue_list_free(struct tape_cas *cas);
static long cue_data_size(struct tape_cas *cas);
static _Bool cue_next(struct tape_cas *cas);

static _Bool read_cue_data(struct tape_cas *cas);
static void write_cue_data(struct tape_cas *cas);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void init_filename_block(uint8_t *block, const char *filename, int type, _Bool is_ascii, _Bool gap, int exec, int load) {
	const char *basename = filename;
	while (*filename) {
		if (*(filename+1) != 0 && ((*filename == '/') || (*filename == '\\')))
			basename = filename+1;
		filename++;
	}
	snprintf((char *)block, 9, "%s", basename);
	for (int i = 0; i < 8; i++) {
		if (block[i] < 32 || block[i] > 126) {
			for (; i < 8; i++)
				block[i] = ' ';
			break;
		}
	}
	block[8] = type;
	block[9] = is_ascii ? 0xff : 0;
	block[10] = gap ? 0xff : 0;
	block[11] = exec >> 8;
	block[12] = exec & 0xff;
	block[13] = load >> 8;
	block[14] = load & 0xff;
}

static void cue_read_ascii(struct tape_cas *cas, const char *filename);
static void cue_read_k7(struct tape_cas *cas);

static struct tape *do_tape_cas_open(const char *filename,
				     const char *mode, _Bool is_ascii, _Bool is_k7) {
	struct tape *t;
	struct tape_cas *cas;
	FILE *fd;

	const char *mod = is_ascii ? "tape/asc" : (is_k7 ? "tape/k7" : "tape/cas");

	_Bool writing = mode[0] == 'w';
	_Bool new_file = 0;
	// if writing, open in read/write mode so that existing cue data can be
	// read.  new data will be appended unless user rewinds tape.
	if (writing) {
		if (!(fd = fopen(filename, "r+b"))) {
			if (!(fd = fopen(filename, "wb"))) {
				LOG_MOD_WARN(mod, "%s: %s\n", filename, strerror(errno));
				return NULL;
			}
			new_file = 1;
		}
	} else {
		if (!(fd = fopen(filename, "rb"))) {
			LOG_MOD_WARN(mod, "%s: %s\n", filename, strerror(errno));
			return NULL;
		}
	}

	off_t filesize = fs_file_size(fd);
	if (filesize == -1) {
		LOG_MOD_WARN(mod, "%s: %s\n", filename, strerror(errno));
		fclose(fd);
		return NULL;
	}

	// don't support ASCII or K7 writing yet
	if (writing) {
		is_ascii = 0;
		is_k7 = 0;
	}

	t = tape_new();
	t->module = &tape_cas_module;
	cas = xmalloc(sizeof(*cas));
	*cas = (struct tape_cas){0};
	t->data = cas;

	/* initialise cas */
	cas->fd = fd;
	cas->writing = writing;
	cas->size = filesize;  // initial value doesn't account for CUE data
	cas->output.sense = -1;
	cas->output.pulse_ticks = cas->output.last_pulse_ticks = 0;
	cas->output.silence_ticks = 0;
	cas->output.byte = cas->output.nbits = 0;
	cas->input.nbits = 0;  /* next read will fetch a new byte */
	cas->input.npulses = 0;

	struct cue_builder *builder = xmalloc(sizeof(*builder));
	*builder = (struct cue_builder){0};
	cas->cue.builder = builder;

	// only set default cycle widths when reading.  cycle widths need to be
	// measured before writing (indicated by bit_av_pw == 0) to populate
	// CUE data.
	if (!writing) {
		builder->bit0_cw = IDIV_ROUND(813 * 16, 64);
		builder->bit1_cw = IDIV_ROUND(435 * 16, 64);
		builder->bit_av_pw = (builder->bit0_cw + builder->bit1_cw) >> 2;
	}

	// note: read_cue_data() will update recorded size to omit any CUE data
	if (!new_file && (is_ascii || is_k7 || !read_cue_data(cas))) {
		if (is_ascii) {
			cue_read_ascii(cas, filename);
		} else if (is_k7) {
			cue_read_k7(cas);
		} else {
			// Normal CAS file: create a raw section encompassing
			// all file data
			cue_add_raw_section(cas, filesize, 0, NULL);
		}
		// if nothing there produced any CUE data, and we're reading,
		// we won't know what to do with this tape, so bail
		if (!writing && !cas->cue.list) {
			cas_close(t);
			LOG_MOD_WARN(mod, "%s: failed to read CUE data\n", filename);
			return NULL;
		}
	}

	t->size = cue_data_size(cas);

	if (writing) {
		// default to appending
		cas_seek(t, 0, SEEK_END);
	} else {
		// Count leader bytes.  Layer above will use this to decide whether to
		// shorten delays for "bad" CAS files.
		cas_seek(t, 0, SEEK_SET);
		t->leader_count = 0;
		for (;;) {
			int b = read_byte(cas);
			if (b != 0x55 && b != 0xaa)
				break;
			t->leader_count++;
		}
		cas_seek(t, 0, SEEK_SET);
	}

	return t;
}

// K7 files: like CAS, but much stricter about block format, with optional preamble
static void cue_read_k7(struct tape_cas *cas) {
	uint8_t buf[7];
	off_t nbytes = cas->size;
	int leader_length = 0;
	off_t block_length = 0;
	_Bool have_preamble = 0;
	off_t block_start = ftello(cas->fd);
	int block_num = 0;
	while (nbytes > 0) {
		int b = fgetc(cas->fd);
		if (b < 0)
			goto short_read;
		nbytes--;

		switch (b) {
		case 0xdc:
			if (leader_length > 0) {
				block_length += leader_length;
				leader_length = 0;
			}
			if (block_length > 0) {
				cue_add_raw_section(cas, block_length, block_start, NULL);
				block_length = 0;
			}
			if (nbytes < 7)
				goto malformed;
			if (fread(buf, 1, 7, cas->fd) < 7)
				goto short_read;
			nbytes -= 7;
			unsigned leader = ((buf[2] & 0x7f) << 8) | buf[1];
			unsigned silence = ((buf[6] & 0x7f) << 24) | (buf[5] << 16) | (buf[4] << 8) | buf[3];
			silence = (unsigned)((float)silence / 44.1);
			cue_add_silence(cas, silence);
			cue_add_leader(cas, leader);
			block_start = ftello(cas->fd);
			have_preamble = 1;
			break;

		case 0x55:
			leader_length++;
			break;

		case 0x3c:
			// a previous block without preamble gets all but one
			// of any leader we've seen
			if (block_length > 0) {
				if (leader_length > 1) {
					block_length += leader_length - 1;
					leader_length = 1;
				}
				cue_add_raw_section(cas, block_length, block_start, NULL);
				block_start += block_length;
			}
			block_length = leader_length + 1;
			// now read type, length
			if (nbytes < 2)
				goto malformed;
			if (fread(buf, 1, 2, cas->fd) < 2)
				goto short_read;
			nbytes -= 2;
			block_length += 2;
			block_num++;
			if (buf[0] == 0) {
				// filename block
				block_num = 0;
			}
			// no preamble?  here are the defaults
			if (!have_preamble) {
				if (block_num == 0) {
					// technically the spec says make this
					// 20s, but that seems excessive!
					cue_add_silence(cas, 4000);
					cue_add_leader(cas, 128);
				} else if (block_num == 1) {
					cue_add_silence(cas, 500);
					cue_add_leader(cas, 128);
				} else if (leader_length < 1) {
					cue_add_leader(cas, 1);
				}
			}
			int bbytes = buf[1] + 1;  // include checksum
			block_length += bbytes;
			if (nbytes < bbytes)
				goto malformed;
			fseeko(cas->fd, bbytes, SEEK_CUR);
			nbytes -= bbytes;
			have_preamble = 0;
			leader_length = 0;
			break;

		default:
			goto malformed;
		}

	}
	goto last_block;

malformed:
	LOG_MOD_WARN("tape/k7", "malformed K7 file\n");
	goto last_block;

short_read:
	LOG_MOD_WARN("tape/k7", "short read processing K7 file\n");
	goto last_block;

last_block:
	if (block_length > 0) {
		block_length += leader_length;
		cue_add_raw_section(cas, block_length, block_start, NULL);
	}
	return;
}

// ASCII BASIC file: create a gapped loader with ASCII translation flagged
static void cue_read_ascii(struct tape_cas *cas, const char *filename) {
	uint8_t *filename_block = xmalloc(15);
	init_filename_block(filename_block, filename, 0, 1, 1, 0, 0);
	cue_add_silence(cas, 500);
	cue_add_leader(cas, 192);
	cue_add_block(cas, 0, 15, 0, 0, filename_block);
	int offset = 0;
	int remain = cas->size;
	while (remain > 0) {
		int bsize = (remain > 255) ? 255 : remain;
		cue_add_silence(cas, 500);
		cue_add_leader(cas, 192);
		cue_add_block(cas, 1, bsize, 1, offset, NULL);
		remain -= bsize;
		offset += bsize;
	}
	cue_add_silence(cas, 500);
	cue_add_leader(cas, 192);
	cue_add_block(cas, 0xff, 0, 0, offset, NULL);
}

struct tape *tape_cas_open(const char *filename, const char *mode) {
	return do_tape_cas_open(filename, mode, 0, 0);
}

struct tape *tape_k7_open(const char *filename, const char *mode) {
	return do_tape_cas_open(filename, mode, 0, 1);
}

struct tape *tape_asc_open(const char *filename, const char *mode) {
	return do_tape_cas_open(filename, mode, 1, 0);
}

static void cas_close(struct tape *t) {
	struct tape_cas *cas = t->data;
	cas_motor_off(t);
	fclose(cas->fd);
	cue_list_free(cas);
	free(cas->cue.builder);
	free(cas);
	tape_free(t);
}

static void cas_motor_off(struct tape *t) {
	struct tape_cas *cas = t->data;
	if (!cas->writing)
		return;
	// ensure last pulse is flushed
	if (cas->output.sense != -1)
		cas_sample_out(t, cas->output.sense ? 0x00 : 0x80, 0);
	commit_data(t);
	commit_silence(t);
	// cue data is written each time motor is turned off.  write_cue_data()
	// resets file position so more can be written subsequently.
	write_cue_data(cas);
	cas->output.sense = -1;
	cas->output.pulse_ticks = 0;
	cas->output.last_pulse_ticks = 0;
	cas->output.silence_ticks = 0;
	// invalidating bit_av_pw forces a resync
	cas->cue.builder->bit_av_pw = 0;
}

static int read_byte(struct tape_cas *cas) {
	struct cue_entry *entry = cas->cue.entry;
	if (!entry)
		return -1;

	int d = -1;

	// raw data chunks
	if (entry->type == cue_raw_section) {
		if (entry->section.data) {
			d = entry->section.data[cas->cue.byte_offset];
		} else {
			d = fs_read_uint8(cas->fd);
		}
	}

	// blocks require decoration
	if (entry->type == cue_block
	    && cas->cue.byte_offset >= 0
	    && cas->cue.byte_offset < entry->section.size+6) {
		if (cas->cue.byte_offset == 0) {
			d = 0x55;  // leader
		} else if (cas->cue.byte_offset == 1) {
			d = 0x3c;  // sync
		} else if (cas->cue.byte_offset == 2) {
			d = entry->block.type;  // type
		} else if (cas->cue.byte_offset == 3) {
			d = entry->section.size;  // size
		} else if (cas->cue.byte_offset < (entry->section.size+4)) {
			if (entry->section.data) {
				d = entry->section.data[cas->cue.byte_offset - 4];
			} else {
				d = fs_read_uint8(cas->fd);
			}
			if (entry->block.ascii && d == 0x0a)
				d = 0x0d;
		} else if (cas->cue.byte_offset == entry->section.size+4) {
			d = entry->block.checksum;
		} else {
			d = 0x55;
		}
	}

	// leader is simple enough
	if (entry->type == cue_leader)
		d = 0x55;

	if (d >= 0)
		cas->cue.byte_offset++;
	return d;
}

static long cas_tell(struct tape const *t) {
	return t->offset;
}

static int cue_entry_seek(struct tape_cas *cas, int offset) {
	struct cue_entry *entry = cas->cue.entry;
	assert(entry != NULL);
	if (entry->section.data || entry->type == cue_leader) {
		cas->cue.byte_offset = offset;
		return 0;
	}
	if (entry->type == cue_block) {
		offset -= 4;  // leader, sync, type, size
		if (offset < 0)
			offset = 0;
	}
	off_t seek_byte = entry->section.offset + offset;
	return fseeko(cas->fd, seek_byte, SEEK_SET);
}

static int cas_seek(struct tape *t, long offset, int whence) {
	struct tape_cas *cas = t->data;

	if (whence == SEEK_CUR) {
		offset += t->offset;
	} else if (whence == SEEK_END) {
		offset += t->size;
	}

	// no fanciness when writing.  if rewinding, flag to zero file, else
	// seek to end to continue appending.
	if (cas->writing) {
		if (offset < t->offset || offset == 0) {
			t->offset = 0;
			cas->rewind = 1;
		} else {
			cas->output.data_start = cas->size;
			fseeko(cas->fd, cas->size, SEEK_SET);
			t->offset = t->size;
			cas->rewind = 0;
		}
		return 0;
	}

	long section_offset = offset;
	cas->cue.next = cas->cue.list;
	struct cue_entry *entry = NULL;
	while (cue_next(cas)) {
		entry = cas->cue.entry;
		if (section_offset < entry->nsamples) {
			break;
		}
		section_offset -= entry->nsamples;
	}

	if (!entry || entry->type == cue_silence) {
		cas->input.nbits = 0;
		cas->input.npulses = 0;
		t->offset = offset;
		return 0;
	}

	section_offset /= entry->section.bit_av_pw;
	int byte_offset = section_offset / 16;
	if (cue_entry_seek(cas, byte_offset) == -1)
		return -1;

	section_offset %= 16;
	cas->input.nbits = (section_offset >> 1) & 7;
	cas->input.npulses = section_offset & 1;
	if (cas->input.nbits != 0 || cas->input.npulses != 0) {
		cas->input.byte = read_byte(cas);
		cas->input.bit = (cas->input.byte & (1 << cas->input.nbits)) ? 1 : 0;
	}
	t->offset = offset;
	return 0;
}

static int cas_to_ms(struct tape const *t, long pos) {
	(void)t;
	return pos / SAMPLES_PER_MS;
}

static long cas_ms_to(struct tape const *t, int ms) {
	(void)t;
	return ms * SAMPLES_PER_MS;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/* Reading */

static _Bool cue_next(struct tape_cas *cas) {
	if (!cas->cue.next)
		return 0;

	struct slist *current = cas->cue.next;
	struct cue_entry *entry = current->data;
	cas->cue.next = current->next;
	cas->cue.entry = entry;
	int type = entry->type;

	// only these types actually refer to file data
	switch (type) {
	case cue_raw_section:
	case cue_block:
		if (!entry->section.data) {
			// not synthetic block, seek to appropriate offset
			fseeko(cas->fd, entry->section.offset, SEEK_SET);
		}
		break;
	default:
		break;
	}

	return 1;
}

static int cas_pulse_in(struct tape *t, int *pulse_width) {
	struct tape_cas *cas = t->data;
	struct cue_entry *entry = cas->cue.entry;
	if (cas->input.npulses == 0) {
		if (cas->input.nbits == 0) {
			while (t->offset >= (entry->time + entry->nsamples)) {
				if (!cue_next(cas))
					return -1;
				entry = cas->cue.entry;
				cas->cue.byte_offset = 0;
			}
			if (entry->type == cue_silence) {
				int remain = (entry->time + entry->nsamples) - t->offset;
				int wait = (remain < (CAS_RATE/100)) ? remain : (CAS_RATE/100);
				*pulse_width = wait * TICKS_PER_SAMPLE;
				t->offset += wait;
				return 1;
			}
			if ((cas->input.byte = read_byte(cas)) == -1)
				return -1;
		}
		cas->input.bit = (cas->input.byte >> cas->input.nbits) & 1;
	}
	int cw = (cas->input.bit == 0) ? entry->section.bit0_cw : entry->section.bit1_cw;
	cw *= TICKS_PER_SAMPLE;
	*pulse_width = cw >> 1;
	cas->input.npulses = (cas->input.npulses + 1) & 1;
	if (cas->input.npulses == 0) {
		cas->input.nbits = (cas->input.nbits + 1) & 7;
	}
	t->offset += entry->section.bit_av_pw;
	return !cas->input.npulses;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/* Writing */

// CAS writing takes an incoming stream of samples and measures the time
// between zero crossings as "pulses".  If no average pulse widths have yet
// been calculated, a buffer of pulses is built up - this should be coming from
// leader bytes, with alternating '0' and '1' bits.
//
// Once the pulse buffer is filled, it is processed to calculate nominal
// average pulse widths for cycles forming '0' and '1' bits.  The buffer and
// any subsequently pulses are then converted to bits and written until silence
// or motor control desyncs the writer, and the process begins again.

static void process_pulse_buffer(struct tape *t);

static void rewind_tape(struct tape *t) {
	struct tape_cas *cas = t->data;

	cas->rewind = 0;
	if (fs_truncate(cas->fd, 0) < 0) {
		LOG_MOD_WARN("tape/cas", "rewind failed: %s\n", strerror(errno));
		return;
	}
	cas->cue.builder->bit_av_pw = 0;
	cue_list_free(cas);
	cas->cue.next = NULL;
	cas->cue.entry = NULL;
	cas->size = 0;
	cas->output.data_start = 0;
	cas->output.byte = 0;
	cas->output.nbits = 0;
	cas->output.silence_ticks = 0;
	cas->output.pulse_ticks = 0;
	cas->output.last_pulse_ticks = 0;
}

static int write_byte(struct tape *t, int v) {
	struct tape_cas *cas = t->data;
	if (!cas->writing)
		return -1;
	if (fs_write_uint8(cas->fd, v) < 0)
		return -1;
	cas->size++;
	return 1;
}

static void bit_out(struct tape *t, int bit) {
	struct tape_cas *cas = t->data;
	cas->output.byte = ((cas->output.byte >> 1) & 0x7f) | (bit ? 0x80 : 0);
	cas->output.nbits++;
	if (cas->output.nbits == 8) {
		cas->output.nbits = 0;
		write_byte(t, cas->output.byte);
	}
}

static void commit_silence(struct tape *t) {
	struct tape_cas *cas = t->data;
	int ticks = cas->output.silence_ticks + cas->output.last_pulse_ticks;
	cas->output.silence_ticks = 0;
	cas->output.last_pulse_ticks = 0;
	int ms = IDIV_ROUND(ticks, TICKS_PER_MS);
	if (ms == 0) {
		return;
	}
	if (ms >= 10) {
		// force a resync after long (well, long-ish) silence
		cas->cue.builder->bit_av_pw = 0;
	}
	cue_add_silence(cas, ms);
	t->offset += ms * SAMPLES_PER_MS;
	t->size = t->offset;
	// add dummy leader to file
	int nleader = ticks / 80200;
	for (int i = 0; i < nleader; i++) {
		write_byte(t, 0x55);
	}
	// update current file position (start of next data entry)
	cas->output.data_start = ftello(cas->fd);
}

static void commit_data(struct tape *t) {
	struct tape_cas *cas = t->data;
	struct cue_builder *builder = cas->cue.builder;

	if (cas->output.num_buffered_pulses > 0)
		process_pulse_buffer(t);

	while (cas->output.nbits > 0) {
		bit_out(t, 0);
		t->offset += builder->bit_av_pw * 2;
		t->size = t->offset;
	}

	off_t data_start = cas->output.data_start;
	off_t data_end = ftello(cas->fd);
	if (data_end > data_start) {
		cue_add_raw_section(cas, data_end - data_start, data_start, NULL);
		// update current file position (start of next data entry)
		cas->output.data_start = data_end;
	}
}

static void add_silence(struct tape *t, int ticks) {
	struct tape_cas *cas = t->data;
	// include last pulse duration in silence, as this indicates a
	// mismatched pulse
	cas->output.silence_ticks += ticks + cas->output.last_pulse_ticks;
	cas->output.last_pulse_ticks = 0;
	// invalidating bit_av_pw forces a resync
	cas->cue.builder->bit_av_pw = 0;
	// flush any silence longer than 2s
	if (cas->output.silence_ticks >= TICKS_PER_SECOND*2) {
		int remain = cas->output.silence_ticks - TICKS_PER_SECOND*2;
		cas->output.silence_ticks = TICKS_PER_SECOND*2;
		commit_silence(t);
		cas->output.silence_ticks = remain;
	}
}

static void add_pulse(struct tape *t, int ticks) {
	struct tape_cas *cas = t->data;
	struct cue_builder *builder = cas->cue.builder;

	// no previous pulse?  just log this one
	if (cas->output.last_pulse_ticks == 0) {
		cas->output.last_pulse_ticks = ticks;
		return;
	}

	int bit_cwt = cas->output.last_pulse_ticks + ticks;
	cas->output.last_pulse_ticks = 0;
	int bit_cw = IDIV_ROUND(bit_cwt, TICKS_PER_SAMPLE);

	cas->output.silence_ticks = 0;
	cas->output.last_pulse_ticks = 0;

	// add_pulse() is never called until bit_av_pw has been determined
	assert(builder->bit_av_pw > 0);

	int bit = (bit_cw < builder->bit_av_pw * 2) ? 1 : 0;
	bit_out(t, bit);
	t->offset += builder->bit_av_pw * 2;

	// don't support mid-file overwriting, always either appending or
	// overwriting whole file.  therefore size always == offset
	t->size = t->offset;
}

static void process_pulse_buffer(struct tape *t) {
	struct tape_cas *cas = t->data;

	int npulses = cas->output.num_buffered_pulses;
	if (npulses == 0)
		return;
	cas->output.num_buffered_pulses = 0;

	int bit0_pwt;
	int bit1_pwt;
	// We need to write out the contents of pulse_buffer, so can't use
	// int_split_inplace here:
	int_split(cas->output.pulse_buffer, npulses, &bit1_pwt, &bit0_pwt);
	if (bit0_pwt <= 0)
		bit0_pwt = (bit1_pwt > 0) ? bit1_pwt * 2 : 6403;
	if (bit1_pwt <= 0)
		bit1_pwt = (bit0_pwt >= 2) ? bit0_pwt / 2 : 3489;
	int bit_av_pwt = (bit0_pwt + bit1_pwt + 1) >> 1;

	// seek until two pulses in same category are found
	int start;
	for (start = 0; start < npulses - 1; start++) {
		if ((cas->output.pulse_buffer[start] <= bit_av_pwt) ==
		    (cas->output.pulse_buffer[start+1] <= bit_av_pwt))
			break;
		add_silence(t, cas->output.pulse_buffer[start]);
	}

	commit_silence(t);

	int bit0_cw = IDIV_ROUND(bit0_pwt*2, TICKS_PER_SAMPLE);
	int bit1_cw = IDIV_ROUND(bit1_pwt*2, TICKS_PER_SAMPLE);
	cue_set_timing(cas, bit0_cw, bit1_cw);

	for (int i = start; i < npulses; i++) {
		add_pulse(t, cas->output.pulse_buffer[i]);
	}
}

static void buffer_pulse(struct tape *t, int ticks) {
	struct tape_cas *cas = t->data;
	cas->output.pulse_buffer[cas->output.num_buffered_pulses++] = ticks;
	if (cas->output.num_buffered_pulses < PULSE_BUFFER_SIZE)
		return;
	// Pulse buffer full, process it to determine average pulse widths for
	// '0' and '1' bits, and write the buffered pulses.
	process_pulse_buffer(t);
}

static void pulse_out(struct tape *t, int ticks) {
	struct tape_cas *cas = t->data;

	// freq <50Hz or >4800Hz considered invalid - treat as silence
	if (ticks > (TICKS_PER_SECOND / 100) || ticks < (TICKS_PER_SECOND / 9600)) {
		// flush any queued data
		commit_data(t);
		add_silence(t, ticks);
		return;
	}

	if (cas->cue.builder->bit_av_pw == 0) {
		// Haven't determined pulse widths yet, so buffer this pulse.
		buffer_pulse(t, ticks);
		return;
	}

	// Otherwise add the pulse.
	add_pulse(t, ticks);
}

static int cas_sample_out(struct tape *t, uint8_t sample, int ticks) {
	struct tape_cas *cas = t->data;
	int sense = (sample >= 0x80);
	if (cas->rewind)
		rewind_tape(t);
	if (cas->output.sense == -1) {
		cas->output.sense = sense;
	}
	if (sense != cas->output.sense) {
		// Sense changed; this is a zero crossing.  Emit as a "pulse"
		// with pulse_out().
		pulse_out(t, cas->output.pulse_ticks);
		cas->output.pulse_ticks = ticks;
		cas->output.sense = sense;
		return 0;
	}
	cas->output.pulse_ticks += ticks;
	while (cas->output.pulse_ticks >= (TICKS_PER_SECOND / 100)) {
		// flush any queued data
		commit_data(t);
		add_silence(t, cas->output.pulse_ticks);
		cas->output.pulse_ticks = 0;
	}
	return 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// CUE list handling.

static struct cue_entry *cue_entry_new(int type) {
	struct cue_entry *new = xmalloc(sizeof(*new));
	*new = (struct cue_entry){0};
	new->type = type;
	return new;
}

static void cue_entry_free(void *sptr) {
	struct cue_entry *entry = sptr;
	if (entry->section.data)
		free(entry->section.data);
	free(entry);
}

static void cue_list_free(struct tape_cas *cas) {
	if (cas && cas->cue.list) {
		slist_free_full(cas->cue.list, (slist_free_func)cue_entry_free);
		cas->cue.list = NULL;
	}
}

static void cue_entry_append(struct tape_cas *cas, struct cue_entry *entry) {
	entry->time = cas->cue.builder->time;
	cas->cue.builder->time += entry->nsamples;
	cas->cue.list = slist_append(cas->cue.list, entry);
}

static void cue_set_timing(struct tape_cas *cas, int bit0_cw, int bit1_cw) {
	cas->cue.builder->bit0_cw = bit0_cw;
	cas->cue.builder->bit1_cw = bit1_cw;
	cas->cue.builder->bit_av_pw = (bit0_cw + bit1_cw) >> 2;
}

static void cue_add_silence(struct tape_cas *cas, int ms) {
	struct cue_entry *entry = cue_entry_new(cue_silence);
	entry->nsamples = (int)((float)ms * SAMPLES_PER_MS);
	cue_entry_append(cas, entry);
}

static void cue_set_section_data(struct tape_cas *cas, struct cue_entry *entry, off_t offset, int size, uint8_t *data) {
	entry->section.bit0_cw = cas->cue.builder->bit0_cw;
	entry->section.bit1_cw = cas->cue.builder->bit1_cw;
	entry->section.bit_av_pw = cas->cue.builder->bit_av_pw;
	entry->section.offset = offset;
	entry->section.size = size;
	entry->section.data = data;
}

static void cue_add_raw_section(struct tape_cas *cas, int size, off_t offset, uint8_t *data) {
	if (size == 0)
		return;
	struct cue_entry *entry = cue_entry_new(cue_raw_section);
	entry->nsamples = size * 16 * cas->cue.builder->bit_av_pw;
	cue_set_section_data(cas, entry, offset, size, data);
	cue_entry_append(cas, entry);
}

static void cue_add_leader(struct tape_cas *cas, int size) {
	struct cue_entry *entry = cue_entry_new(cue_leader);
	entry->nsamples = size * 16 * cas->cue.builder->bit_av_pw;
	cue_set_section_data(cas, entry, 0, size, NULL);
	cue_entry_append(cas, entry);
}

// A block is decorated on the fly with: leader byte fore and aft, sync, type
// (supplied), size (supplied), checksum (computed).  'ascii' flag indicates
// that LF should be translated to CR.

static void cue_add_block(struct tape_cas *cas, int type, int size, _Bool ascii,
			  off_t offset, uint8_t *data) {
	struct cue_entry *entry = cue_entry_new(cue_block);
	assert(size >= 0 && size <= 255);

	// compute checksum
	uint8_t sum = type + size;
	uint8_t block[255];
	off_t oldpos = 0;
	if (data) {
		memcpy(block, data, size);
	} else {
		oldpos = ftello(cas->fd);
		fseeko(cas->fd, offset, SEEK_SET);
		size = fread(block, 1, size, cas->fd);
	}
	for (int i = 0; i < size; i++) {
		if (ascii && block[i] == 0x0a)
			block[i] = 0x0d;
		sum += block[i];
	}
	if (!data) {
		fseeko(cas->fd, oldpos, SEEK_SET);
	}

	entry->nsamples = (size+6) * 16 * cas->cue.builder->bit_av_pw;

	cue_set_section_data(cas, entry, offset, size, data);

	entry->block.type = type;
	entry->block.ascii = ascii;
	entry->block.checksum = sum;
	cue_entry_append(cas, entry);
}

// Compute total size of file in samples based on CUE data.

static long cue_data_size(struct tape_cas *cas) {
	int size = 0;
	for (struct slist *iter = cas->cue.list; iter; iter = iter->next) {
		struct cue_entry *entry = iter->data;
		size += entry->nsamples;
	}
	return size;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Read in CUE data, if present.
//
// cas->size is assumed to be the absolute filesize on entry, but this function
// modifies that value to omit the CUE data.  Must reset absolute filesize
// before calling again.

static _Bool read_cue_data(struct tape_cas *cas) {
	FILE *fd = cas->fd;

	off_t filesize = cas->size;

	// At least 12 bytes are required for the CUE markers...
	if (filesize < 12)
		return 0;

	off_t cue_start;
	off_t cue_end = filesize - 8;

	// check for CUE end marker: "CUE]"
	if (fseeko(fd, filesize-4, SEEK_SET) < 0)
		goto seek_failed;
	if (fs_read_uint16(fd) != 0x4355 || fs_read_uint16(fd) != 0x455d)
		return 0;
	if (fseeko(fd, cue_end, SEEK_SET) < 0)
		goto seek_failed;
	cue_start = fs_read_uint31(fd);
	if (cue_start < 0 || cue_end < cue_start)
		return 0;
	if (fseeko(fd, cue_start, SEEK_SET) < 0)
		goto seek_failed;

	// check for CUE start marker: "[CUE"
	if (fs_read_uint16(fd) != 0x5b43 || fs_read_uint16(fd) != 0x5545) {
		return 0;
	}

	// update file size to omit CUE data
	cas->size = cue_start;

	// should never hit EOF
	while (!feof(fd) && ftello(fd) < cue_end) {
		int type = fs_read_uint8(fd);
		int nread;
		int32_t elength = fs_read_vuint32(fd, &nread);
		if (type < 0 || elength < 0 || nread <= 0)
			goto read_failed;

		switch (type) {

		case CUE_TIMING: {
			int bit0_hz = fs_read_uint16(fd);
			int bit1_hz = fs_read_uint16(fd);
			// zero is also invalid, as we'll be dividing by it!
			if (bit0_hz <= 0 || bit1_hz <= 0)
				goto read_failed;
			elength -= 4;
			int bit0_cw = IDIV_ROUND(CAS_RATE, bit0_hz);
			int bit1_cw = IDIV_ROUND(CAS_RATE, bit1_hz);
			cue_set_timing(cas, bit0_cw, bit1_cw);
		}
			break;

		case CUE_SILENCE: {
			int silence_ms = fs_read_uint16(fd);
			if (silence_ms < 0)
				goto read_failed;
			elength -= 2;
			cue_add_silence(cas, silence_ms);
		}
			break;

		case CUE_DATA: {
			off_t start = fs_read_uint31(fd);
			off_t end = fs_read_uint31(fd);
			if (start < 0 || end < 0)
				goto read_failed;
			elength -= 8;
			if (end > start) {
				int size = end - start;
				cue_add_raw_section(cas, size, start, NULL);
			}
		}
			break;

		default:
			LOG_MOD_WARN("tape/cas", "read_cue_data(): unexpected entry type: %d (%d bytes)\n", type, elength);
			if (fseeko(fd, elength, SEEK_CUR) < 0) {
				cue_list_free(cas);
				return 0;
			}
			elength = 0;
		}

		if (elength != 0) {
			if (elength > 0) {
				LOG_MOD_WARN("tape/cas", "read_cue_data(): read underrun: %d bytes\n", elength);
			} else {
				LOG_MOD_WARN("tape/cas", "read_cue_data(): read overrun: %d bytes\n", -elength);
			}
			cue_list_free(cas);
			return 0;
		}
	}
	return 1;

read_failed:
	if (feof(fd)) {
		LOG_MOD_WARN("tape/cas", "read_cue_data(): EOF reading CUE data\n");
	} else if (ferror(fd)) {
		LOG_MOD_WARN("tape/cas", "read_cue_data(): error reading CUE data\n");
	}
	cue_list_free(cas);
	return 0;

seek_failed:
	LOG_MOD_WARN("tape/cas", "read_cue_data(): seek failed: %s\n", strerror(errno));
	cue_list_free(cas);
	return 0;
}

// Write CUE data at end of current file (file length in cas->size).  Does not
// write internal CUE entry types (block type).
//
// NOTE: you'll see no vuint31 writes here, as all current chunks are actually
// < 128 bytes long (where the vuint31 representation is identical to the
// uint8).

static void write_cue_data(struct tape_cas *cas) {
	assert(cas != NULL);

	if (!cas->writing || !cas->cue.list)
		return;

	FILE *fd = cas->fd;
	struct cue_builder *builder = cas->cue.builder;

	// Invalidate current cycle width information so that we always emit
	// TIMING information.
	builder->bit0_cw = 0;
	builder->bit1_cw = 0;
	builder->bit_av_pw = 0;

	off_t cue_start = cas->size;
	fseeko(fd, cue_start, SEEK_SET);

	// CUE start marker
	fwrite("[CUE", 1, 4, fd);

	for (struct slist *iter = cas->cue.list; iter; iter = iter->next) {
		struct cue_entry *entry = iter->data;
		switch (entry->type) {

		case cue_silence:
			{
				int nsamples = entry->nsamples;

				// coalesce silence
				while (iter->next) {
					struct cue_entry *nentry = iter->next->data;
					if (nentry->type != cue_silence)
						break;
					iter = iter->next;
					nsamples += nentry->nsamples;
				}

				int ms = IDIV_ROUND(nsamples, SAMPLES_PER_MS);
				fs_write_uint8(fd, CUE_SILENCE);
				fs_write_uint8(fd, 2);
				fs_write_uint16(fd, ms);
			}
			break;

		case cue_raw_section:
			{
				int start = entry->section.offset;
				int end = start + entry->section.size;
				if (start == end)
					break;
				if (builder->bit0_cw != entry->section.bit0_cw || builder->bit1_cw != entry->section.bit1_cw) {
					fs_write_uint8(fd, CUE_TIMING);
					fs_write_uint8(fd, 4);
					builder->bit0_cw = entry->section.bit0_cw;
					builder->bit1_cw = entry->section.bit1_cw;
					int bit0_hz = IDIV_ROUND(CAS_RATE, builder->bit0_cw);
					int bit1_hz = IDIV_ROUND(CAS_RATE, builder->bit1_cw);
					fs_write_uint16(fd, bit0_hz);
					fs_write_uint16(fd, bit1_hz);
				}
				fs_write_uint8(fd, CUE_DATA);
				fs_write_uint8(fd, 8);
				fs_write_uint16(fd, start >> 16);
				fs_write_uint16(fd, start);
				fs_write_uint16(fd, end >> 16);
				fs_write_uint16(fd, end);
			}
			break;

		default:
			LOG_MOD_ERROR("tape/cas", "write_cue_data(): unexpected entry type: %d\n", entry->type);
			break;
		}
	}

	// CUE start offset
	fs_write_uint31(fd, cue_start);
	// CUE end marker
	fwrite("CUE]", 1, 4, fd);

	// return file position to end of actual data
	fseeko(cas->fd, cue_start, SEEK_SET);
}
