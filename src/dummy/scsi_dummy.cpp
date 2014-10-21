/*
 *  scsi_dummy.cpp - SCSI Manager, dummy implementation
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


/**** LOTS of this comes from previous.
https://sourceforge.net/projects/previous/
http://previous.alternative-system.com/

The excellent NeXT machine emulator!
*****/


#include <stdio.h>
#include "sysdeps.h"
#include "prefs.h"
#include "scsi.h"

#define DEBUG 0
#include "debug.h"

/******** marcros	*************/
#define COMMAND_ReadInt16(a, i) (((unsigned) a[i] << 8) | a[i + 1])
#define COMMAND_ReadInt24(a, i) (((unsigned) a[i] << 16) | ((unsigned) a[i + 1] << 8) | a[i + 2])
#define COMMAND_ReadInt32(a, i) (((unsigned) a[i] << 24) | ((unsigned) a[i + 1] << 16) | ((unsigned) a[i + 2] << 8) | a[i + 3])


#define BLOCKSIZE 512

#define LUN_DISK 0 // for now only LUN 0 is valid for our phys drives

/* Status Codes */
#define STAT_GOOD           0x00
#define STAT_CHECK_COND     0x02
#define STAT_COND_MET       0x04
#define STAT_BUSY           0x08
#define STAT_INTERMEDIATE   0x10
#define STAT_INTER_COND_MET 0x14
#define STAT_RESERV_CONFL   0x18

/* Messages */
#define MSG_COMPLETE        0x00
#define MSG_SAVE_PTRS       0x02
#define MSG_RESTORE_PTRS    0x03
#define MSG_DISCONNECT      0x04
#define MSG_INITIATOR_ERR   0x05
#define MSG_ABORT           0x06
#define MSG_MSG_REJECT      0x07
#define MSG_NOP             0x08
#define MSG_PARITY_ERR      0x09
#define MSG_LINK_CMD_CMPLT  0x0A
#define MSG_LNKCMDCMPLTFLAG 0x0B
#define MSG_DEVICE_RESET    0x0C

#define MSG_IDENTIFY_MASK   0x80
#define MSG_ID_DISCONN      0x40
#define MSG_LUNMASK         0x07

/* Sense Keys */
#define SK_NOSENSE          0x00
#define SK_RECOVERED        0x01
#define SK_NOTREADY         0x02
#define SK_MEDIA            0x03
#define SK_HARDWARE         0x04
#define SK_ILLEGAL_REQ      0x05
#define SK_UNIT_ATN         0x06
#define SK_DATAPROTECT      0x07
#define SK_ABORTED_CMD      0x0B
#define SK_VOL_OVERFLOW     0x0D
#define SK_MISCOMPARE       0x0E

/* Additional Sense Codes */
#define SC_NO_ERROR         0x00    // 0
#define SC_NO_SECTOR        0x01    // 4
#define SC_WRITE_FAULT      0x03    // 5
#define SC_INVALID_CMD      0x20    // 5
#define SC_INVALID_LBA      0x21    // 5
#define SC_INVALID_CDB      0x24    // 5
#define SC_INVALID_LUN      0x25    // 5
#define SC_WRITE_PROTECT    0x27    // 7


/* SCSI Commands */

/* The following are multi-sector transfers with seek implied */
#define CMD_VERIFY_TRACK    0x05    /* Verify track */
#define CMD_FORMAT_TRACK    0x06    /* Format track */
#define CMD_READ_SECTOR     0x08    /* Read sector */
#define CMD_READ_SECTOR1    0x28    /* Read sector (class 1) */
#define CMD_WRITE_SECTOR    0x0A    /* Write sector */
#define CMD_WRITE_SECTOR1   0x2A    /* Write sector (class 1) */

/* Other codes */
#define CMD_TEST_UNIT_RDY   0x00    /* Test unit ready */
#define CMD_FORMAT_DRIVE    0x04    /* Format the whole drive */
#define CMD_SEEK            0x0B    /* Seek */
#define CMD_CORRECTION      0x0D    /* Correction */
#define CMD_INQUIRY         0x12    /* Inquiry */
#define CMD_MODESELECT      0x15    /* Mode select */
#define CMD_MODESENSE       0x1A    /* Mode sense */
#define CMD_REQ_SENSE       0x03    /* Request sense */
#define CMD_SHIP            0x1B    /* Ship drive */
#define CMD_READ_CAPACITY1  0x25    /* Read capacity (class 1) */


/* INQUIRY response data */
#define DEVTYPE_DISK        0x00    /* read/write disks */
#define DEVTYPE_TAPE        0x01    /* tapes and other sequential devices */
#define DEVTYPE_PRINTER     0x02    /* printers */
#define DEVTYPE_PROCESSOR   0x03    /* cpus */
#define DEVTYPE_WORM        0x04    /* write-once optical disks */
#define DEVTYPE_READONLY    0x05    /* cd-roms */
#define DEVTYPE_NOTPRESENT  0x7f    /* logical unit not present */

