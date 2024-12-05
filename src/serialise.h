/** \file
 *
 *  \brief Serialisation and deserialisation helpers.
 *
 *  \copyright Copyright 2015-2024 Ciaran Anscomb
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
 * unsigned integers (vuint32).
 *
 * Nesting happens by default until a special closing zero byte tag reduces the
 * nesting level.
 *
 * Most read and write helpers do NOT return special values on error, instead
 * they store the error code in the handle.  Caller should check this by
 * calling ser_error() at a convenient point.  Subsequent calls to helpers will
 * take no action if an error has been flagged, with read functions returning
 * zero or NULL.
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

// Deserialising:
//
// ser_read_tag() will fetch the next tag, skipping any data remaining in the
// current tag.  The first closing tag after a non-closing tag is skipped, but
// after that successive closing tags are returned to the caller to signal
// reduced nesting level.  For this reason, nesting requires at least one
// nested tag to be present.
//
// User can then decide how to read the tag's data, but helper functions for
// common types are included.

// ser_open*() will return NULL on error.  ser_close() will return zero or an
// error code.  ser_error() will return any current error code if needed before
// the call to ser_close().

#ifndef XROAR_SERIALISE_H_
#define XROAR_SERIALISE_H_

#include <stddef.h>
#include <stdio.h>

#include "sds.h"

enum ser_error {
	ser_error_none = 0,
	ser_error_file_io,  // error came from file i/o; might be EOF
	ser_error_bad_tag,  // negative tag, or unknown in read struct
	ser_error_format,  // badly formatted data
	ser_error_bad_handle,  // NULL serialiser handle passed
	ser_error_system,  // see errno or ser_eof()
	ser_error_type,  // bad type found during struct read/write
};

enum ser_mode {
	ser_mode_read,
	ser_mode_write
};

struct ser_handle;

// Struct reading & writing

struct ser_struct_data;

/** \brief For marking up struct members in a struct ser_struct.
 */
enum ser_type {
	ser_type_bool,
	ser_type_int,
	ser_type_unsigned,
	ser_type_int8,
	ser_type_uint8,
	ser_type_int16,
	ser_type_uint16,
	ser_type_int32,
	ser_type_uint32,
	ser_type_tick,  // event_tick relative to current time
	ser_type_event,  // tick delta only written if queued
	ser_type_eventp,  // pointer to event (only read/write if non-null)
	ser_type_string,  // only written if non-null
	ser_type_sds,  // only written if non-null
	ser_type_sds_list,  // slist of sds strings
	ser_type_unhandled,  // returns control to caller
	ser_type_nest,  // recurse using data.ser_struct_data
};

// Because both int and unsigned will usually tend to be one of these (could be
// 64-bit, but likely not), we need to split our _Generic match up.  This is
// safe as they'll be accessed and read/written in the same way.

#define ser_type_for(m) _Generic((m), \
		int: ser_type_int, \
		unsigned: ser_type_unsigned, \
		default: _Generic((m), \
			_Bool: ser_type_bool, \
			int8_t: ser_type_int8, \
			uint8_t: ser_type_uint8, \
			int16_t: ser_type_int16, \
			uint16_t: ser_type_uint16, \
			int32_t: ser_type_int32, \
			uint32_t: ser_type_uint32, \
			char *: ser_type_string ) )

/** \brief Describes a struct member. */
struct ser_struct {
	// While transitioning old code, a tag ID of 0 implies that ID is equal
	// to its index into the metadata array + 1.
	uint16_t tag;
	// Type is one of enum ser_type.
	uint8_t type;
	// Offset within the struct of member.
	size_t offset;
	// Extra data - currently only used when nesting ser_struct_data
	// definitions.
	union {
		const struct ser_struct_data *ser_struct_data;
	} data;
};

