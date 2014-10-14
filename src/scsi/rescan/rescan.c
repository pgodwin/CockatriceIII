/*++

Copyright (c) 1990-2000 Microsoft Corporation, All Rights Reserved

Module Name:

    Rescan.c

Abstract:

Please note that the objective of this sample is to demonstrate the 
techniques used. This is not a complete program to be used in 
commercial product.
 
The purpose of the sample program is to enumerates all available SCSI
adapters and tell them to reenumerate the bus. This will find the new
device that are added to the bus after the previous bus scan. This will
also list all the child devices of the SCSI adapter.

See SDK & DDK Documentation for more information about the APIs, 
IOCTLs and data structures used.
    
Author:

    Raju Ramanathan     06/01/2001

Notes:


Revision History:

  Raju Ramanathan       06/01/2001    Completed the sample

--*/

#include <stdio.h> 
#include <conio.h> 
#include <stdlib.h> 
#include <stddef.h>
#include <windows.h>  
#include <initguid.h>   // Guid definition
#include <devguid.h>    // Device guids
#include <setupapi.h>   // for SetupDiXxx functions.
#include <cfgmgr32.h>   // for SetupDiXxx functions.
#include <devioctl.h>  
#include <ntdddisk.h>
#include <ntddscsi.h>
#include <rescan.h>


ULONG   DebugLevel = 1;
                            // 0 = Suppress All Messages
                            // 1 = Display & Fatal Error Message
                            // 2 = Warning & Debug Messages
                            // 3 = Informational Messages


VOID DebugPrint( USHORT DebugPrintLevel, PCHAR DebugMessage, ... )
/*++

Routine Description:

    This routine print the given string, if given debug level is <= to the
    current debug level.

Arguments:

    DebugPrintLevel - Debug level of the given message

    DebugMessage    - Message to be printed

Return Value:

  None

--*/
{

    va_list args;

    va_start(args, DebugMessage);

    if (DebugPrintLevel <= DebugLevel) {
        char buffer[128];
        (VOID) vsprintf(buffer, DebugMessage, args);
        printf( "%s", buffer );
    }

    va_end(args);
}

int __cdecl main()
/*++

Routine Description:

    This is the main function. It takes no arguments from the user.

Arguments:

    None

Return Value:

  Status

--*/
{
    HDEVINFO        hDevInfo;
    DWORD           index;
    BOOL            status;


    hDevInfo = SetupDiGetClassDevs(
                    (LPGUID) &GUID_DEVCLASS_SCSIADAPTER,
                    NULL,
                    NULL, 
                    DIGCF_PRESENT  ); // All devices present in the system
    if (hDevInfo == INVALID_HANDLE_VALUE)
    {
        DebugPrint( 1, "SetupDiGetClassDevs failed with error: %d\n", GetLastError()   );
        exit(1);
    }

    //
    //  Enumerate all the SCSI adapters
    //
    index = 0;

    for (index = 0; ; index++) 
    {
        DebugPrint( 1, "\nProperties for Device %d:", index+1);
        status = GetRegistryProperty( hDevInfo, index );
        if ( status == FALSE ) {
            break;
        }
    }
    DebugPrint( 1, "\r ***  End of Device List  *** \n");
    SetupDiDestroyDeviceInfoList(hDevInfo);
    return 0;
}