/* SCSI phase */
#define PHASE_DO      0x00 /* data out */
#define PHASE_DI      0x01 /* data in */
#define PHASE_CD      0x02 /* command */
#define PHASE_ST      0x03 /* status */
#define PHASE_MO      0x06 /* message out */
#define PHASE_MI      0x07 /* message in */


#define ESP_MAX_DEVS 7


static unsigned char inquiry_bytes[] =
{
	0x00,             /* 0: device type: see above */
	0x00,             /* 1: &0x7F - device type qualifier 0x00 unsupported, &0x80 - rmb: 0x00 = nonremovable, 0x80 = removable */
	0x01,             /* 2: ANSI SCSI standard (first release) compliant */
    0x02,             /* 3: Response format (format of following data): 0x01 SCSI-1 compliant */
	0x31,             /* 4: additional length of the following data */
    0x00, 0x00,       /* 5,6: reserved */
    0x1C,             /* 7: RelAdr=0, Wbus32=0, Wbus16=0, Sync=1, Linked=1, RSVD=1, CmdQue=0, SftRe=0 */
	'P','r','e','v','i','o','u','s',        /*  8-15: Vendor ASCII */
	'H','D','D',' ',' ',' ',' ',' ',        /* 16-23: Model ASCII */
    ' ',' ',' ',' ',' ',' ',' ',' ',        /* 24-31: Blank space ASCII */
    '0','0','0','0','0','0','0','1',        /* 32-39: Revision ASCII */
	'0','0','0','0','0','0','0','0',        /* 40-47: Serial Number ASCII */
    ' ',' ',' ',' ',' ',' '                 /* 48-53: Blank space ASCII */
};



/******************************/
/* SCSI disk */
struct SCSIdiskst {
    FILE* dsk;

    uint32 size;
    bool cdrom;
    uint8 lun;
    uint8 status;
    uint8 message;
    
    struct senset{
        uint8 key;
        uint8 code;
        bool valid;
        uint32 info;
    } sense;
    
    uint32 lba;
    uint32 blockcounter;
} SCSIdisk[ESP_MAX_DEVS];


/* This buffer temporarily stores data to be written to memory or disk */

struct scsi_bufferst{
    uint8 data[128*1024];	//[512]; /* FIXME: BLOCKSIZE */
    uint32 limit;
    uint32 size;
    bool disk;		//disk or cdrom
} scsi_buffer;


/* Mode Pages */
#define MODEPAGE_MAX_SIZE 24

struct MODEPAGE {
    uint8 current[MODEPAGE_MAX_SIZE];
    uint8 changeable[MODEPAGE_MAX_SIZE];
    uint8 modepage[MODEPAGE_MAX_SIZE]; // default values
    uint8 saved[MODEPAGE_MAX_SIZE];
    uint8 pagesize;
};

typedef struct MODEPAGE MODEPAGE;


/*******prototypes****************/
void SCSI_Emulate_Command(unsigned char *cdb);
void SCSIabort(void);
void SCSI_Inquiry (unsigned char *cdb);
int SCSI_GetTransferLength(uint8 opcode, uint8 *cdb);
void SCSI_TestUnitReady(uint8 *cdb);
void SCSI_StartStop(uint8 *cdb);
void SCSI_ReadCapacity(uint8 *cdb);
void scsi_read_sector(void);
void SCSI_ReadSector(uint8 *cdb);
unsigned long SCSI_GetOffset(uint8 opcode, uint8 *cdb);
int SCSI_GetCount(uint8 opcode, uint8 *cdb);
void SCSI_ModeSense(uint8 *cdb);
MODEPAGE SCSI_GetModePage(uint8 pagecode);
void SCSI_GuessGeometry(uint32 size, uint32 *cylinders, uint32 *heads, uint32 *sectors);
void SCSI_WriteSector(uint8 *cdb);
void scsi_write_sector(void);



/////AMIGA

unsigned long buffer_size;				// Size of data buffer
uint8 *buffer = NULL;			// Pointer to data buffer

uint8 cmd_buffer[12];			// Buffer for SCSI command
int scsi_CmdLength;			// On the Amiga it's scsi.scsi_CmdLength

int SENSE_LENGTH = 256;
uint8 *sense_buffer = NULL;		// Buffer for autosense data
////////
//me.

int target=0;	//the current target.





/**********************************************************************************/
/*
 *  Initialization
 */

