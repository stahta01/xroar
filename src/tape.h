/** \file
 *
 *  \brief Cassette tape support.
 *
 *  \copyright Copyright 2003-2022 Ciaran Anscomb
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

#ifndef XROAR_TAPE_H_
#define XROAR_TAPE_H_

#include <stdint.h>

#include "delegate.h"
#include "events.h"

struct ui_interface;

/* These are the usual cycle lengths for each bit as written by the Dragon
 * BASIC ROM. */
#define TAPE_BIT0_LENGTH (813 * EVENT_TICKS_14M31818(16))
#define TAPE_BIT1_LENGTH (435 * EVENT_TICKS_14M31818(16))
#define TAPE_AV_BIT_LENGTH ((TAPE_BIT0_LENGTH + TAPE_BIT1_LENGTH) / 2)

struct machine;

struct tape_interface {
	// Delegate for tape output updates
	DELEGATE_T1(void, float) update_audio;

	// Current tapes for input and output
	struct tape *tape_input;
	struct tape *tape_output;

	// Set by machines without motor control.  When set, tape interface
	// will pause on reset or when a new tape is inserted.
	_Bool default_paused;
};

struct tape_module;

struct tape {
	struct tape_module *module;
	struct tape_interface *tape_interface;
	int type;  // file type FILETYPE_*
	void *data;  /* module-specific data */
	long offset;  /* current tape position */
	long size;  /* current tape size */
	int leader_count;  /* CAS files will report initial leader bytes */
	event_ticks last_write_cycle;
};

struct tape_module {
	void (* const close)(struct tape *t);
	long (* const tell)(struct tape const *t);
	int (* const seek)(struct tape *t, long offset, int whence);
	int (* const to_ms)(struct tape const *t, long pos);
	long (* const ms_to)(struct tape const *t, int ms);
	int (* const pulse_in)(struct tape *t, int *pulse_width);
	int (* const sample_out)(struct tape *t, uint8_t sample, int length);
	void (* const motor_off)(struct tape *t);
	void (* const set_panning)(struct tape *, float pan);  // 0.0 (L) .. 1.0 (R)
	void (* const set_hysteresis)(struct tape *, float hysteresis);  // % full swing
};

#define tape_close(t) (t)->module->close(t)
/* for audio file input, tape_tell() might return an absolute file position,
   or sample number whichever is appropriate.  for cas file, it'll probably be
   file position * 8 + bit position */
#define tape_tell(t) (t)->module->tell(t)
int tape_seek(struct tape *t, long offset, int whence);
#define tape_to_ms(t,...) (t)->module->to_ms((t), __VA_ARGS__)
#define tape_ms_to(t,...) (t)->module->ms_to((t), __VA_ARGS__)
#define tape_rewind(t) tape_seek(t, 0, SEEK_SET)
#define tape_sample_out(t,...) (t)->module->sample_out((t), __VA_ARGS__)

struct tape_file {
	long offset;
	char name[9];
	int type;
	_Bool ascii_flag;
	_Bool gap_flag;
	int start_address;
	int load_address;
	_Bool checksum_error;
	int fnblock_size;
	uint16_t fnblock_crc;
};

/* find next tape file */
struct tape_file *tape_file_next(struct tape *t, int skip_bad);
/* seek to a tape file */
void tape_seek_to_file(struct tape *t, struct tape_file const *f);

/* Module-specific open() calls */
struct tape *tape_cas_open(const char *filename, const char *mode);
struct tape *tape_k7_open(const char *filename, const char *mode);
struct tape *tape_asc_open(const char *filename, const char *mode);
struct tape *tape_sndfile_open(const char *filename, const char *mode, int rate);

/* Only to be used by tape modules */
struct tape *tape_new(void);
void tape_free(struct tape *t);

/**************************************************************************/

struct tape_interface *tape_interface_new(struct ui_interface *ui);
void tape_interface_free(struct tape_interface *ti);
void tape_interface_connect_machine(struct tape_interface *ti, struct machine *m);
void tape_interface_disconnect_machine(struct tape_interface *ti);

void tape_reset(struct tape_interface *ti);

int tape_open_reading(struct tape_interface *ti, const char *filename);
void tape_close_reading(struct tape_interface *ti);
int tape_open_writing(struct tape_interface *ti, const char *filename);
void tape_close_writing(struct tape_interface *ti);

int tape_autorun(struct tape_interface *ti, const char *filename);


// Automatic motor control.  Simulates cassette relay.
void tape_set_motor(struct tape_interface *ti, _Bool motor);

// Called by machine to reflect change in tape output level.
void tape_update_output(struct tape_interface *ti, uint8_t value);

#endif
