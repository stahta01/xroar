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

#include "top-config.h"

// Comment this out for debugging
#define SF_DEBUG(...)

#define _POSIX_C_SOURCE 200112L

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include "array.h"
#include "pl-endian.h"
#include "xalloc.h"

#include "fs.h"
#include "logging.h"
#include "sndfile_compat.h"

#ifndef SF_DEBUG
#define SF_DEBUG(...) LOG_PRINT(__VA_ARGS__)
#define SF_MOD_DEBUG(...) LOG_MOD_PRINT("sndfile_compat", __VA_ARGS__)
#else
#define SF_MOD_DEBUG(l,...)
#endif

#ifndef HAVE_SNDFILE

#if SIZEOF_FLOAT == 4
#define SUPPORT_FLOAT
#if SIZEOF_DOUBLE == 8
#define SUPPORT_DOUBLE
#endif
#endif

// Last error seen, useful when sf_open() fails
static int sndfile_compat_error = 0;

struct wav_data {
	// Offset into file of fact chunk data
	off_t fact_offset;
	// Offset into file of first byte of WAVE data chunk
	off_t data_offset;
};

struct SNDFILE {
	FILE *fd;
	int mode;
	int error;  // last error code

	unsigned fmt;

	_Bool wrong_endian;

	int framerate;
	int nchannels;
	int bytes_per_frame;  // == bytes_per_sample * nchannels
	int bytes_per_sample;

	// Sample data size in frames
	off_t data_size;

	// Current file offset in frames
	off_t offset;

	union {
		struct wav_data wav;
	} data;
};

static void set_error(SNDFILE *sf, int err);
static _Bool read_cc4(SNDFILE *sf, uint32_t *dst);
static _Bool read_uint8(SNDFILE *sf, uint8_t *dst);
static _Bool read_uint16(SNDFILE *sf, uint16_t *dst);
static _Bool read_uint32(SNDFILE *sf, uint32_t *dst);
static _Bool write_cc4(SNDFILE *sf, uint32_t v);
static _Bool write_uint8(SNDFILE *sf, uint8_t v);
static _Bool write_uint16(SNDFILE *sf, uint16_t v);
static _Bool write_uint32(SNDFILE *sf, uint32_t v);

#ifdef SUPPORT_FLOAT
static _Bool read_float(SNDFILE *sf, float *dst);
static _Bool write_float(SNDFILE *sf, float v);
#ifdef SUPPORT_DOUBLE
static _Bool read_double(SNDFILE *sf, double *dst);
static _Bool write_double(SNDFILE *sf, double v);
#endif
#endif

// Scan an opened SNDFILE for WAV chunk information