void SCSIInit(void)
{
unsigned int cylinders,heads,sectors,count=0;
char prefs_name[16];

	// Allocate buffer
	buffer = (uint8 *)malloc(buffer_size = 0x10000);
	sense_buffer = (uint8*)malloc(SENSE_LENGTH+1);

	// Zap previous's scsi disk info block
	memset(SCSIdisk,0x0,sizeof(SCSIdisk));
	memset(scsi_buffer.data,0x0,sizeof(scsi_buffer.data));

	//force our first SCSI target.
	//SCSIdisk[1].size=488*1024*1024;
	//SCSIdisk[1].cdrom=false;
	//SCSIdisk[1].lun=0;
	//SCSIdisk[1].dsk=fopen("scsi.test","rb+");
	//SCSI_GuessGeometry(SCSIdisk[1].size, &cylinders, &heads, &sectors);
        //printf("[SCSI] Disk geometry: %i sectors, %i cylinders, %i heads\n", sectors, cylinders, heads);

	for(count=0;count<7;count++)
	{
	sprintf(prefs_name, "scsi%d", count);
	const char *str = PrefsFindString(prefs_name);
	if(str!=NULL) {
		SCSIdisk[count].dsk=fopen(str,"rb+");
		if(SCSIdisk[count].dsk!=NULL){
			fseek(SCSIdisk[count].dsk, 0, SEEK_END);
			SCSIdisk[count].size=ftell(SCSIdisk[count].dsk);
			fseek(SCSIdisk[count].dsk, 0, SEEK_SET);
			SCSIdisk[count].lun=0;
			SCSIdisk[count].cdrom=false;
			SCSI_GuessGeometry(SCSIdisk[count].size, &cylinders, &heads, &sectors);
		        printf("SCSI Unit %d:%s geometry: %i sectors, %i cylinders, %i heads\n",count,str, sectors, cylinders, heads);
			}	//end populate scsi
		}
	}  //end of for7 loop
            
	// Reset SCSI bus
	SCSIReset();
}




/*
 *  Deinitialization
 */

void SCSIExit(void)
{
int count;
printf("SCSIExit\n");

for(count=0;count<7;count++)
	if (SCSIdisk[count].dsk!=NULL)
		fclose(SCSIdisk[count].dsk);
}


/*
 *  Set SCSI command to be sent by scsi_send_cmd()
 */

void scsi_set_cmd(int cmd_length, uint8 *cmd)
{
#if 1
int j=0;
unsigned char lun = 0;
/*
//printf("dummy:scsi_set_cmd command ");
while (j<cmd_length)
	{
	printf("%x ",*cmd++);
	j++;
	}
*/
if (cmd_length&MSG_IDENTIFY_MASK)  /* if identify message is valid */
	 lun = cmd_length&MSG_LUNMASK;
else
	lun = (cmd[1]&0xE0)>>5;
//printf("LUN is %d\n",lun);
//printf("\n");
//SCSI_Emulate_Command(cmd);
#endif

//Amiga	
memcpy(cmd_buffer, cmd, cmd_length);
scsi_CmdLength = cmd_length;
}


/*
 *  Check for presence of SCSI target
 */

bool scsi_is_target_present(int id)
{
/*if(id==1)
	return true;
else
	return false;*/
if(SCSIdisk[id].dsk!=NULL)
	return true;
else
	return false;
}


/*
 *  Set SCSI target (returns false on error)
 */

bool scsi_set_target(int id, int lun)
{
/**printf("dummy: scsi_set_target %d %d\n",id,lun);
if(id==1 && lun==0)
	return true;
else
	return false;****/
if( (SCSIdisk[id].dsk!=NULL)&&(lun==0))	{	//for now we only do LUN 0.
	target=id;	//globals. oh my.
	return true;
	}
else
	return false;
}



/*
 *  Check if requested data size fits into buffer, allocate new buffer if needed
 */

static bool try_buffer(int size)
{
	if (size <= buffer_size)
		return true;
//printf("growing new buffer\n");
	uint8 *new_buffer = (uint8 *)malloc(size);
	if (new_buffer == NULL)
		return false;
	//free(buffer, buffer_size);
	buffer = new_buffer;
	buffer_size = size;
	return true;
}


/*
 *  Send SCSI command to active target (scsi_set_command() must have been called),
 *  read/write data according to S/G table (returns false on error)
 */

bool scsi_send_cmd(size_t data_length, bool reading, int sg_size, uint8 **sg_ptr, uint32 *sg_len, uint16 *stat, uint32 timeout)
{
//xx
//int target=1;
//printf("\n");

/////////// From the Amiga

	// Check if buffer is large enough, allocate new buffer if needed
	if (!try_buffer(data_length)) {
		//char str[256];
		//sprintf(str, GetString(STR_SCSI_BUFFER_ERR), data_length);
		//ErrorAlert(str);
		printf("SCSI BUFFR ERROR!\n");
		return false;
	}

	if(data_length>256*1024)
		printf("Mac is asking for %d byte buffer with %d byte elements\n",data_length,sg_size);

	// Process S/G table when writing
	if (!reading) {
		D(bug(" writing to buffer\n"));
		uint8 *buffer_ptr = buffer;
		for (int i=0; i<sg_size; i++) {
			uint32 len = sg_len[i];
			D(bug("  %d bytes from %08lx\n", len, sg_ptr[i]));
			memcpy(buffer_ptr, sg_ptr[i], len);
			buffer_ptr += len;
		}
	}

	memcpy(scsi_buffer.data,buffer,data_length);
	scsi_buffer.size=data_length;
	SCSI_Emulate_Command(cmd_buffer);
	*stat=SCSIdisk[target].status;

	
		D(bug(" reading from buffer\n"));
		uint8 *buffer_ptr = scsi_buffer.data;	//buffer;
		for (int i=0; i<sg_size; i++) {
			uint32 len = sg_len[i];
			D(bug("  %d bytes to %08lx\n", len, sg_ptr[i]));
			memcpy(sg_ptr[i], buffer_ptr, len);
			buffer_ptr += len;
		}



return true;
}







