/*++

Copyright (c) 1990-2000 Microsoft Corporation, All Rights Reserved

Module Name:

    Rescan.h

Abstract:

    This file includes data declarations for the Rescan sample

Author:

    Raju Ramanathan     06/01/2001

Notes:

Revision History:


--*/

#ifndef _RESCAN_H_
#define _RESCAN_H_


/*** 
NOTE: 

This buffer size must be >= 
(sizeof(SCSI_ADAPTER_BUS_INFO) 
    + (NumberOfBuses - 1) * sizeof(SCSI_BUS_DATA)) 
    + ((sizeof(SCSI_INQUIRY_DATA) - 1 + INQUIRYDATABUFFERSIZE) * NumberOfLUs)
    rounded up to an alignment boundary.

***/

#define	SCSI_INFO_BUFFER_SIZE	0x5000	// Big enough to hold all Bus/Device info.

#define	DISK_DRIVE		        0x00
#define	TAPE_DRIVE		        0x01
#define	PRINTER   		        0x02
#define	PROCESSOR 		        0x03
#define	WORM_DEVICE		        0x04
#define	CDROM_DRIVE		        0x05
#define	SCANNER			        0x06
#define	OPTICAL_DISK	        0x07
#define	MEDIA_CHANGER	        0x08
#define	COMM_DEVICE		        0x09
#define ASCIT8_DEVICE1          0x0A
#define ASCIT8_DEVICE2          0x0B
#define ARRAY_DEVICE            0x0C
#define ENCLOSURE_DEVICE        0x0D
#define RBC_DEVICE              0x0E
#define	UNKNOWN_DEVICE	        0x0F
                                                        
                                                        
//
// Bus Type
//

static char* BusType[] = {
    "UNKNOWN",  // 0x00
    "SCSI",
    "ATAPI",
    "ATA",
    "IEEE 1394",
    "SSA",
    "FIBRE",
    "USB",
    "RAID"
};

VOID DebugPrint( USHORT, PCHAR, ... );
VOID PrintError( ULONG );
BOOL GetRegistryProperty( HDEVINFO, DWORD );
VOID GetChildDevices( DEVINST );
VOID GetInquiryData( PCTSTR );

#endif    // _RESCAN_H_