BOOL GetRegistryProperty( HDEVINFO DevInfo, DWORD Index )
/*++

Routine Description:

    This routine enumerates the SCSI adapters using the Setup class GUID
    GUID GUID_DEVCLASS_SCSIADAPTER. Gets the Device ID from the Registry 
    property.

Arguments:

    DevInfo - Handles to the device information list

    Index   - Device member 

Return Value:

  TRUE / FALSE. This decides whether to continue or not

--*/
{

    SP_DEVINFO_DATA deviceInfoData;
    DWORD           errorCode;
    DWORD           bufferSize = 0;
    DWORD           dataType;
    LPTSTR          buffer = NULL;
    BOOL            status;
    CONFIGRET       configRetVal;
    TCHAR           deviceInstanceId[MAX_DEVICE_ID_LEN];
    

    // Get the DEVINFO data. This has the handle to device instance 

    deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    status = SetupDiEnumDeviceInfo(
                DevInfo,                // Device info set
                Index,                  // Index in the info set
                &deviceInfoData);       // Info data. Contains handle to dev inst

    if ( status == FALSE ) {
        errorCode = GetLastError();
        if ( errorCode == ERROR_NO_MORE_ITEMS ) {
            DebugPrint( 2, "No more devices.\n");
        }
        else {
            DebugPrint( 1, "SetupDiEnumDeviceInfo failed with error: %d\n", errorCode );
        }
        return FALSE;
    }


    // Get the Device Instance ID using the handle

    configRetVal = CM_Get_Device_ID(
                        deviceInfoData.DevInst,                 // Handle to dev inst
                        deviceInstanceId,                       // Buffer to receive dev inst
                        sizeof(deviceInstanceId)/sizeof(TCHAR), // Buffer size
                        0);                                     // Must be zero
    if (configRetVal != CR_SUCCESS) {
        DebugPrint( 1, "CM_Get_Device_ID failed with error: %d\n", configRetVal );
        return FALSE;
    }

    //
    // We won't know the size of the HardwareID buffer until we call
    // this function. So call it with a null to begin with, and then 
    // use the required buffer size to Alloc the necessary space.
    // Call it again with the obtained buffer size
    //

    status = SetupDiGetDeviceRegistryProperty(
                DevInfo,
                &deviceInfoData,
                SPDRP_HARDWAREID,
                &dataType,
                (PBYTE)buffer,
                bufferSize,
                &bufferSize);

    if ( status == FALSE ) {
        errorCode = GetLastError();
        if ( errorCode != ERROR_INSUFFICIENT_BUFFER ) {
            if ( errorCode == ERROR_INVALID_DATA ) {
                //
                // May be a Legacy Device with no HardwareID. Continue.
                //
                DebugPrint( 2, "No Hardware ID, may be a legacy device!\n" );
            }
            else {
                DebugPrint( 1, "SetupDiGetDeviceRegistryProperty failed with error: %d\n", errorCode );
                return FALSE;
            }
        }
    }

    //
    // We need to change the buffer size.
    //

    buffer = LocalAlloc(LPTR, bufferSize);
    
    status = SetupDiGetDeviceRegistryProperty(
                DevInfo,
                &deviceInfoData,
                SPDRP_HARDWAREID,
                &dataType,
                (PBYTE)buffer,
                bufferSize,
                &bufferSize);

    if ( status == FALSE ) {
        errorCode = GetLastError();
        if ( errorCode == ERROR_INVALID_DATA ) {
            //
            // May be a Legacy Device with no HardwareID. Continue.
            //
            DebugPrint( 2, "No Hardware ID, may be a legacy device!\n" );
        }
        else {
            DebugPrint( 1, "SetupDiGetDeviceRegistryProperty failed with error: %d\n", errorCode );
            return FALSE;
        }
    }

    DebugPrint( 1, "\n\nDevice ID           : %s\n",buffer );

    if (buffer) {
        LocalFree(buffer);
    }

    // **** Now get the Device Description

    //
    // We won't know the size of the Location Information buffer until
    // we call this function. So call it with a null to begin with,
    // and then use the required buffer size to Alloc the necessary space.
    // Call it again with the obtained buffer size
    //

    status = SetupDiGetDeviceRegistryProperty(
                DevInfo,
                &deviceInfoData,
                SPDRP_DEVICEDESC,
                &dataType,
                (PBYTE)buffer,
                bufferSize,
                &bufferSize);

    if ( status == FALSE ) {
        errorCode = GetLastError();
        if ( errorCode != ERROR_INSUFFICIENT_BUFFER ) {
            if ( errorCode == ERROR_INVALID_DATA ) {
                DebugPrint( 2, "No Device Description!\n" );
            }
            else {
                DebugPrint( 1, "SetupDiGetDeviceRegistryProperty failed with error: %d\n", errorCode );
                return FALSE;
            }
        }
    }

    //
    // We need to change the buffer size.
    //

    buffer = LocalAlloc(LPTR, bufferSize);
    
    status = SetupDiGetDeviceRegistryProperty(
                DevInfo,
                &deviceInfoData,
                SPDRP_DEVICEDESC,
                &dataType,
                (PBYTE)buffer,
                bufferSize,
                &bufferSize);

    if ( status == FALSE ) {
        errorCode = GetLastError();
        if ( errorCode == ERROR_INVALID_DATA ) {
            DebugPrint( 2, "No Device Description!\n" );
        }
        else {
            DebugPrint( 1, "SetupDiGetDeviceRegistryProperty failed with error: %d\n", errorCode );
            return FALSE;
        }
    }

    DebugPrint( 1, "Device Description  : %s\n",buffer );


    // Now get the UI Number, which is also the PCI slot number for add-on cards
    // Note: This value is not valid for non-PCI devices and on-board PCI
    // devices.

    //
    // Use the same buffer as we are expecting only one character
    //

    status = SetupDiGetDeviceRegistryProperty(
                DevInfo,
                &deviceInfoData,
                SPDRP_UI_NUMBER,
                &dataType,
                (PBYTE)buffer,
                bufferSize,
                &bufferSize);

    if ( status == FALSE ) {
        errorCode = GetLastError();
        if ( errorCode != ERROR_INSUFFICIENT_BUFFER ) {
            if ( errorCode == ERROR_INVALID_DATA ) {
                DebugPrint( 2, "UI number not available, may be an on-board device!\n" );
            }
            else {
                DebugPrint( 1, "SetupDiGetDeviceRegistryProperty failed with error: %d\n", errorCode );
                return FALSE;
            }
        }
    }
    DebugPrint( 1, "UI (PCI Slot) Number: %d\n\n", buffer[0] );


    if (buffer) {
        LocalFree(buffer);
    }

    // Reenumerate the SCSI adapter device node

    configRetVal = CM_Reenumerate_DevNode(
                        deviceInfoData.DevInst, 
                        0);
    
    if (configRetVal != CR_SUCCESS) {
        DebugPrint( 1, "CM_Reenumerate_DevNode failed with error: %d\n", configRetVal );
       return FALSE;
    }

    GetInquiryData( (PCTSTR) &deviceInstanceId );

    // Find the children devices

    GetChildDevices( deviceInfoData.DevInst );

    return TRUE;
}


