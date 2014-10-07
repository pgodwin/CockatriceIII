#ifndef _SYS_WINDOWS_H_
#define _SYS_WINDOWS_H_

#define MAX_DEVICE_NAME _MAX_PATH

// File handles are pointers to these structures
struct file_handle {
	unsigned int fd;
	HANDLE h;
	bool is_file;			// Flag: plain file or /dev/something?
	bool is_floppy;		// Flag: floppy device
	bool is_real_floppy;
	bool is_cdrom;		// Flag: CD-ROM device
	bool is_hd;				// Flag: Hard disk.
	bool is_physical;	// Flag: drive instead of partition.
	bool read_only;		// Copy of Sys_open() flag
	off_t start_byte;	// Size of file header (if any)
//	cachetype cache;
	char *name;				//[MAX_DEVICE_NAME];
	bool is_media_present;
	int drive;
	long file_size;
	bool locked;
	int mount_mode;
//	undo_buffer ub;
};

#define DUMMY_HANDLE (HANDLE)-2

void media_removed(void);
void media_arrived(void);
//void mount_removable_media( media_t media );

#endif // _SYS_WINDOWS_H_