static _Bool wav_scan(SNDFILE *sf) {
	assert(sf != NULL);

	off_t old_position = ftello(sf->fd);
	if (fseeko(sf->fd, 0, SEEK_SET) != 0) {
		set_error(sf, SF_ERR_SYSTEM);
		return 0;
	}

	// RIFF or RIFX fourcc
	uint32_t riff = 0;
	_Bool error = !read_cc4(sf, &riff);
	if (!error && riff == 0x52494646) {
		// "RIFF" - little-endian
		sf->fmt = SF_FORMAT_WAV | SF_ENDIAN_LITTLE;
		sf->wrong_endian = 1;

	} else if (!error && riff == 0x52494658) {
		// "RIFX" - big-endian
		sf->fmt = SF_FORMAT_WAV | SF_ENDIAN_BIG;
		sf->wrong_endian = 0;
	} else if (!error) {
		error = 1;
		set_error(sf, SF_ERR_UNRECOGNISED_FORMAT);
	}

	// 32-bit RIFF length
	uint32_t riff_length = 0;
	error = error || !read_uint32(sf, &riff_length);
	if (riff_length < 4) {
		error = 1;
	}

	// WAV then requires "WAVE".  No length.
	uint32_t wave = 0;
	error = error || !read_cc4(sf, &wave);
	if (wave == 0x57415645) {
		// "WAVE"
		riff_length -= 4;
	} else {
		error = 1;
	}

	// data_length is calculated as the size of the data chunk divided by
	// the size of a frame.  dwFileSize is read from a "fact" chunk, and if
	// present, should match data_length.
	uint32_t data_length = 0;
	_Bool have_dwFileSize = 0;
	uint32_t dwFileSize = 0;

	// wFormatTag in the 'fmt ' chunk.  Declare here so that we can
	// disregard the contents of a 'fact' chunk if it's PCM.
	uint16_t wFormatTag = 0;

	while (!error) {
		if (riff_length < 8) {
			// not enough bytes left in RIFF to be any more
			// chunks - we're done!
			break;
		}
		uint32_t chunk = 0;
		uint32_t chunk_length = 0;
		error = !read_cc4(sf, &chunk);
		error = error || !read_uint32(sf, &chunk_length);
		riff_length -= 8;
		if (riff_length < chunk_length) {
			error = 1;
		}
		if (error) {
			break;
		}
		riff_length -= chunk_length;

		// TODO: wavl, slnt, fact, cue, plst, etc.

		switch (chunk) {
		case 0x666d7420:
			// "fmt "
			{
				uint16_t wChannels = 0;
				uint32_t dwSamplesPerSec = 0;
				uint32_t tmp32;
				uint16_t wBitsPerSample = 0;
				SF_MOD_DEBUG("'fmt ' chunk\n");

				// common-fields
				if (chunk_length < 16) {
					set_error(sf, SF_ERR_UNRECOGNISED_FORMAT);
					error = 1;
					break;
				}
				error = error || !read_uint16(sf, &wFormatTag);
				error = error || !read_uint16(sf, &wChannels);
				error = error || !read_uint32(sf, &dwSamplesPerSec);
				error = error || !read_uint32(sf, &tmp32);
				error = error || !read_uint16(sf, (uint16_t *)&tmp32);
				error = error || !read_uint16(sf, &wBitsPerSample);
				chunk_length -= 16;

				if (error) {
					SF_DEBUG("\terror reading chunk data\n");
					break;
				}

				SF_DEBUG("\twFormatTag=");
				if (wFormatTag == 0x0001) { SF_DEBUG("WAVE_FORMAT_PCM\n"); }
				else if (wFormatTag == 0x0003) { SF_DEBUG("WAVE_FORMAT_IEEE_FLOAT\n"); }
				else { SF_DEBUG("unknown\n"); }
				SF_DEBUG("\twBitsPerSample=%u\n", wBitsPerSample);

				if (wFormatTag == 0x0001) {
					// WAVE_FORMAT_PCM
					if (wBitsPerSample == 8) {
						sf->bytes_per_sample = 1;
						sf->fmt |= SF_FORMAT_PCM_U8;
						SF_DEBUG("\tunsigned 8-bit\n");
					} else if (wBitsPerSample == 16) {
						sf->bytes_per_sample = 2;
						sf->fmt |= SF_FORMAT_PCM_16;
						SF_DEBUG("\tsigned 16-bi6\n");
					} else {
						SF_DEBUG("\tunsupported encoding\n");
						set_error(sf, SF_ERR_UNSUPPORTED_ENCODING);
						error = 1;
					}
#ifdef SUPPORT_FLOAT
				} else if (wFormatTag == 0x0003) {
					// WAVE_FORMAT_IEEE_FLOAT
					// FLOAT data without an extension is
					// supposedly invalid, but we'll just
					// warn:
					if (chunk_length < 2) {
						LOG_MOD_WARN("sndfile_compat", "WAVE_FORMAT_IEEE_FLOAT should have 'fmt' extension\n");
					}
					if (wBitsPerSample == 32) {
						sf->bytes_per_sample = 4;
						sf->fmt |= SF_FORMAT_FLOAT;
						SF_DEBUG("\tfloat\n");
#ifdef SUPPORT_DOUBLE
					} else if (wBitsPerSample == 64) {
						sf->bytes_per_sample = 8;
						sf->fmt |= SF_FORMAT_DOUBLE;
						SF_DEBUG("\tdouble\n");
#endif
					} else {
						SF_DEBUG("unsupported encoding\n");
						set_error(sf, SF_ERR_UNSUPPORTED_ENCODING);
						error = 1;
						break;
					}
#endif  /* SUPPORT_FLOAT */
				} else {
					SF_MOD_DEBUG("unknown wFormatTag\n");
					set_error(sf, SF_ERR_UNSUPPORTED_ENCODING);
					error = 1;
					break;
				}

				sf->bytes_per_frame = sf->bytes_per_sample * wChannels;
				sf->nchannels = wChannels;
				sf->framerate = dwSamplesPerSec;
				SF_DEBUG("\twChannels=%d\n\tdwSamplesPerSec=%d\n", wChannels, dwSamplesPerSec);
			}
			break;

		case 0x66616374:
			// "fact"
			{
				SF_MOD_DEBUG("'fact' chunk\n");
				error = error || !read_uint32(sf, &dwFileSize);
				if (error) {
					SF_DEBUG("\terror reading chunk data\n");
					break;
				}
				SF_DEBUG("\tdwFileSize=%08x\n", dwFileSize);
				if (wFormatTag == 0x0001) {
					// WAVE_FORMAT_PCM
					SF_DEBUG("\tdisregarding for WAVE_FORMAT_PCM\n");
				} else {
					have_dwFileSize = 1;
				}
				chunk_length -= 4;
			}
			break;

		case 0x64617461:
			// "data"
			sf->data.wav.data_offset = ftello(sf->fd);
			data_length = chunk_length;
			break;

		default:
			// unknown chunk - skip it
			break;
		}

		// skip any remaining bytes in the chunk
		if (fseeko(sf->fd, chunk_length, SEEK_CUR) != 0) {
			error = 1;
		}
	}

	if (!error && sf->bytes_per_frame == 0) {
		set_error(sf, SF_ERR_MALFORMED_FILE);
		error = 1;
	}

	if (!error) {
		sf->data_size = data_length / sf->bytes_per_frame;
		if (have_dwFileSize && dwFileSize != sf->data_size) {
			set_error(sf, SF_ERR_MALFORMED_FILE);
			error = 1;
		}
	}

	if (!error && sf->nchannels == 0) {
		set_error(sf, SF_ERR_MALFORMED_FILE);
		error = 1;
	}

	if (!error) {
		// If no error, then this was recognised as a WAV file.
		// Seek to beginning of sample data ready to read.
		fseeko(sf->fd, sf->data.wav.data_offset, SEEK_SET);
		sf->offset = 0;
	} else {
		// If there was an error, seek to the position we got on entry.
		fseeko(sf->fd, old_position, SEEK_SET);
		set_error(sf, SF_ERR_MALFORMED_FILE);
	}

	return error;
}