void SCSI_Emulate_Command(unsigned char *cdb)	{
    unsigned char opcode = cdb[0];
    //unsigned char target = 1; SCSIbus.target;
    
    /* First check for lun-independent commands */
    switch (opcode) {
        case CMD_INQUIRY:
            //printf( "SCSI command: Inquiry\n");
            SCSI_Inquiry(cdb);
            break;
        case CMD_REQ_SENSE:
            //printf( "SCSI command: Request sense\n");
            //SCSI_RequestSense(cdb);
            break;
    /* Check if the specified lun is valid for our disk */
        default:
            if (SCSIdisk[target].lun!=LUN_DISK) {
                //printf( "SCSI command: Invalid lun! Check condition.\n");
		
                //SCSIbus.phase = PHASE_ST;
                SCSIdisk[target].status = STAT_CHECK_COND; /* status: check condition */
                SCSIdisk[target].message = MSG_COMPLETE; /* TODO: CHECK THIS! */
                SCSIdisk[target].sense.code = SC_INVALID_LUN;
                SCSIdisk[target].sense.valid = false;
                return;
            }
            
    /* Then check for lun-dependent commands */
            switch(opcode) {
                case CMD_TEST_UNIT_RDY:
                    //printf( "SCSI command: Test unit ready\n");
                    SCSI_TestUnitReady(cdb);
                    break;
                case CMD_READ_CAPACITY1:
                    //printf( "SCSI command: Read capacity\n");
                    SCSI_ReadCapacity(cdb);
                    break;
                case CMD_READ_SECTOR:
                case CMD_READ_SECTOR1:
                    //printf( "SCSI command: Read sector\n");
                    SCSI_ReadSector(cdb);
                    break;
                case CMD_WRITE_SECTOR:
                case CMD_WRITE_SECTOR1:
                    //printf( "SCSI command: Write sector\n");
                    SCSI_WriteSector(cdb);
                    break;
                case CMD_SEEK:
                    //printf( "SCSI command: Seek\n");
                    SCSIabort();
                    break;
                case CMD_SHIP:
                    //printf( "SCSI command: Ship\n");
                    SCSI_StartStop(cdb);
                    break;
                case CMD_MODESELECT:
                    //printf( "SCSI command: Mode select\n");
                    SCSIabort();
                    break;
                case CMD_MODESENSE:
                    //printf( "SCSI command: Mode sense\n");
                    SCSI_ModeSense(cdb);
                    break;
                case CMD_FORMAT_DRIVE:
                    //printf( "SCSI command: Format drive\n");
                    SCSIabort();
                    break;
                /* as of yet unsupported commands */
                case CMD_VERIFY_TRACK:
                case CMD_FORMAT_TRACK:
                case CMD_CORRECTION:
                default:
                    //printf( "SCSI command: Unknown Command\n");
                    SCSIdisk[target].status = STAT_CHECK_COND;
                    SCSIdisk[target].sense.code = SC_INVALID_CMD;
                    SCSIdisk[target].sense.valid = false;
                    //SCSIbus.phase = PHASE_ST;
                    break;
            }
            break;
    }
    
    SCSIdisk[target].message = MSG_COMPLETE;
    
    /* Update the led each time a command is processed */
//	Statusbar_BlinkLed(DEVICE_LED_SCSI);
}

void SCSIabort(void)
{
	//do what?
}



