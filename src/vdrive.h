/** \file
 *
 *  \brief Virtual floppy drives.
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
 *
 *  Implements virtual disks in a set of drives
 */

#ifndef XROAR_VDRIVE_H_
#define XROAR_VDRIVE_H_

#include <stdint.h>

#include "delegate.h"

struct ser_handle;
struct vdisk;

#define VDRIVE_MAX_DRIVES (4)

// Used to update disk UI

struct vdrive_info {
	unsigned drive;
	unsigned cylinder;
	unsigned head;
};

/* Interface to be connected to a disk controller. */

struct vdrive_interface {
	// Signal callbacks
	DELEGATE_T1(void,bool) ready;
	DELEGATE_T1(void,bool) tr00;
	DELEGATE_T1(void,bool) index_pulse;
	DELEGATE_T1(void,bool) write_protect;

	// Signals to all drives
	void (*set_dirc)(void *sptr, _Bool dirc);
	void (*set_dden)(void *sptr, _Bool dden);
	void (*set_sso)(void *sptr, unsigned sso);

	// Drive select
	void (*set_drive)(struct vdrive_interface *vi, unsigned drive);

	// Operations on selected drive
	void (*step)(void *sptr);
	void (*write)(void *sptr, uint8_t data);
	void (*skip)(void *sptr);
	uint8_t (*read)(void *sptr);
	void (*write_idam)(void *sptr);
	unsigned (*time_to_next_byte)(void *sptr);
	unsigned (*time_to_next_idam)(void *sptr);
	uint8_t *(*next_idam)(void *sptr);
	void (*update_connection)(void *sptr);
};

struct vdrive_interface *vdrive_interface_new(void);
void vdrive_interface_free(struct vdrive_interface *vi);
void vdrive_disconnect(struct vdrive_interface *vi);

void vdrive_interface_serialise(struct vdrive_interface *vi, struct ser_handle *sh, unsigned otag);
void vdrive_interface_deserialise(struct vdrive_interface *vi, struct ser_handle *sh);

void vdrive_insert_disk(struct vdrive_interface *vi, unsigned drive, struct vdisk *disk);
void vdrive_eject_disk(struct vdrive_interface *vi, unsigned drive);
struct vdisk *vdrive_disk_in_drive(struct vdrive_interface *vi, unsigned drive);

void vdrive_flush(struct vdrive_interface *vi);

#endif