// Write a WAV header to open file

static _Bool wav_write_header(SNDFILE *sf) {
	assert(sf != NULL);

	uint32_t riff_length = 36;
	uint32_t fmt_length = 16;
	uint16_t wFormatTag = 0x0001;  // WAVE_FORMAT_PCM
	_Bool want_fact = 0;

	if ((sf->fmt & SF_FORMAT_SUBMASK) == SF_FORMAT_FLOAT ||
	    (sf->fmt & SF_FORMAT_SUBMASK) == SF_FORMAT_DOUBLE) {
		wFormatTag = 0x0003;  // WAVE_FORMAT_IEEE_FLOAT
		fmt_length += 2;
		riff_length += 2;  // 2 extra bytes in fmt
		want_fact = 1;
		riff_length += 12;  // fact chunk takes 12 bytes
	}

	_Bool error = 0;
	// RIFF or RIFX fourcc
	if (sf->wrong_endian) {
		error = error || !write_cc4(sf, 0x52494646);  // "RIFF"
	} else {
		error = error || !write_cc4(sf, 0x52494658);  // "RIFX"
	}
	// 32-bit RIFF length
	error = error || !write_uint32(sf, riff_length);

	// WAVE fourcc
	error = error || !write_cc4(sf, 0x57415645);  // "WAVE"

	// fmt chunk
	error = error || !write_cc4(sf, 0x666d7420);  // "fmt "
	error = error || !write_uint32(sf, fmt_length);  // 16 or 18 bytes

	// fmt common-fields
	error = error || !write_uint16(sf, wFormatTag);
	error = error || !write_uint16(sf, sf->nchannels);
	error = error || !write_uint32(sf, sf->framerate);
	error = error || !write_uint32(sf, sf->framerate * sf->bytes_per_frame);
	error = error || !write_uint16(sf, sf->bytes_per_frame);
	error = error || !write_uint16(sf, sf->bytes_per_sample * 8);
	fmt_length -= 16;

	// fmt extension
	if (fmt_length >= 2) {
		// cbSize should be present for float formats, but
		// needn't actually have anything in it, so zero size:
		error = error || !write_uint16(sf, 0);
		fmt_length -= 2;
	}
	assert(fmt_length == 0);

	// fact chunk
	if (want_fact) {
		error = error || !write_cc4(sf, 0x66616374);  // "fact"
		error = error || !write_uint32(sf, 4);
		sf->data.wav.fact_offset = ftello(sf->fd);
		error = error || !write_uint32(sf, 0);
	}

	// data chunk
	error = error || !write_cc4(sf, 0x64617461);  // "data"
	error = error || !write_uint32(sf, 0);  // 0 bytes

	sf->data_size = 0;
	sf->data.wav.data_offset = ftello(sf->fd);

	return error;
}