void SCSI_Inquiry (unsigned char *cdb) {
    //unsigned char target = 1;	//SCSIbus.target;
    
    if (SCSIdisk[target].cdrom) {
        inquiry_bytes[0] = DEVTYPE_READONLY;
        inquiry_bytes[1] |= 0x80;
        inquiry_bytes[16] = 'C';
        inquiry_bytes[18] = '-';
        inquiry_bytes[19] = 'R';
        inquiry_bytes[20] = 'O';
        inquiry_bytes[21] = 'M';
        //printf( "[SCSI] Disk is CD-ROM\n");
    } else {
        inquiry_bytes[0] = DEVTYPE_DISK;
        inquiry_bytes[1] &= ~0x80;
        inquiry_bytes[16] = 'H';
        inquiry_bytes[18] = 'D';
        inquiry_bytes[19] = ' ';
        inquiry_bytes[20] = ' ';
        inquiry_bytes[21] = ' ';
        //printf( "[SCSI] Disk is HDD\n");
    }
    
    if (SCSIdisk[target].lun!=LUN_DISK) {
        inquiry_bytes[0] = DEVTYPE_NOTPRESENT;
    }
    
    scsi_buffer.disk=false;
    scsi_buffer.limit = scsi_buffer.size = SCSI_GetTransferLength(cdb[0], cdb);
    if (scsi_buffer.limit > (int)sizeof(inquiry_bytes)) {
        scsi_buffer.limit = scsi_buffer.size = sizeof(inquiry_bytes);
    }

    memcpy(scsi_buffer.data, inquiry_bytes, scsi_buffer.limit);

    //printf( "[SCSI] Inquiry data length: %d", scsi_buffer.limit);
    //printf( "[SCSI] Inquiry Data: %c,%c,%c,%c,%c,%c,%c,%c\n",scsi_buffer.data[8],
    //           scsi_buffer.data[9],scsi_buffer.data[10],scsi_buffer.data[11],scsi_buffer.data[12],
    //           scsi_buffer.data[13],scsi_buffer.data[14],scsi_buffer.data[15]);
    
    SCSIdisk[target].status = STAT_GOOD;
//    SCSIbus.phase = PHASE_DI;
	SCSIdisk[target].sense.code = SC_NO_ERROR;
    SCSIdisk[target].sense.valid = false;
}



int SCSI_GetTransferLength(uint8 opcode, uint8 *cdb)
{
	return opcode < 0x20?
    // class 0
    cdb[4] :
    // class 1
    COMMAND_ReadInt16(cdb, 7);
}


void SCSI_TestUnitReady(uint8 *cdb) {
    //unsigned char target = 1;
	SCSIdisk[target].status = STAT_GOOD;
//    SCSIbus.phase = PHASE_ST;
}

void SCSI_StartStop(uint8 *cdb) {
//	unsigned char target=1;
    SCSIdisk[target].status = STAT_GOOD;
//    SCSIbus.phase = PHASE_ST;
}


void SCSI_ReadCapacity(uint8 *cdb) {
    //uint8 target=1;
    //Uint8 target = SCSIbus.target;
    
    //printf("[SCSI] Read disk image: size = %i byte\n", SCSIdisk[target].size);
  
    uint32 sectors = (SCSIdisk[target].size / BLOCKSIZE) - 1; /* last LBA */
    
    static uint8 scsi_disksize[8];

    scsi_disksize[0] = (sectors >> 24) & 0xFF;
    scsi_disksize[1] = (sectors >> 16) & 0xFF;
    scsi_disksize[2] = (sectors >> 8) & 0xFF;
    scsi_disksize[3] = sectors & 0xFF;
    scsi_disksize[4] = (BLOCKSIZE >> 24) & 0xFF;
    scsi_disksize[5] = (BLOCKSIZE >> 16) & 0xFF;
    scsi_disksize[6] = (BLOCKSIZE >> 8) & 0xFF;
    scsi_disksize[7] = BLOCKSIZE & 0xFF;
    
    memcpy(scsi_buffer.data, scsi_disksize, 8);
    scsi_buffer.limit=scsi_buffer.size=8;
    scsi_buffer.disk=false;
    
    SCSIdisk[target].status = STAT_GOOD;
//    SCSIbus.phase = PHASE_DI;
    SCSIdisk[target].sense.code = SC_NO_ERROR;
    SCSIdisk[target].sense.valid = false;
}





void SCSI_ReadSector(uint8 *cdb) {
    //uint8 target = 1;	//SCSIbus.target;
    
    SCSIdisk[target].lba = SCSI_GetOffset(cdb[0], cdb);
    SCSIdisk[target].blockcounter = SCSI_GetCount(cdb[0], cdb);
    scsi_buffer.disk=true;
    scsi_buffer.size=0;
    //SCSIbus.phase = PHASE_DI;
    //printf("[SCSI] Read sector: %i block(s) at offset %i (blocksize: %i byte)",
    //           SCSIdisk[target].blockcounter, SCSIdisk[target].lba, BLOCKSIZE);
    scsi_read_sector();
}