VOID
PrintError(ULONG ErrorCode)
/*++

Routine Description:

    Prints formated error message

Arguments:

    ErrorCode   - Error code to print


Return Value:
    
      None
--*/
{
    UCHAR errorBuffer[80];
    ULONG count;

    count = FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM,
                    NULL,
                    ErrorCode,
                    0,
                    errorBuffer,
                    sizeof(errorBuffer),
                    NULL
                    );

    if (count != 0) {
        DebugPrint( 1, "%s\n", errorBuffer);
    } else {
        DebugPrint( 1, "Format message failed.  Error: %d\n", GetLastError());
    }
}

VOID
GetInquiryData( PCTSTR pDevId )
{


    SP_DEVICE_INTERFACE_DATA            interfaceData;
    PSP_DEVICE_INTERFACE_DETAIL_DATA    interfaceDetailData = NULL;
    STORAGE_PROPERTY_QUERY              query;
    PSTORAGE_ADAPTER_DESCRIPTOR         adpDesc;
	PSCSI_BUS_DATA			            BusData;
	PSCSI_INQUIRY_DATA		            InquiryData;
	PSCSI_ADAPTER_BUS_INFO          	AdapterInfo;
    HANDLE                              hDevice;
    BOOL                                status;
    DWORD                               index = 0;
    HDEVINFO                            hIntDevInfo;
    UCHAR                               outBuf[512];
    BOOL	                            Claimed;
    ULONG                               returnedLength;
    SHORT	                            Bus,
                                        Luns;
    DWORD                               interfaceDetailDataSize,
                                        reqSize,
                                        errorCode;


    hIntDevInfo = SetupDiGetClassDevs (
                 (LPGUID)&StoragePortClassGuid,
                 pDevId,                                 // Enumerator
                 NULL,                                   // Parent Window
                 (DIGCF_PRESENT | DIGCF_INTERFACEDEVICE  // Only Devices present & Interface class
                 ));

    if( hIntDevInfo == INVALID_HANDLE_VALUE ) {
        DebugPrint( 1, "SetupDiGetClassDevs failed with error: %d\n", GetLastError() );
        return;
    }

    interfaceData.cbSize = sizeof (SP_INTERFACE_DEVICE_DATA);

    status = SetupDiEnumDeviceInterfaces ( 
                hIntDevInfo,            // Interface Device Info handle
                0,
                (LPGUID) &StoragePortClassGuid,
                index,                  // Member
                &interfaceData          // Device Interface Data
                );

    if ( status == FALSE ) {
        errorCode = GetLastError();
        if ( errorCode == ERROR_NO_MORE_ITEMS ) {
            DebugPrint( 2, "No more interfaces\n" );
        }
        else {
            DebugPrint( 1, "SetupDiEnumDeviceInterfaces failed with error: %d\n", errorCode  );
        }
        return;
    }
        
    //
    // Find out required buffer size, so pass NULL 
    //

    status = SetupDiGetDeviceInterfaceDetail (
                hIntDevInfo,        // Interface Device info handle
                &interfaceData,     // Interface data for the event class
                NULL,               // Checking for buffer size
                0,                  // Checking for buffer size
                &reqSize,           // Buffer size required to get the detail data
                NULL                // Checking for buffer size
                );

    //
    // This call returns ERROR_INSUFFICIENT_BUFFER with reqSize 
    // set to the required buffer size. Ignore the above error and
    // pass a bigger buffer to get the detail data
    //

    if ( status == FALSE ) {
        errorCode = GetLastError();
        if ( errorCode != ERROR_INSUFFICIENT_BUFFER ) {
            DebugPrint( 1, "SetupDiGetDeviceInterfaceDetail failed with error: %d\n", errorCode   );
            return;
        }
    }

    //
    // Allocate memory to get the interface detail data
    // This contains the devicepath we need to open the device
    //

    interfaceDetailDataSize = reqSize;
    interfaceDetailData = malloc (interfaceDetailDataSize);
    if ( interfaceDetailData == NULL ) {
        DebugPrint( 1, "Unable to allocate memory to get the interface detail data.\n" );
        return;
    }
    interfaceDetailData->cbSize = sizeof (SP_INTERFACE_DEVICE_DETAIL_DATA);

    status = SetupDiGetDeviceInterfaceDetail (
                  hIntDevInfo,              // Interface Device info handle
                  &interfaceData,           // Interface data for the event class
                  interfaceDetailData,      // Interface detail data
                  interfaceDetailDataSize,  // Interface detail data size
                  &reqSize,                 // Buffer size required to get the detail data
                  NULL);                    // Interface device info

    if ( status == FALSE ) {
        DebugPrint( 1, "Error in SetupDiGetDeviceInterfaceDetail failed with error: %d\n", GetLastError() );
        return;
    }

    //
    // Now we have the device path. Open the device interface
    // to get adapter property and inquiry data


    hDevice = CreateFile(
                interfaceDetailData->DevicePath,    // device interface name
                GENERIC_READ | GENERIC_WRITE,       // dwDesiredAccess
                FILE_SHARE_READ | FILE_SHARE_WRITE, // dwShareMode
                NULL,                               // lpSecurityAttributes
                OPEN_EXISTING,                      // dwCreationDistribution
                0,                                  // dwFlagsAndAttributes
                NULL                                // hTemplateFile
                );
                
    //
    // We have the handle to talk to the device. 
    // So we can release the interfaceDetailData buffer
    //

    free (interfaceDetailData);

    if (hDevice == INVALID_HANDLE_VALUE) {
        DebugPrint( 1, "CreateFile failed with error: %d\n", GetLastError() );
        CloseHandle ( hDevice );	
        return;
    }

    query.PropertyId = StorageAdapterProperty;
    query.QueryType = PropertyStandardQuery;

    status = DeviceIoControl(
                        hDevice,                
                        IOCTL_STORAGE_QUERY_PROPERTY,
                        &query,
                        sizeof( STORAGE_PROPERTY_QUERY ),
                        &outBuf,                   
                        512,                      
                        &returnedLength,      
                        NULL                    
                        );
    if ( !status ) {
        DebugPrint( 1, "IOCTL failed with error code%d.\n\n", GetLastError() );
    }
    else {
        adpDesc = (PSTORAGE_ADAPTER_DESCRIPTOR) outBuf;
        DebugPrint( 1, "\nAdapter Properties\n");
        DebugPrint( 1, "------------------\n");
        DebugPrint( 1, "Bus Type       : %s\n",   BusType[adpDesc->BusType]);
        DebugPrint( 1, "Max. Tr. Length: 0x%x\n", adpDesc->MaximumTransferLength );
        DebugPrint( 1, "Max. Phy. Pages: 0x%x\n", adpDesc->MaximumPhysicalPages );
        DebugPrint( 1, "Alignment Mask : 0x%x\n", adpDesc->AlignmentMask );

    }
    
	AdapterInfo = (PSCSI_ADAPTER_BUS_INFO) malloc(SCSI_INFO_BUFFER_SIZE) ;
	if ( AdapterInfo == NULL) {
		DebugPrint(1,"Error in allocating memory\n");
        CloseHandle ( hDevice );	
		return;
	}

    // Get the SCSI inquiry data for all devices for the given SCSI bus
    status = DeviceIoControl(	
                            hDevice,
                            IOCTL_SCSI_GET_INQUIRY_DATA,
                            NULL,
                            0,
                            AdapterInfo,
                            SCSI_INFO_BUFFER_SIZE,
                            &returnedLength,
                            NULL );

    if ( !status ) {
        DebugPrint(1,"Error in IOCTL_SCSI_GET_INQUIRY_DATA\n" );
        free( AdapterInfo );
        CloseHandle ( hDevice );	
        return;
    }

    DebugPrint(1,"\nChild Device Properties:\n\n" );
	DebugPrint(1,"Initiator   Path ID  Target ID    LUN  Claimed  Device   \n");
	DebugPrint(1,"---------------------------------------------------------\n");

    for ( Bus = 0; Bus < AdapterInfo->NumberOfBuses; Bus++ ) {
        BusData = &AdapterInfo->BusData[Bus];
        InquiryData = (PSCSI_INQUIRY_DATA) ( (PUCHAR) AdapterInfo + BusData->InquiryDataOffset );
        for ( Luns = 0; Luns < BusData->NumberOfLogicalUnits; Luns++ ) {
            Claimed = InquiryData->DeviceClaimed;
            DebugPrint(1,"   %3d        %d         %d          %d     %s   ",
                BusData->InitiatorBusId, InquiryData->PathId, InquiryData->TargetId, 
                InquiryData->Lun, Claimed? "Yes" : "No " );
            switch ( InquiryData->InquiryData[0] & 0x1f ) {

            case DISK_DRIVE:
                    DebugPrint(1,"  Disk\n");
                    break;

                case TAPE_DRIVE:
                    DebugPrint(1,"  Tape\n");	
                    break;

                case PRINTER:
                    DebugPrint(1,"  Printer\n");
                    break;

                case PROCESSOR:
                    DebugPrint(1,"  Processor device\n");
                    break;

                case WORM_DEVICE:
                    DebugPrint(1,"  WORM Device\n");
                    break;

                case CDROM_DRIVE:
                    DebugPrint(1,"  CDROM Drive\n");
                    break;

                case SCANNER:
                    DebugPrint(1,"  Scanner\n");
                    break;

                case OPTICAL_DISK:
                    DebugPrint(1,"  Optical Disk\n");
                    break;

                case MEDIA_CHANGER:
                    DebugPrint(1,"  Media changer\n");
                    break;

                case COMM_DEVICE:
                    DebugPrint(1,"  Comm. Device\n");
                    break;

                case ASCIT8_DEVICE1:
                    DebugPrint(1,"  ASCIT8 Device\n");
                    break;
    
                case ASCIT8_DEVICE2:
                    DebugPrint(1,"  ASCIT8 Device\n");
                    break;
    
                case ARRAY_DEVICE:
                    DebugPrint(1,"  Array Device\n");
                    break;

                case ENCLOSURE_DEVICE:
                    DebugPrint(1,"  Enclosure Device\n");
                    break;
    
                case RBC_DEVICE:
                    DebugPrint(1,"  RBC Device\n");
                    break;

                default:
                    DebugPrint(1,"  Unknown device\n");
                    break;
            }

            InquiryData = (PSCSI_INQUIRY_DATA) ( (PUCHAR) AdapterInfo + InquiryData->NextInquiryDataOffset );
        }	// for Luns
    }	// for Bus

    DebugPrint( 1, "\n" );
	free( AdapterInfo );
    CloseHandle ( hDevice );	

    return;
}

