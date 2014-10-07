/*
 *  extfs.cpp - MacOS file system for access native file system access
 *
 *  Basilisk II (C) 1997-1999 Christian Bauer
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 *  SEE ALSO
 *    Guide to the File System Manager (from FSM 1.2 SDK)
 *
 *  TODO
 *    LockRng
 *    UnlockRng
 *    (CatSearch)
 *    (MakeFSSpec)
 *    (GetVolMountInfoSize)
 *    (GetVolMountInfo)
 *    (GetForeignPrivs)
 *    (SetForeignPrivs)
 */

#include "sysdeps.h"

/***
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
****/


#include "cpu_emulation.h"
#include "macos_util.h"
#include "emul_op.h"
#include "main.h"
#include "disk.h"
#include "prefs.h"
#include "user_strings.h"
#include "extfs.h"
#include "extfs_defs.h"

#define DEBUG 0
#include "debug.h"


// File system global data and 68k routines
enum {
	fsCommProcStub = 0,
	fsHFSProcStub = 6,
	fsDrvStatus = 12,				// Drive Status record
	fsFSD = 42,						// File system descriptor
	fsPB = 238,						// IOParam (for mounting and renaming)
	fsVMI = 288,					// VoumeMountInfoHeader (for mounting)
	fsParseRec = 296,				// ParsePathRec struct
	fsReturn = 306,					// Area for return data of 68k routines
	fsAllocateVCB = 562,			// UTAllocateVCB(uint16 *sysVCBLength{a0}, uint32 *vcb{a1})
	fsAddNewVCB = 578,				// UTAddNewVCB(int drive_number{d0}, int16 *vRefNum{a1}, uint32 vcb{a1})
	fsDetermineVol = 594,			// UTDetermineVol(uint32 pb{a0}, int16 *status{a1}, int16 *more_matches{a2}, int16 *vRefNum{a3}, uint32 *vcb{a4})
	fsResolveWDCB = 614,			// UTResolveWDCB(uint32 procID{d0}, int16 index{d1}, int16 vRefNum{d0}, uint32 *wdcb{a0})
	fsGetDefaultVol = 632,			// UTGetDefaultVol(uint32 wdpb{a0})
	fsGetPathComponentName = 644,	// UTGetPathComponentName(uint32 rec{a0})
	fsParsePathname = 656,			// UTParsePathname(uint32 *start{a0}, uint32 name{a1})
	fsDisposeVCB = 670,				// UTDisposeVCB(uint32 vcb{a0})
	fsCheckWDRefNum = 682,			// UTCheckWDRefNum(int16 refNum{d0})
	fsSetDefaultVol = 694,			// UTSetDefaultVol(uint32 dummy{d0}, int32 dirID{d1}, int16 refNum{d2})
	fsAllocateFCB = 710,			// UTAllocateFCB(int16 *refNum{a0}, uint32 *fcb{a1})
	fsReleaseFCB = 724,				// UTReleaseFCB(int16 refNum{d0})
	fsIndexFCB = 736,				// UTIndexFCB(uint32 vcb{a0}, int16 *refNum{a1}, uint32 *fcb{a2})
	fsResolveFCB = 752,				// UTResolveFCB(int16 refNum{d0}, uint32 *fcb{a0})
	fsAdjustEOF = 766,				// UTAdjustEOF(int16 refNum{d0})
	fsAllocateWDCB = 778,			// UTAllocateWDCB(uint32 pb{a0})
	fsReleaseWDCB = 790,			// UTReleaseWDCB(int16 vRefNum{d0})
	SIZEOF_fsdat = 802
};

static uint32 fs_data = 0;		// Mac address of global data


// File system and volume name
static char FS_NAME[32], VOLUME_NAME[32];

// This directory is our root (read from prefs)
static const char *RootPath;
static bool ready = false;
//static struct stat root_stat;

// File system ID/media type
const int16 MY_FSID = 'ba';
const uint32 MY_MEDIA_TYPE = 'basi';

// CNID of root and root's parent
const uint32 ROOT_ID = 2;
const uint32 ROOT_PARENT_ID = 1;

// File system stack size
const int STACK_SIZE = 0x10000;

// Drive number of our pseudo-drive
static int drive_number;


// Disk/drive icon
const uint8 ExtFSIcon[256] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xff, 0xff, 0xfe,
	0x80, 0x00, 0x00, 0x91, 0x80, 0x00, 0x00, 0x91, 0x80, 0x00, 0x01, 0x21, 0x80, 0x00, 0x01, 0x21,
	0x80, 0x00, 0x02, 0x41, 0x8c, 0x00, 0x02, 0x41, 0x80, 0x00, 0x04, 0x81, 0x80, 0x00, 0x04, 0x81,
	0x7f, 0xff, 0xff, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xff, 0xff, 0xfe,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0x7f, 0xff, 0xff, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};