void scsi_read_sector(void) {
    //uint8 target = 1;	//SCSIbus.target;
	int loop=0;
    
    if (SCSIdisk[target].blockcounter==0) {
        //SCSIbus.phase = PHASE_ST;
        return;
    }

  	  int n;	//jj
	while (SCSIdisk[target].blockcounter >0){
    
//	    printf("[SCSI] Reading block at offset %i (%i blocks remaining).\n",
//        	       SCSIdisk[target].lba,SCSIdisk[target].blockcounter-1);
//printf(".%d",SCSIdisk[target].lba);
    
	    /* seek to the position */
		if ((SCSIdisk[target].dsk==NULL) || (fseek(SCSIdisk[target].dsk, SCSIdisk[target].lba*BLOCKSIZE, SEEK_SET) != 0)) {
	        n = 0;
		} else {
	        n = fread(scsi_buffer.data+(loop*512), BLOCKSIZE, 1, SCSIdisk[target].dsk);
        	scsi_buffer.limit=scsi_buffer.size=BLOCKSIZE;
	    }
    
	    if (n == 1) {
        	SCSIdisk[target].status = STAT_GOOD;
	        SCSIdisk[target].sense.code = SC_NO_ERROR;
        	SCSIdisk[target].sense.valid = false;
	        SCSIdisk[target].lba++;
	       	SCSIdisk[target].blockcounter--;
		//printf("scsi_read_sector ok\n");
	    } else {
		printf("ERROR WITH SCSI READ SECTOR!\n");
        	SCSIdisk[target].status = STAT_CHECK_COND;
	        SCSIdisk[target].sense.code = SC_INVALID_LBA;
        	SCSIdisk[target].sense.valid = true;
	        SCSIdisk[target].sense.info = SCSIdisk[target].lba;
        	//SCSIbus.phase = PHASE_ST;
	    }
	loop++;
	}//end while block counter >0
//printf("\t%d\n",loop);
}



void SCSI_WriteSector(uint8 *cdb) {
    //uint8 target = 1;	//SCSIbus.target;
    
    SCSIdisk[target].lba = SCSI_GetOffset(cdb[0], cdb);
    SCSIdisk[target].blockcounter = SCSI_GetCount(cdb[0], cdb);
    
    if (SCSIdisk[target].cdrom) {
        //printf("[SCSI] Write sector: Disk is write protected! Check condition.\n");
        SCSIdisk[target].status = STAT_CHECK_COND;
        SCSIdisk[target].sense.code = SC_WRITE_PROTECT;
        SCSIdisk[target].sense.valid = false;
        //SCSIbus.phase = PHASE_ST;
        return;
    }
    scsi_buffer.disk=true;
    scsi_buffer.size=0;
    scsi_buffer.limit=BLOCKSIZE;
    //SCSIbus.phase = PHASE_DO;
    //printf("[SCSI] Write sector: %i block(s) at offset %i (blocksize: %i byte)",
    //           SCSIdisk[target].blockcounter, SCSIdisk[target].lba, BLOCKSIZE);
    scsi_write_sector();
}

void scsi_write_sector(void) {
    //uint8 target = 1;	//SCSIbus.target;
    int loop = 0;
    int n=0;
    
	while (SCSIdisk[target].blockcounter >0)
	{
//	    printf("[SCSI] Writing block at offset %i (%i blocks remaining).\n",
//        	       SCSIdisk[target].lba,SCSIdisk[target].blockcounter-1);
    
	    /* seek to the position */
		if ((SCSIdisk[target].dsk==NULL) || (fseek(SCSIdisk[target].dsk, SCSIdisk[target].lba*BLOCKSIZE, SEEK_SET) != 0)) {
	        n = 0;
		} else {
#if 1
	        n = fwrite(scsi_buffer.data+(loop*512), BLOCKSIZE, 1, SCSIdisk[target].dsk);
#else
	        n=1;
	        printf("[SCSI] WARNING: File write disabled!");
#endif
	        scsi_buffer.limit=BLOCKSIZE;
	        scsi_buffer.size=0;
	    }
    
	    if (n == 1) {
	        SCSIdisk[target].status = STAT_GOOD;
	        SCSIdisk[target].sense.code = SC_NO_ERROR;
	        SCSIdisk[target].sense.valid = false;
	        SCSIdisk[target].lba++;
	        SCSIdisk[target].blockcounter--;
	        if (SCSIdisk[target].blockcounter==0) {
	            //SCSIbus.phase = PHASE_ST;
	        }
	    } else {
		printf("write error!\n");
	        SCSIdisk[target].status = STAT_CHECK_COND;
	        SCSIdisk[target].sense.code = SC_INVALID_LBA;
	        SCSIdisk[target].sense.valid = true;
	        SCSIdisk[target].sense.info = SCSIdisk[target].lba;
	        //SCSIbus.phase = PHASE_ST;
	    }
	//printf(".%d",SCSIdisk[target].lba);
	loop++;
	}//end while block counter
//printf("\t%d\n",loop);
}






unsigned long SCSI_GetOffset(uint8 opcode, uint8 *cdb)
{
	return opcode < 0x20?
    // class 0
    (COMMAND_ReadInt24(cdb, 1) & 0x1FFFFF) :
    // class 1
    COMMAND_ReadInt32(cdb, 2);
}

// get reserved count for SCSI reply
int SCSI_GetCount(uint8 opcode, uint8 *cdb)
{
	return opcode < 0x20?
    // class 0
    ((cdb[4]==0)?0x100:cdb[4]) :
    // class 1
    COMMAND_ReadInt16(cdb, 7);
}