// Update previously-written header in-place, preserving file position.
// Returns error code.

static int wav_update_header(SNDFILE *sf) {
	assert(sf != NULL);
	assert(sf->mode == SFM_WRITE || sf->mode == SFM_RDWR);

	off_t old_position = ftello(sf->fd);
	if (old_position < 0)
		return SF_ERR_SYSTEM;

	uint32_t data_bytes = sf->data_size * sf->bytes_per_frame;
	// Turns out the data chunk has to be an even number of bytes:
	if (data_bytes & 1) {
		off_t byte_offset = sf->data.wav.data_offset + (sf->data_size * sf->bytes_per_frame);
		if (fseeko(sf->fd, byte_offset, SEEK_SET) < 0)
			return SF_ERR_SYSTEM;
		if (!write_uint8(sf, 0))
			return SF_ERR_SYSTEM;
		data_bytes++;
	}
	if (fseeko(sf->fd, sf->data.wav.data_offset - 4, SEEK_SET) < 0)
		return SF_ERR_SYSTEM;
	if (!write_uint32(sf, data_bytes))
		return SF_ERR_SYSTEM;
	if (sf->data.wav.fact_offset > 0) {
		if (fseeko(sf->fd, sf->data.wav.fact_offset, SEEK_SET) < 0)
			return SF_ERR_SYSTEM;
		if (!write_uint32(sf, sf->data_size))
			return SF_ERR_SYSTEM;
	}
	if (fseeko(sf->fd, 4, SEEK_SET) < 0)
		return SF_ERR_SYSTEM;
	uint32_t riff_bytes = sf->data.wav.data_offset + data_bytes - 8;
	if (!write_uint32(sf, riff_bytes))
		return SF_ERR_SYSTEM;

	if (fseeko(sf->fd, old_position, SEEK_SET) < 0)
		return SF_ERR_SYSTEM;

	return SF_ERR_NO_ERROR;
}

