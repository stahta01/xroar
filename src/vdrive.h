/*  XRoar - a Dragon/Tandy Coco emulator
 *  Copyright (C) 2003-2017  Ciaran Anscomb
 *
 *  See COPYING.GPL for redistribution conditions. */

/* Implements virtual disks in a set of drives */

#ifndef XROAR_VDRIVE_H_
#define XROAR_VDRIVE_H_

#include <stdint.h>

#include "delegate.h"

struct vdisk;

#define VDRIVE_MAX_DRIVES (4)

/* Interface to be connected to a disk controller. */

struct vdrive_interface {
	// Signal callbacks
	DELEGATE_T1(void,bool) ready;
	DELEGATE_T1(void,bool) tr00;
	DELEGATE_T1(void,bool) index_pulse;
	DELEGATE_T1(void,bool) write_protect;

	// UI callbacks
	DELEGATE_T3(void,unsigned,unsigned,unsigned) update_drive_cyl_head;

	// Signals to all drives
	void (*set_dirc)(void *sptr, int dirc);
	void (*set_dden)(void *sptr, _Bool dden);
	void (*set_sso)(void *sptr, unsigned sso);

	// Drive select
	void (*set_drive)(struct vdrive_interface *vi, unsigned drive);

	// Operations on selected drive
	unsigned (*get_head_pos)(void *sptr);
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

void vdrive_insert_disk(struct vdrive_interface *vi, unsigned drive, struct vdisk *disk);
void vdrive_eject_disk(struct vdrive_interface *vi, unsigned drive);
struct vdisk *vdrive_disk_in_drive(struct vdrive_interface *vi, unsigned drive);

#endif