void SCSI_ModeSense(uint8 *cdb) {
    //uint8 target = 1;	//SCSIbus.target;
    
    uint8 retbuf[256];
    MODEPAGE page;
    
    uint32 sectors = SCSIdisk[target].size / BLOCKSIZE;

    uint8 pagecontrol = (cdb[2] & 0x0C) >> 6;
    uint8 pagecode = cdb[2] & 0x3F;
    uint8 dbd = cdb[1] & 0x08; // disable block descriptor
        
    //printf("[SCSI] Mode Sense: page = %02x, page_control = %i, %s\n", pagecode, pagecontrol, dbd == 0x08 ? "block descriptor disabled" : "block descriptor enabled");
    
    /* Header */
    retbuf[0] = 0x00; // length of following data
    retbuf[1] = 0x00; // medium type (always 0)
    retbuf[2] = SCSIdisk[target].cdrom == true ? 0x80 : 0x00; // if media is read-only 0x80, else 0x00
    retbuf[3] = 0x08; // block descriptor length
    
    /* Block descriptor data */
    uint8 header_size = 4;
    if (!dbd) {
        retbuf[4] = 0x00; // media density code
        retbuf[5] = sectors >> 16;  // Number of blocks, high (?)
        retbuf[6] = sectors >> 8;   // Number of blocks, med (?)
        retbuf[7] = sectors;        // Number of blocks, low (?)
        retbuf[8] = 0x00; // reserved
        retbuf[9] = (BLOCKSIZE >> 16) & 0xFF;      // Block size in bytes, high
        retbuf[10] = (BLOCKSIZE >> 8) & 0xFF;     // Block size in bytes, med
        retbuf[11] = BLOCKSIZE & 0xFF;     // Block size in bytes, low
        header_size = 12;
        //printf("[SCSI] Mode Sense: Block descriptor data: %s, size = %i blocks, blocksize = %i byte\n",
        //           SCSIdisk[target].cdrom == true ? "disk is read-only" : "disk is read/write" , sectors, BLOCKSIZE);
    }
    retbuf[0] = header_size - 1;
    
    /* Mode Pages */
    if (pagecode == 0x3F) { // return all pages!
        uint8 offset = header_size;
        uint8 counter;
        for (pagecode = 0; pagecode < 0x3F; pagecode++) {
            page = SCSI_GetModePage(pagecode);

            switch (pagecontrol) {
                case 0: // current values (not supported, using default values)
                    memcpy(page.current, page.modepage, page.pagesize);
                    for (counter = 0; counter < page.pagesize; counter++) {
                        retbuf[counter+offset] = page.current[counter];
                    }
                    break;
                case 1: // changeable values (not supported, all 0)
                    memset(page.changeable, 0x00, page.pagesize);
                    for (counter = 0; counter < page.pagesize; counter++) {
                        retbuf[counter+offset] = page.changeable[counter];
                    }
                    break;
                case 2: // default values
                    for (counter = 0; counter < page.pagesize; counter++) {
                        retbuf[counter+offset] = page.modepage[counter];
                    }
                    break;
                case 3: // saved values (not supported, using default values)
                    memcpy(page.saved, page.modepage, page.pagesize);
                    for (counter = 0; counter < page.pagesize; counter++) {
                        retbuf[counter+offset] = page.saved[counter];
                    }
                    break;
                    
                default:
                    break;
            }
            offset += page.pagesize;
            retbuf[0] += page.pagesize;
        }
    } else { // return only single requested page
        page = SCSI_GetModePage(pagecode);
        
        uint8 counter;
        switch (pagecontrol) {
            case 0: // current values (not supported, using default values)
                memcpy(page.current, page.modepage, page.pagesize);
                for (counter = 0; counter < page.pagesize; counter++) {
                    retbuf[counter+header_size] = page.current[counter];
                }
                break;
            case 1: // changeable values (not supported, all 0)
                memset(page.changeable, 0x00, page.pagesize);
                for (counter = 0; counter < page.pagesize; counter++) {
                    retbuf[counter+header_size] = page.changeable[counter];
                }
                break;
            case 2: // default values
                for (counter = 0; counter < page.pagesize; counter++) {
                    retbuf[counter+header_size] = page.modepage[counter];
                }
                break;
            case 3: // saved values (not supported, using default values)
                memcpy(page.saved, page.modepage, page.pagesize);
                for (counter = 0; counter < page.pagesize; counter++) {
                    retbuf[counter+header_size] = page.saved[counter];
                }
                break;
                
            default:
                break;
        }
        
        retbuf[0] += page.pagesize;
    }
    
    scsi_buffer.disk=false;
    scsi_buffer.limit = scsi_buffer.size = retbuf[0]+1;
    if (scsi_buffer.limit > SCSI_GetTransferLength(cdb[0], cdb)) {
        scsi_buffer.limit = scsi_buffer.size = SCSI_GetTransferLength(cdb[0], cdb);
    }
    memcpy(scsi_buffer.data, retbuf, scsi_buffer.limit);

    SCSIdisk[target].status = STAT_GOOD;
    //SCSIbus.phase = PHASE_DI;
    SCSIdisk[target].sense.code = SC_NO_ERROR;
    SCSIdisk[target].sense.valid = false;
}