SNDFILE *sf_open(const char *path, int mode, SF_INFO *sf_info) {
	sndfile_compat_error = SF_ERR_NO_ERROR;

	const char *fmode;
	if (mode == SFM_WRITE) {
		fmode = "wb";
	} else if (mode == SFM_RDWR) {
		fmode = "r+b";
	} else {
		mode = SFM_READ;
		fmode = "rb";
	}

	FILE *fd = fopen(path, fmode);
	if (!fd && mode == SFM_RDWR)
		fd = fopen(path, "w+b");
	if (!fd) {
		sndfile_compat_error = SF_ERR_SYSTEM;
		return NULL;
	}

	SNDFILE *sf = xmalloc(sizeof(*sf));
	*sf = (SNDFILE){0};
	sf->fd = fd;
	sf->mode = mode;

	// No magic - this is WAV-only code, so we attempt to treat it as a WAV
	// file and if something breaks we return an error.

	_Bool error = 0;

	if (fs_file_size(fd) == 0 && mode != SFM_READ) {
		// No data in file, need to write a WAV header
		assert(sf_info != NULL);

		// Only support WAV
		if ((sf_info->format & SF_FORMAT_TYPEMASK) != SF_FORMAT_WAV) {
			set_error(sf, SF_ERR_UNRECOGNISED_FORMAT);
			error = 1;
		}

		// Determine endianness for output
		sf->fmt = sf_info->format;
		switch (sf->fmt & SF_FORMAT_ENDMASK) {
		default:
			sf->wrong_endian = 1;
			break;

		case SF_ENDIAN_CPU:
			sf->wrong_endian = (__BYTE_ORDER != __BIG_ENDIAN);
			break;

		case SF_ENDIAN_BIG:
			sf->wrong_endian = 0;
			break;
		}

		sf->framerate = sf_info->samplerate;
		sf->nchannels = sf_info->channels;
		switch (sf_info->format & SF_FORMAT_SUBMASK) {

#ifdef SUPPORT_FLOAT
		case SF_FORMAT_FLOAT:
			sf->bytes_per_sample = 4;
			break;
#ifdef SUPPORT_DOUBLE
		case SF_FORMAT_DOUBLE:
			sf->bytes_per_sample = 8;
			break;
#endif
#endif

		case SF_FORMAT_PCM_16:
			sf->bytes_per_sample = 2;
			break;

		default:
			sf->bytes_per_sample = 1;
			break;
		}
		sf->bytes_per_frame = sf->bytes_per_sample * sf->nchannels;

		// Write the WAV header
		error = error || wav_write_header(sf);

	} else {
		// Read-only or read-write
		error = error || wav_scan(sf);

		if (sf_info) {
			sf_info->samplerate = sf->framerate;
			sf_info->channels = sf->nchannels;
			sf_info->format = sf->fmt;
		}
	}

	if (error) {
		sndfile_compat_error = sf->error;
		free(sf);
		fclose(fd);
		return NULL;
	}

	return sf;
}

int sf_close(SNDFILE *sf) {
	if (!sf)
		return SF_ERR_SYSTEM;
	if (sf->mode == SFM_WRITE || sf->mode == SFM_RDWR) {
		int err = wav_update_header(sf);
		if (err != SF_ERR_NO_ERROR)
			return err;
	}
	fclose(sf->fd);
	free(sf);
	sndfile_compat_error = SF_ERR_NO_ERROR;
	return SF_ERR_NO_ERROR;
}

void sf_write_sync(SNDFILE *sf) {
	int err = wav_update_header(sf);
	if (err != SF_ERR_NO_ERROR) {
		set_error(sf, err);
	} else {
		fflush(sf->fd);
	}
}

int sf_error(SNDFILE *sf) {
	if (!sf)
		return sndfile_compat_error;
	return sf->error;
}