VOID
GetChildDevices( DEVINST DevInst )
{
    DEVINST         childDevInst;
    DEVINST         siblingDevInst;
    CONFIGRET       configRetVal;
    TCHAR           deviceInstanceId[MAX_DEVICE_ID_LEN];


    configRetVal = CM_Get_Child (
                        &childDevInst,
                        DevInst,
                        0 );


    if (configRetVal == CR_NO_SUCH_DEVNODE ) {
        DebugPrint( 2, "No child devices\n" );
        return;
    }

    if (configRetVal != CR_SUCCESS ) {
        DebugPrint( 1, "CM_Get_Child failed with error: %x\n", configRetVal );
        return;
    }

    do {

        // Get the Device Instance ID using the handle
    
        configRetVal = CM_Get_Device_ID(
                            childDevInst,
                            deviceInstanceId,
                            sizeof(deviceInstanceId)/sizeof(TCHAR),
                            0
                            );
        if (configRetVal != CR_SUCCESS) {
            DebugPrint( 1, "CM_Get_Device_ID: Get Device Instance ID failed with error: %x\n", configRetVal );
            return;
        }
        DebugPrint( 1, "%s\n", deviceInstanceId );

        // Get sibling

        configRetVal = CM_Get_Sibling (
                            &siblingDevInst,
                            childDevInst,
                            0 );

        if (configRetVal == CR_NO_SUCH_DEVNODE ) {
            DebugPrint( 2, "No more siblings!\n" );
            return;
        }

        if (configRetVal != CR_SUCCESS ) {
            DebugPrint( 1, "CM_Get_Sibling failed with error: 0x%X\n", configRetVal );
            return;
        }
        childDevInst = siblingDevInst;

    } while ( TRUE );

    return;
}