// These objects are used to map CNIDs to path names
struct FSItem {
	FSItem *next;			// Pointer to next FSItem in list
	uint32 id;				// CNID of this file/dir
	uint32 parent_id;		// CNID of parent file/dir
	FSItem *parent;			// Pointer to parent
	char name[32];			// Object name (C string)
	time_t mtime;			// Modification time for get_cat_info caching
	int cache_dircount;		// Cached number of files in directory
};

static FSItem *first_fs_item, *last_fs_item;

static uint32 next_cnid = fsUsrCNID;	// Next available CNID


/*
 *  Find FSItem for given CNID
 */

static FSItem *find_fsitem_by_id(uint32 cnid)
{
	FSItem *p = first_fs_item;
	while (p) {
		if (p->id == cnid)
			return p;
		p = p->next;
	}
	return NULL;
}


/*
 *  Find FSItem for given name and parent, construct new FSItem if not found
 */

static FSItem *find_fsitem(const char *name, FSItem *parent)
{
	FSItem *p = first_fs_item;
	while (p) {
		if (p->parent == parent && !strcmp(p->name, name))
			return p;
		p = p->next;
	}

	// Not found, construct new FSItem
	p = new FSItem;
	last_fs_item->next = p;
	p->next = NULL;
	last_fs_item = p;
	p->id = next_cnid++;
	p->parent_id = parent->id;
	p->parent = parent;
	strncpy(p->name, name, 31);
	p->name[31] = 0;
	p->mtime = 0;
	return p;
}


/*
 *  Get full path (->full_path) for given FSItem
 */

static char full_path[MAX_PATH_LENGTH];

static void add_path_comp(const char *s)
{
	add_path_component(full_path, s);
}

static void get_path_for_fsitem(FSItem *p)
{
	if (p->id == ROOT_PARENT_ID) {
		full_path[0] = 0;
	} else if (p->id == ROOT_ID) {
		strncpy(full_path, RootPath, MAX_PATH_LENGTH-1);
		full_path[MAX_PATH_LENGTH-1] = 0;
	} else {
		get_path_for_fsitem(p->parent);
		add_path_comp(p->name);
	}
}


/*
 *  Exchange parent CNIDs in all FSItems
 */

static void swap_parent_ids(uint32 parent1, uint32 parent2)
{
	FSItem *p = first_fs_item;
	while (p) {
		if (p->parent_id == parent1)
			p->parent_id = parent2;
		else if (p->parent_id == parent2)
			p->parent_id = parent1;
		p = p->next;
	}
}


/*
 *  String handling functions
 */

// Copy pascal string
static void pstrcpy(char *dst, const char *src)
{
	int size = *dst++ = *src++;
	while (size--)
		*dst++ = *src++;
}

// Convert C string to pascal string
static void cstr2pstr(char *dst, const char *src)
{
	*dst++ = strlen(src);
	char c;
	while ((c = *src++) != 0) {
		if (c == ':')
			c = '/';
		*dst++ = c;
	}
}

// Convert pascal string to C string
static void pstr2cstr(char *dst, const char *src)
{
	int size = *src++;
	while (size--) {
		char c = *src++;
		if (c == '/')
			c = ':';
		*dst++ = c;
	}
	*dst = 0;
}

// Convert string (no length byte) to C string, length given separately
static void strn2cstr(char *dst, const char *src, int size)
{
	while (size--) {
		char c = *src++;
		if (c == '/')
			c = ':';
		*dst++ = c;
	}
	*dst = 0;
}


/*
 *  Convert errno to MacOS error code
 */

static int16 errno2oserr(void)
{
	return 0;
}


/*
 *  Initialization
 */

void ExtFSInit(void)
{
}


/*
 *  Deinitialization
 */

void ExtFSExit(void)
{
}


/*
 *  Install file system
 */

void InstallExtFS(void)
{
}


/*
 *  FS communications function
 */

int16 ExtFSComm(uint16 message, uint32 paramBlock, uint32 globalsPtr)
{
	return 0;
}


/*
 *  Get current directory specified by given ParamBlock/dirID
 */

static int16 get_current_dir(uint32 pb, uint32 dirID, uint32 &current_dir, bool no_vol_name = false)
{
	return 0;
}