sf_count_t sf_seek(SNDFILE *sf, sf_count_t frames, int whence) {
	// TODO: if WAV playlist support implemented, will need to account for
	// it here
	if (!sf || sf->error) {
		return -1;
	}

	off_t frame_offset;
	switch (whence) {
	case SF_SEEK_SET:
	default:
		frame_offset = frames;
		break;

	case SF_SEEK_CUR:
		frame_offset = sf->offset + frames;
		break;

	case SF_SEEK_END:
		frame_offset = sf->data_size - frames;
		break;
	}

	if (frame_offset < 0) {
		frame_offset = 0;
	} else if (frame_offset > sf->data_size) {
		frame_offset = sf->data_size;
	}

	off_t byte_offset = sf->data.wav.data_offset + (frame_offset * sf->bytes_per_frame);
	if (fseeko(sf->fd, byte_offset, SEEK_SET) < 0) {
		set_error(sf, SF_ERR_SYSTEM);
		return -1;
	}
	sf->offset = frame_offset;

	return frame_offset;
}

static _Bool read_frame_float(SNDFILE *sf, float *dst) {
	switch (sf->fmt & SF_FORMAT_SUBMASK) {
	case SF_FORMAT_PCM_S8:
		for (int i = sf->nchannels; i; i--) {
			int8_t sample = 0;
			if (!read_uint8(sf, (uint8_t *)&sample))
				return 0;
			uint8_t usample = sample + 0x80;
			*(dst++) = ((float)usample / 127.5) - 1.;
		}
		return 1;

	case SF_FORMAT_PCM_U8:
		for (int i = sf->nchannels; i; i--) {
			uint8_t sample = 0;
			if (!read_uint8(sf, &sample))
				return 0;
			*(dst++) = ((float)sample / 127.5) - 1.;
		}
		return 1;

	case SF_FORMAT_PCM_16:
		for (int i = sf->nchannels; i; i--) {
			int16_t sample = 0;
			if (!read_uint16(sf, (uint16_t *)&sample))
				return 0;
			uint32_t usample = sample + 0x8000;
			*(dst++) = ((float)usample / 32767.5) - 1.;
		}
		return 1;

#ifdef SUPPORT_FLOAT
	case SF_FORMAT_FLOAT:
		for (int i = sf->nchannels; i; i--) {
			float sample = 0.0;
			if (!read_float(sf, &sample))
				return 0;
			*(dst++) = sample;
		}
		return 1;

#ifdef SUPPORT_DOUBLE
	case SF_FORMAT_DOUBLE:
		for (int i = sf->nchannels; i; i--) {
			double sample = 0.0;
			if (!read_double(sf, &sample))
				return 0;
			*(dst++) = (float)sample;
		}
		return 1;
#endif
#endif

	default:
		break;
	}
	return 0;
}

sf_count_t sf_readf_float(SNDFILE *sf, float *ptr, sf_count_t frames) {
	if (!sf || sf->error) {
		return -1;
	}
	sf_count_t nread = 0;
	for (; frames > 0; frames--) {
		if (sf->offset >= sf->data_size)
			break;
		if (!read_frame_float(sf, ptr))
			break;
		ptr += sf->nchannels;
		nread++;
		sf->offset++;
	}
	if (sf->error)
		return -1;
	return nread;
}

