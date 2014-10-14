#include <winsock2.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
//#include <io.h>
//#include <direct.h>		//for _chdir

// Prototypes
unsigned short ROMVersion;
unsigned char *ROMBaseHost;

int main(int argc, char*argv[])
{
int x;
if(argc<2)
	{exit(0);}
printf("Checking ROM %s\n",argv[1]);
ROMBaseHost =(unsigned char*) malloc(1024*1024+1024);
memset(ROMBaseHost,0xAA,0x100000);
int rom_fd = _open(argv[1] , O_RDONLY|O_BINARY);
if(rom_fd<0)
	{exit(0);}
x= _read(rom_fd, ROMBaseHost, 1024*1024);//ROMSize);	

ROMVersion = ntohs(*(unsigned short *)(ROMBaseHost + 8));
close(rom_fd);
printf("\nYour ROM is Version is:\ndecimal %d\tHex 0x%x\n",ROMVersion,ROMVersion);
if(ROMVersion==0x0000)
	printf("64K rom\n");
if(ROMVersion==0x0075)
	printf("Macintosh Plus 128kb\n");
if(ROMVersion==0x0276)
	printf("Macintosh Classic 256kb/512kb\n");
if(ROMVersion==0x0178)
	printf("24-bit AKA Dirty ROM (256kb)\n");
if(ROMVersion==0x067c)
	printf("32-bit Clean ROM (512kb/1MB)\n");
return 0;
}