MODEPAGE SCSI_GetModePage(uint8 pagecode) {
    //uint8 target = 1;//SCSIbus.target;
    
    MODEPAGE page;
    
    switch (pagecode) {
        case 0x00: // operating page
            page.pagesize = 4;
            page.modepage[0] = 0x00; // &0x80: page savable? (not supported!), &0x7F: page code = 0x00
            page.modepage[1] = 0x02; // page length = 2
            page.modepage[2] = 0x80; // &0x80: usage bit = 1, &0x10: disable unit attention = 0
            page.modepage[3] = 0x00; // &0x7F: device type qualifier = 0x00, see inquiry!
            break;

        case 0x01: // error recovery page
            page.pagesize = 8;
            page.modepage[0] = 0x01; // &0x80: page savable? (not supported!), &0x7F: page code = 0x01
            page.modepage[1] = 0x06; // page length = 6
            page.modepage[2] = 0x00; // AWRE, ARRE, TB, RC, EER, PER, DTE, DCR
            page.modepage[3] = 0x1B; // retry count
            page.modepage[4] = 0x0B; // correction span in bits
            page.modepage[5] = 0x00; // head offset count
            page.modepage[6] = 0x00; // data strobe offset count
            page.modepage[7] = 0xFF; // recovery time limit
            break;
            
        case 0x03: // format device page
            page.pagesize = 0;
            //printf("[SCSI] Mode Sense: Page %02x not yet emulated!\n", pagecode);
            //abort();
            break;

        case 0x04: // rigid disc geometry page
        {
            uint32 num_sectors = SCSIdisk[target].size/BLOCKSIZE;
            
            uint32 cylinders, heads, sectors;

            SCSI_GuessGeometry(num_sectors, &cylinders, &heads, &sectors);
            
            //printf("[SCSI] Disk geometry: %i sectors, %i cylinders, %i heads\n", sectors, cylinders, heads);
            
            page.pagesize = 20;
            page.modepage[0] = 0x04; // &0x80: page savable? (not supported!), &0x7F: page code = 0x04
            page.modepage[1] = 0x12;
            page.modepage[2] = (cylinders >> 16) & 0xFF;
            page.modepage[3] = (cylinders >> 8) & 0xFF;
            page.modepage[4] = cylinders & 0xFF;
            page.modepage[5] = heads;
            page.modepage[6] = 0x00; // 6,7,8: starting cylinder - write precomp (not supported)
            page.modepage[7] = 0x00;
            page.modepage[8] = 0x00;
            page.modepage[9] = 0x00; // 9,10,11: starting cylinder - reduced write current (not supported)
            page.modepage[10] = 0x00;
            page.modepage[11] = 0x00;
            page.modepage[12] = 0x00; // 12,13: drive step rate (not supported)
            page.modepage[13] = 0x00;
            page.modepage[14] = 0x00; // 14,15,16: loading zone cylinder (not supported)
            page.modepage[15] = 0x00;
            page.modepage[16] = 0x00;
            page.modepage[17] = 0x00; // &0x03: rotational position locking
            page.modepage[18] = 0x00; // rotational position lock offset
            page.modepage[19] = 0x00; // reserved
        }
            break;
	case 0x30: // I HAVE NO IDEA
			page.pagesize = 1;
			page.modepage[0]=0x30&0x7F;
			page.modepage[1]=0x1;
			page.modepage[2]=0x0;
			break;
            
        case 0x02: // disconnect/reconnect page
        case 0x08: // caching page
        case 0x0C: // notch page
        case 0x0D: // power condition page
        case 0x38: // cache control page
        case 0x3C: // soft ID page (EEPROM)
            page.pagesize = 0;
            //printf("[SCSI] Mode Sense: Page %02x not yet emulated!\n", pagecode);
            //abort();
            break;
            
        default:
            page.pagesize = 0;
            //printf("[SCSI] Mode Sense: Invalid page code: %02x!\n", pagecode);
            break;
    }
    return page;
}




void SCSI_GuessGeometry(uint32 size, uint32 *cylinders, uint32 *heads, uint32 *sectors)
{
    uint32 c,h,s;
    
    for (h=16; h>0; h--) {
        for (s=63; s>15; s--) {
            if ((size%(s*h))==0) {
                c=size/(s*h);
                *cylinders=c;
                *heads=h;
                *sectors=s;
	    //printf("[SCSI] Disk geometry: using %d cylinders %d heads %sectors.\n",cylinders,heads,sectors);
                return;
            }
        }
    }
    //printf("[SCSI] Disk geometry: No valid geometry found! Using default.\n");
    
    h=16;
    s=63;
    c=size/(s*h);
    if ((size%(s*h))!=0) {
        c+=1;
    }
    *cylinders=c;
    *heads=h;
    *sectors=s;
}
