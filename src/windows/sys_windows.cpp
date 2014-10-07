#include <stdlib.h>
#include <ctype.h>
#include "sysdeps.h"
#include "main.h"
#include "prefs.h"
#include "user_strings.h"
#include "cpu_emulation.h"
#include "macos_util.h"
#include "sony.h"
#include "cdrom.h"
#include "disk.h"
#include "sys.h"
#include "errno.h"
#include "winioctl.h"
#include "sys_windows.h"
#include "emul_op.h"

// This must be always on.
#define DEBUG 0
#include "debug.h"

//#include "sysdeps.h"



//#include "sys_windows.h"

#include <winsock.h>
#include <io.h>
#include <stdio.h>
/*
 *  Initialization
 */

void SysInit(void)
{
int iResult;
WSADATA wsaData;


// Initialize Winsock
iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
if (iResult != 0) {
    printf("WSAStartup failed: %d\n", iResult);
    exit(0);
}
}


/*
 *  Deinitialization
 */

void SysExit(void)
{
}

void QuitEmulator()
{
	fflush(stdout);
	
	exit(0);
}


void *Sys_open(const char *name, bool read_only)	//this is.. way too complicated.
{
	bool is_file=true;
	D(bug("Sys_open(%s, %s)\n", name, read_only ? "read-only" : "read/write"));
	
	int fd = open(name, read_only ? _O_RDONLY|_O_BINARY : _O_RDWR|_O_BINARY);

	if (fd < 0 && !read_only) {
		// Read-write failed, try read-only
		read_only = true;
		D(bug("Setting file to read only!\n"));
		fd = open(name, O_RDONLY|_O_BINARY);
	}
	if (fd >= 0) {
		int rc;
		int size;
		D(bug("File Handle for %s is %d\n",name,fd));
		file_handle *fh = new file_handle;
		fh->name = strdup(name);
		fh->fd = fd;
		fh->is_file = true;//is_file;
		fh->read_only = read_only;
		fh->start_byte = 0;
		fh->is_floppy = false;
		fh->is_cdrom = false;
		
		size = lseek(fd, 0, SEEK_END);
		rc=_lseek(fd, 0L, SEEK_SET);
		//we cache the file size.
		fh->file_size=size;
		D(bug("File Size is %d\n",size));
        uint8 data[256];
        read(fd, data, 256);
		FileDiskLayout(size, data, fh->start_byte, fh->file_size);
		D(bug("fh->start_byte is %d\n",fh->start_byte));
        //if (fh->is_floppy && first_floppy == NULL)
        //   first_floppy = fh;

	return fh;
	}
return NULL;
}


/*
 *  Close file/device, delete file handle
 */

void Sys_close(void *arg)
{
	file_handle *fh = (file_handle *)arg;
	D(bug("Sys_close on %s\n",fh->name));
	if (!fh)
		return;

	close(fh->fd);
	if (fh->name)
		free(fh->name);
	delete fh;
}

/*
 *  Read "length" bytes from file/device, starting at "offset", to "buffer",
 *  returns number of bytes read (or 0)
 */

size_t Sys_read(void *arg, void *buffer, loff_t offset, size_t length)
{
	int rc;
        file_handle *fh=(file_handle*)arg;
        if(!fh)
                return 0;
        if(lseek(fh->fd,offset+fh->start_byte,SEEK_SET)<0)
                return 0;
        rc= read(fh->fd, buffer, length);
		//D(bug("read %d from %s, asked for %d with an offset of %d\n",rc,fh->name,length,offset));
		return rc;
}

size_t Sys_write(void *arg, void *buffer, loff_t offset, size_t length)
{
        file_handle *fh = (file_handle *)arg;
        if (!fh)
                return 0;

        if (lseek(fh->fd, offset + fh->start_byte, SEEK_SET) < 0)
                return 0;
        // Write data
        return write(fh->fd, buffer, length);

}


loff_t SysGetFileSize(void *arg)
{
	file_handle *fh = (file_handle *)arg;
	if (!fh)
		return true;


    return lseek(fh->fd, 0, SEEK_END) - fh->start_byte;

}


void SysAddDiskPrefs(void)
{	//no auto disks
	//D(bug("SysAddDisks?\n"));
}

void SysAddFloppyPrefs(void)
{	//no floppies
}

bool SysFormat(void *arg)
{	//no floppies
return FALSE;
}

void SysAddSerialPrefs(void)
{	//no serial ports
}

bool SysIsFixedDisk(void *arg)
{
	//we don't do floppies
		return true;
}

bool SysIsDiskInserted(void *arg)
{
   file_handle *fh = (file_handle *)arg;
    if (!fh)
       return false;

   if (fh->is_file) 
        return true;
   
	   return true;
}

void SysEject(void *arg)
{
	file_handle *fh = (file_handle *)arg;
    if (!fh)
		return;
	D(bug("ejecting %s\n",fh->name));

}

bool SysIsReadOnly(void *arg)
{
	file_handle *fh = (file_handle *)arg;
    if (!fh)
		return TRUE;
	else
		return fh->read_only;
}