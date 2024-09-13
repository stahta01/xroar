/** \file
 *
 *  \brief Basic WAV-only libsndfile compatible interface.
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

#ifndef XROAR_SNDFILE_H_
#define XROAR_SNDFILE_H_

#ifdef HAVE_SNDFILE

#include <sndfile.h>

#else

#include <stdlib.h>
#include <fcntl.h>

// Our WAV-only workalike, providing only as much functionality as needed by
// XRoar.

typedef off_t sf_count_t;

// Named constants are NOT defined the same as libsndfile does, so DON'T rely
// on their values.

enum {
	SF_ERR_NO_ERROR = 0,
	SF_ERR_UNRECOGNISED_FORMAT,
	SF_ERR_SYSTEM,
	SF_ERR_MALFORMED_FILE,
	SF_ERR_UNSUPPORTED_ENCODING,
};

enum {
	// Endianness
	SF_ENDIAN_FILE   = (0 << 8),
	SF_ENDIAN_LITTLE = (1 << 8),
	SF_ENDIAN_BIG    = (2 << 8),
	SF_ENDIAN_CPU    = (3 << 8),

	// Types
	SF_FORMAT_WAV = (1 << 4),

	// Sub-type data (common)
	SF_FORMAT_PCM_S8 = (1 << 0),
	SF_FORMAT_PCM_16 = (2 << 0),
	SF_FORMAT_PCM_U8 = (5 << 0),
	SF_FORMAT_FLOAT  = (6 << 0),
	SF_FORMAT_DOUBLE = (7 << 0),

	SF_FORMAT_ENDMASK  = (0x3 << 8),
	SF_FORMAT_TYPEMASK = (0xf << 4),
	SF_FORMAT_SUBMASK  = (0xf << 0),
};

enum {
	SFM_READ = 0,
	SFM_WRITE = 1,
	SFM_RDWR = 2,
};

struct SF_INFO {
	int samplerate;
	int channels;
	int format;
};
typedef struct SF_INFO SF_INFO;

enum {
	SF_SEEK_SET = SEEK_SET,
	SF_SEEK_CUR = SEEK_CUR,
	SF_SEEK_END = SEEK_END,
};

struct SNDFILE;
typedef struct SNDFILE SNDFILE;

SNDFILE *sf_open(const char *path, int mode, SF_INFO *sf_info);
int sf_close(SNDFILE *);

// Unlike actual libsndfile, this will also ensure the on-disk file is valid by
// updating WAV headers as well.
void sf_write_sync(SNDFILE *);

int sf_error(SNDFILE *);

sf_count_t sf_seek(SNDFILE *, sf_count_t frames, int whence);

sf_count_t sf_readf_float(SNDFILE *, float *ptr, sf_count_t frames);
sf_count_t sf_writef_float(SNDFILE *, const float *ptr, sf_count_t frames);

const char *sf_strerror(SNDFILE *);

#endif

#endif