#define SER_ID_STRUCT_TYPE(i,t,s,e) { .tag = (i), .type = t, .offset = offsetof(s,e) }
#define SER_ID_STRUCT_ELEM(i,s,e) { .tag = (i), .type = ser_type_for(((s *)0)->e), .offset = offsetof(s,e) }
#define SER_ID_STRUCT_UNHANDLED(i) { .tag = (i), .type = ser_type_unhandled }
#define SER_ID_STRUCT_SUBSTRUCT(i,s,e,d) { .tag = (i), .type = ser_type_nest, .offset = offsetof(s,e), .data.ser_struct_data = d }
#define SER_ID_STRUCT_NEST(i,d) { .tag = (i), .type = ser_type_nest, .offset = 0, .data.ser_struct_data = d }

// Collects a list of ser_struct member metadata with the size of the list,
// and external handlers to deal with members of type ser_type_unhandled.

struct ser_struct_data {
	const struct ser_struct *elems;
	const unsigned num_elems;
	_Bool (* const read_elem)(void *sptr, struct ser_handle *sh, int tag);
	_Bool (* const write_elem)(void *sptr, struct ser_handle *sh, int tag);
};

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

/** \brief Set error status.
 * \param sh Serialiser handle.
 * \param error Error code.
 *
 * Usually called by deserialisers to report a format error.
 */
void ser_set_error(struct ser_handle *sh, int error);

/** \brief Get error string.
 */

const char *ser_errstr(struct ser_handle *sh);

/** \brief Get file position when error occurred.
 */

ssize_t ser_errpos(struct ser_handle *sh);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Write helpers.

void ser_write_int8(struct ser_handle *sh, int tag, int8_t v);
void ser_write_uint8(struct ser_handle *sh, int tag, uint8_t v);
void ser_write_int16(struct ser_handle *sh, int tag, int16_t v);
void ser_write_uint16(struct ser_handle *sh, int tag, uint16_t v);
void ser_write_vint32(struct ser_handle *sh, int tag, int v);
void ser_write_vuint32(struct ser_handle *sh, int tag, uint32_t v);
void ser_write_string(struct ser_handle *sh, int tag, const char *s);
void ser_write_sds(struct ser_handle *sh, int tag, const sds s);
void ser_write(struct ser_handle *sh, int tag, const void *ptr, size_t size);

void ser_write_array_uint8(struct ser_handle *sh, int tag, uint8_t *src, size_t nelems);
void ser_write_array_uint16(struct ser_handle *sh, int tag, uint16_t *src, size_t nelems);

// Open tag write helpers.

void ser_write_open_vuint32(struct ser_handle *sh, int tag, uint32_t v);
void ser_write_open_string(struct ser_handle *sh, int tag, const char *s);
void ser_write_open_sds(struct ser_handle *sh, int tag, const sds s);

// Untagged write helpers.

void ser_write_uint8_untagged(struct ser_handle *sh, uint8_t v);
void ser_write_uint16_untagged(struct ser_handle *sh, uint16_t v);
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

// For array read helpers, dst is a _pointer_ to the destination pointer, which
// will be allocated if NULL.  nelems is the maximum number of elements
// allowed.  Returns actual number of elements read, called can raise a format
// error if mismatch is a bad thing.

size_t ser_read_array_uint8(struct ser_handle *sh, uint8_t **dst, size_t nelems);
size_t ser_read_array_uint16(struct ser_handle *sh, uint16_t **dst, size_t nelems);

// The following all allocate their own storage:

char *ser_read_string(struct ser_handle *sh);
sds ser_read_sds(struct ser_handle *sh);
void *ser_read_new(struct ser_handle *sh, size_t size);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Struct writer.  Writes fields in order, calling ss->write_elem when type is
// unhandled.

void ser_write_struct_data(struct ser_handle *sh, const struct ser_struct_data *ss, void *s);

// Struct reader.  Reads data into struct until closing tag.  Unhandled tags
// call ss->read_elem.

void ser_read_struct_data(struct ser_handle *sh, const struct ser_struct_data *ss, void *s);

#endif