/*
 *  Get path component name
 */

static int16 get_path_component_name(uint32 rec)
{
	return 0;
}


/*
 *  Get FSItem and full path (->full_path) for file/dir specified in ParamBlock
 */

static int16 get_item_and_path(uint32 pb, uint32 dirID, FSItem *&item, bool no_vol_name = false)
{
	int16 result;
		result = bdNamErr;
	return result;
}


/*
 *  Find FCB for given file RefNum
 */

static uint32 find_fcb(int16 refNum)
{
	D(bug("  finding FCB\n"));
	M68kRegisters r;
	r.d[0] = refNum;
	r.a[0] = fs_data + fsReturn;
	Execute68k(fs_data + fsResolveFCB, &r);
	uint32 fcb = ReadMacInt32(fs_data + fsReturn);
	D(bug("  UTResolveFCB() returned %d, fcb %08lx\n", r.d[0], fcb));
	if (r.d[0] & 0xffff)
		return 0;
	else
		return fcb;
}


/*
 *  HFS interface functions
 */

// Check if volume belongs to our FS
static int16 fs_mount_vol(uint32 pb)
{
		return extFSErr;
}

// Mount volume
static int16 fs_volume_mount(uint32 pb)
{

	return extFSErr;
}

// Unmount volume
static int16 fs_unmount_vol(uint32 vcb)
{
	return extFSErr;
}

// Change volume information (HVolumeParam)
static int16 fs_set_vol_info(uint32 pb)
{
	return extFSErr;
}

// Get volume parameter block
static int16 fs_get_vol_parms(uint32 pb)
{
return extFSErr;
}

// Get default volume (WDParam)
static int16 fs_get_vol(uint32 pb)
{
	return extFSErr;
}

// Set default volume (WDParam)
static int16 fs_set_vol(uint32 pb, bool hfs, uint32 vcb)
{
	return extFSErr;
}

// Query file attributes (HFileParam)
static int16 fs_get_file_info(uint32 pb, bool hfs, uint32 dirID)
{
	return extFSErr;
}

// Set file attributes (HFileParam)
static int16 fs_set_file_info(uint32 pb, bool hfs, uint32 dirID)
{
	return extFSErr;
}

// Query file/directory attributes
static int16 fs_get_cat_info(uint32 pb)
{
	return extFSErr;
}

// Set file/directory attributes
static int16 fs_set_cat_info(uint32 pb)
{
return extFSErr;
}

// Open file
static int16 fs_open(uint32 pb, uint32 dirID, uint32 vcb, bool resource_fork)
{
return extFSErr;
}

// Close file
static int16 fs_close(uint32 pb)
{
return extFSErr;
}

// Query information about FCB (FCBPBRec)
static int16 fs_get_fcb_info(uint32 pb, uint32 vcb)
{
return extFSErr;
}

// Obtain logical size of an open file
static int16 fs_get_eof(uint32 pb)
{
return extFSErr;
}

// Truncate file
static int16 fs_set_eof(uint32 pb)
{
return extFSErr;
}

// Query current file position
static int16 fs_get_fpos(uint32 pb)
{
return extFSErr;
}

// Set current file position
static int16 fs_set_fpos(uint32 pb)
{
return extFSErr;
}

// Read from file
static int16 fs_read(uint32 pb)
{
return extFSErr;
}

// Write to file
static int16 fs_write(uint32 pb)
{
return extFSErr;
}

// Create file
static int16 fs_create(uint32 pb, uint32 dirID)
{
return extFSErr;
}

// Create directory
static int16 fs_dir_create(uint32 pb)
{
return extFSErr;
}

// Delete file/directory
static int16 fs_delete(uint32 pb, uint32 dirID)
{
return extFSErr;
}

// Rename file/directory
static int16 fs_rename(uint32 pb, uint32 dirID)
{
return extFSErr;
}

// Move file/directory (CMovePBRec)
static int16 fs_cat_move(uint32 pb)
{
return extFSErr;
}

// Open working directory (WDParam)
static int16 fs_open_wd(uint32 pb)
{
	return extFSErr;
}

// Close working directory (WDParam)
static int16 fs_close_wd(uint32 pb)
{
	return extFSErr;
}

// Query information about working directory (WDParam)
static int16 fs_get_wd_info(uint32 pb, uint32 vcb)
{
	return extFSErr;
}

// Main dispatch routine
int16 ExtFSHFS(uint32 vcb, uint16 selectCode, uint32 paramBlock, uint32 globalsPtr, int16 fsid)
{
	return paramErr;
}
