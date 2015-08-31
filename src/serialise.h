/** \file
 *
 *  \brief Serialisation and deserialisation helpers.
 *
 *  \copyright Copyright 2015-2021 Ciaran Anscomb
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
 * A set of simple tools to aid in the serialisation and deserialisation of
 * data.  The general structure is (TAG,LENGTH,DATA), where LENGTH is the
 * length in bytes of DATA.  TAG and LENGTH are both written as variable-length
 * unsigned integers (vuint31).
 *
 * Nesting happens by default until a special closing zero byte tag reduces the
 * nesting level.
 *
 * Read and write helpers do NOT return special values on error, instead they
 * store the error code in the handle.  Caller should check this by calling
 * ser_error() at a convenient point.  Subsequent calls to helpers will take no
 * action if an error has been flagged, with read functions returning zero or
 * NULL.
 *
 * ser_close() will return any flagged error.
 *
 */

// When reading, any error is fatal, but it's ok to keep calling until a
// convenient point to check ser_error().

// Serialising:
//
// Helper functions are provided for common data types.  They emit the open
// tag, calculate the appropriate length and write the data followed by the
// close tag (zero byte).
//
// To nest, use ser_write_open_tag().  The DATA portion of the tag will be a
// string identifying the nested data.  No closing tag will be emitted until
// you call ser_write_close_tag().

// Deserialising:
//
// ser_read_tag() will fetch the next tag, skipping any data remaining in the
// current tag.  The first closing tag after a non-closing tag is skipped, but
// after that successive closing tags are returned to the caller to signal
// reduced nesting level.
//
// User can then decide how to read the tag's data, but helper functions for
// common types are included.
//
// ser_read_close_tag() will skip entries until the nesting level is reduced.

// ser_open*() will return NULL on error.  ser_close() will return zero or an
// error code.  ser_read_open_tag() will return -1 on EOF or error.
// ser_error() will return any current error code if needed before the call to
// ser_close().

#ifndef XROAR_SERIALISE_H_
#define XROAR_SERIALISE_H_

#include <stddef.h>
#include <stdio.h>

#include "sds.h"

enum ser_error {
	ser_error_none = 0,
	ser_error_file_io,  // error came from file i/o; might be EOF
	ser_error_bad_tag,  // negative tags not allowed
	ser_error_format,  // badly formatted data
	ser_error_bad_handle,  // NULL serialiser handle passed
	ser_error_system,  // see errno or ser_eof()
};

enum ser_mode {
	ser_mode_read,
	ser_mode_write
};

struct ser_handle;

/** \brief Open a file.
 * \param filename File to open.
 * \return New file handle or NULL on error.
 */
struct ser_handle *ser_open(const char *filename, enum ser_mode mode);

/** \brief Close a file.
 * \param sh Serialiser handle.
 * \return Zero or last error code.
 */
int ser_close(struct ser_handle *sh);

/** \brief Write an open tag, with length information.
 * \param sh Serialiser handle.
 * \param tag Tag to write (must be positive and non-zero).
 * \param length Data length.
 */
void ser_write_tag(struct ser_handle *sh, int tag, size_t length);

/** \brief Write a close tag.
 * \param sh Serialiser handle.
 */
void ser_write_close_tag(struct ser_handle *sh);

/** \brief Read the next open tag.
 * \param sh Serialiser handle.
 * \return Tag id (including zero for close tag).  Negative implies EOF or error.
 */
int ser_read_tag(struct ser_handle *sh);

/** \brief Number of bytes remaining in current tag's DATA.
 * \param sh Serialiser handle.
 * \return Bytes unread of current tag's DATA.
 */
size_t ser_data_length(struct ser_handle *sh);

/** \brief Test for end of file.
 * \param sh Serialiser handle.
 * \return Non-zero if end of file.
 */
int ser_eof(struct ser_handle *sh);

/** \brief Test error status.
 * \param sh Serialiser handle.
 * \return Zero or last error code.
 */
int ser_error(struct ser_handle *sh);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Write helpers.

void ser_write_int8(struct ser_handle *sh, int tag, int8_t v);
void ser_write_uint8(struct ser_handle *sh, int tag, uint8_t v);
void ser_write_int16(struct ser_handle *sh, int tag, int16_t v);
void ser_write_uint16(struct ser_handle *sh, int tag, uint16_t v);
void ser_write_vint32(struct ser_handle *sh, int tag, int v);
void ser_write_vuint32(struct ser_handle *sh, int tag, int v);
void ser_write_string(struct ser_handle *sh, int tag, const char *s);
void ser_write_sds(struct ser_handle *sh, int tag, const sds s);
void ser_write(struct ser_handle *sh, int tag, const void *ptr, size_t size);

// Open tag write helpers.

void ser_write_open_string(struct ser_handle *sh, int tag, const char *s);
void ser_write_open_sds(struct ser_handle *sh, int tag, const sds s);

// Untagged write helpers.

void ser_write_uint8_untagged(struct ser_handle *sh, uint8_t v);
void ser_write_untagged(struct ser_handle *sh, const void *ptr, size_t size);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Read helpers.

int8_t ser_read_int8(struct ser_handle *sh);
uint8_t ser_read_uint8(struct ser_handle *sh);
int16_t ser_read_int16(struct ser_handle *sh);
uint16_t ser_read_uint16(struct ser_handle *sh);
int32_t ser_read_vint32(struct ser_handle *sh);
uint32_t ser_read_vuint32(struct ser_handle *sh);
void ser_read(struct ser_handle *sh, void *ptr, size_t size);

// The following all allocate their own storage:

char *ser_read_string(struct ser_handle *sh);
sds ser_read_sds(struct ser_handle *sh);
void *ser_read_new(struct ser_handle *sh, size_t size);

#endif