static _Bool write_frame_float(SNDFILE *sf, const float *src) {
	switch (sf->fmt & SF_FORMAT_SUBMASK) {
	case SF_FORMAT_PCM_S8:
		for (int i = sf->nchannels; i; i--) {
			float fsample = *(src++);
			if (fsample < -1.0)
				fsample = -1.0;
			if (fsample > 1.0)
				fsample = 1.0;
			int8_t sample = fsample * 127.5;
			if (!write_uint8(sf, (uint8_t)sample))
				return 0;
		}
		return 1;

	case SF_FORMAT_PCM_U8:
		for (int i = sf->nchannels; i; i--) {
			float fsample = *(src++);
			if (fsample < -1.0)
				fsample = -1.0;
			if (fsample > 1.0)
				fsample = 1.0;
			uint8_t sample = (int)(fsample * 127.5) + 0x80;
			if (!write_uint8(sf, sample))
				return 0;
		}
		return 1;

	case SF_FORMAT_PCM_16:
		for (int i = sf->nchannels; i; i--) {
			float fsample = *(src++);
			if (fsample < -1.0)
				fsample = -1.0;
			if (fsample > 1.0)
				fsample = 1.0;
			int16_t sample = fsample * 32767.5;
			if (!write_uint16(sf, (uint16_t)sample))
				return 0;
		}
		return 1;

#ifdef SUPPORT_FLOAT
	case SF_FORMAT_FLOAT:
		for (int i = sf->nchannels; i; i--) {
			float sample = *(src++);
			if (sample < -1.0)
				sample = -1.0;
			if (sample > 1.0)
				sample = 1.0;
			if (!write_float(sf, sample))
				return 0;
		}
		return 1;

#ifdef SUPPORT_DOUBLE
	case SF_FORMAT_DOUBLE:
		for (int i = sf->nchannels; i; i--) {
			double sample = *(src++);
			if (sample < -1.0)
				sample = -1.0;
			if (sample > 1.0)
				sample = 1.0;
			if (!write_double(sf, sample))
				return 0;
		}
		return 1;
#endif

#endif

	default:
		break;
	}
	return 0;
}

sf_count_t sf_writef_float(SNDFILE *sf, const float *ptr, sf_count_t frames) {
	if (!sf || sf->error) {
		return -1;
	}
	sf_count_t nwritten = 0;
	for (; frames > 0; frames--) {
		if (!write_frame_float(sf, ptr))
			break;
		ptr += sf->nchannels;
		nwritten++;
		sf->offset++;
	}
	if (sf->offset > sf->data_size)
		sf->data_size = sf->offset;
	if (sf->error)
		return -1;
	return nwritten;
}

static const char *sf_error_string[] = {
	"no error",
	"unrecognised format",
	"system error",
	"malformed file",
	"unsupported encoding",
};

const char *sf_strerror(SNDFILE *sf) {
	int err = sndfile_compat_error;
	if (sf) {
		err = sf->error;
	}
	if (err == SF_ERR_SYSTEM) {
		return strerror(errno);
	}
	if (err >= 0 && err < (int)ARRAY_N_ELEMENTS(sf_error_string)) {
		return sf_error_string[err];
	}
	return "unknown error";
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Set error state if not already set

static void set_error(SNDFILE *sf, int err) {
	if (!sf) {
		sndfile_compat_error = err;
		return;
	}
	if (sf->error != 0)
		return;
	sndfile_compat_error = err;
	sf->error = err;
}

static _Bool read_cc4(SNDFILE *sf, uint32_t *dst) {
	assert(dst != NULL);
	int msb = fs_read_uint16(sf->fd);
	int lsb = fs_read_uint16(sf->fd);
	if (msb < 0 || lsb < 0) {
		set_error(sf, SF_ERR_SYSTEM);
		errno = ferror(sf->fd);
		return 0;
	}
	*dst = (msb << 16) | lsb;
	return 1;
}

static _Bool read_uint8(SNDFILE *sf, uint8_t *dst) {
	assert(dst != NULL);
	int v = fs_read_uint8(sf->fd);
	if (v < 0) {
		set_error(sf, SF_ERR_SYSTEM);
		errno = ferror(sf->fd);
		return 0;
	}
	*dst = v;
	return 1;
}

static _Bool read_uint16(SNDFILE *sf, uint16_t *dst) {
	assert(dst != NULL);
	int v = sf->wrong_endian ? fs_read_uint16_le(sf->fd) : fs_read_uint16(sf->fd);
	if (v < 0) {
		set_error(sf, SF_ERR_SYSTEM);
		errno = ferror(sf->fd);
		return 0;
	}
	*dst = v;
	return 1;
}

static _Bool read_uint32(SNDFILE *sf, uint32_t *dst) {
	uint16_t w0, w1;
	if (!read_uint16(sf, &w0))
		return 0;
	if (!read_uint16(sf, &w1))
		return 0;
	if (sf->wrong_endian) {
		*dst = (w1 << 16) | w0;
	} else {
		*dst = (w0 << 16) | w1;
	}
	return 1;
}

#ifdef SUPPORT_FLOAT

static _Bool read_float(SNDFILE *sf, float *dst) {
	_Bool error = 0;
	uint8_t sample[4] = {0};
	if (sf->wrong_endian == (__BYTE_ORDER != __BIG_ENDIAN)) {
		for (int i = 0; i < 4; i++)
			error = error || !read_uint8(sf, &sample[i]);
	} else {
		for (int i = 0; i < 4; i++)
			error = error || !read_uint8(sf, &sample[3-i]);
	}
	if (error)
		return 0;
	*dst = *((float *)sample);
	return 1;
}

#ifdef SUPPORT_DOUBLE
static _Bool read_double(SNDFILE *sf, double *dst) {
	_Bool error = 0;
	uint8_t sample[8] = {0};
	if (sf->wrong_endian == (__BYTE_ORDER != __BIG_ENDIAN)) {
		for (int i = 0; i < 8; i++)
			error = error || !read_uint8(sf, &sample[i]);
	} else {
		for (int i = 0; i < 8; i++)
			error = error || !read_uint8(sf, &sample[7-i]);
	}
	if (error)
		return 0;
	*dst = *((double *)sample);
	return 1;
}
#endif

#endif

static _Bool write_cc4(SNDFILE *sf, uint32_t v) {
	if (fs_write_uint16(sf->fd, v >> 16) != 2)
		return 0;
	if (fs_write_uint16(sf->fd, v) != 2)
		return 0;
	return 1;
}

static _Bool write_uint8(SNDFILE *sf, uint8_t v) {
	if (fs_write_uint8(sf->fd, v) != 1) {
		set_error(sf, SF_ERR_SYSTEM);
		errno = ferror(sf->fd);
		return 0;
	}
	return 1;
}

static _Bool write_uint16(SNDFILE *sf, uint16_t v) {
	int r = sf->wrong_endian ? fs_write_uint16_le(sf->fd, v) : fs_write_uint16(sf->fd, v);
	if (r != 2) {
		set_error(sf, SF_ERR_SYSTEM);
		errno = ferror(sf->fd);
		return 0;
	}
	return 1;
}

static _Bool write_uint32(SNDFILE *sf, uint32_t v) {
	if (sf->wrong_endian) {
		if (!write_uint16(sf, v))
			return 0;
		if (!write_uint16(sf, v >> 16))
			return 0;
	} else {
		if (!write_uint16(sf, v >> 16))
			return 0;
		if (!write_uint16(sf, v))
			return 0;
	}
	return 1;
}

#ifdef SUPPORT_FLOAT

static _Bool write_float(SNDFILE *sf, float v) {
	_Bool error = 0;
	uint8_t sample[4];
	*(float *)sample = v;
	if (sf->wrong_endian == (__BYTE_ORDER != __BIG_ENDIAN)) {
		for (int i = 0; i < 4; i++)
			error = error || !write_uint8(sf, sample[i]);
	} else {
		for (int i = 0; i < 4; i++)
			error = error || !write_uint8(sf, sample[3-i]);
	}
	return !error;
}

#ifdef SUPPORT_DOUBLE
static _Bool write_double(SNDFILE *sf, double v) {
	_Bool error = 0;
	uint8_t sample[8];
	*(double *)sample = v;
	if (sf->wrong_endian == (__BYTE_ORDER != __BIG_ENDIAN)) {
		for (int i = 0; i < 8; i++)
			error = error || !write_uint8(sf, sample[i]);
	} else {
		for (int i = 0; i < 8; i++)
			error = error || !write_uint8(sf, sample[7-i]);
	}
	return !error;
}
#endif

#endif

#endif
