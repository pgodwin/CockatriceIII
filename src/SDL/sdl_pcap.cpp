/*
 *  ether_unix.cpp - Ethernet device driver, Unix specific stuff
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

#include "sysdeps.h"

#ifdef WIN32
#include <windows.h>
//#include <sys\ioctl.h>	both for visual C++??
//#include <sys\poll.h>
#include <SDL\SDL.h>
#include <SDL\SDL_thread.h>
#include <pcap.h>
static HINSTANCE hLib = 0;                      /* handle to DLL */
static char* lib_name = "wpcap.dll";
#else
#include <SDL/SDL.h>
#include <SDL/SDL_thread.h>
#include <dlfcn.h>
#include <pcap/pcap.h>
static void *hLib = 0;                      /* handle to Library */
#define __cdecl
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>	//for malloc/free
#include <string.h>	//for memcpy

#if defined __APPLE__ || defined __linux
#define UNIX
#endif

pcap_t *pcap;
//const unsigned char *data;
//struct pcap_pkthdr h;

/**************dynamic pcap*************/

#ifdef __APPLE__
static char* lib_name = "/usr/lib/libpcap.A.dylib";
#elif __linux
static char* lib_name = "libpcap.so";
#endif

typedef pcap_t* (__cdecl * PCAP_OPEN_LIVE)(const char *, int, int, int, char *);
typedef int (__cdecl * PCAP_SENDPACKET)(pcap_t* handle, const u_char* msg, int len);
typedef int (__cdecl * PCAP_SETNONBLOCK)(pcap_t *, int, char *);
typedef const u_char*(__cdecl *PCAP_NEXT)(pcap_t *, struct pcap_pkthdr *);
typedef const char*(__cdecl *PCAP_LIB_VERSION)(void);
typedef void (__cdecl *PCAP_CLOSE)(pcap_t *);
typedef int  (__cdecl *PCAP_GETNONBLOCK)(pcap_t *p, char *errbuf);
typedef int (__cdecl *PCAP_COMPILE)(pcap_t *p, struct bpf_program *fp, const char *str, int optimize, bpf_u_int32 netmask);
typedef int (__cdecl *PCAP_SETFILTER)(pcap_t *p, struct bpf_program *fp);



PCAP_LIB_VERSION 	_pcap_lib_version;
PCAP_OPEN_LIVE		_pcap_open_live;
PCAP_SENDPACKET		_pcap_sendpacket;
PCAP_SETNONBLOCK	_pcap_setnonblock;
PCAP_NEXT		_pcap_next;
PCAP_CLOSE		_pcap_close;
PCAP_GETNONBLOCK	_pcap_getnonblock;
PCAP_COMPILE		_pcap_compile;
PCAP_SETFILTER		_pcap_setfilter;

/****************/

//SLIRP stuff
extern "C" int slirp_init(void);
int slirp_redir(int is_udp, int host_port, struct in_addr guest_addr, int guest_port);
extern "C" void slirp_input(const uint8_t *pkt, int pkt_len);
extern "C" int slirp_select_fill(int *pnfds, 
                       fd_set *readfds, fd_set *writefds, fd_set *xfds);
extern "C" void slirp_select_poll(fd_set *readfds, fd_set *writefds, fd_set *xfds);
extern "C" void slirp_exit(int);
void slirp_debug_init(char*,int);
extern "C" void slirp_output(const uint8 *pkt, int pkt_len);
extern "C" int slirp_can_output(void);

//simple queue thing I found
//hopefully this can replace tbuff
struct queuepacket{
        int len;
        unsigned char data[2000];
};
typedef struct queuepacket *queueElementT;
typedef struct queueCDT *queueADT;	
extern "C" queueADT QueueCreate(void);
extern "C" void QueueDestroy(queueADT queue);
extern "C" void QueueEnter(queueADT queue, queueElementT element);
extern "C" queueElementT QueueDelete(queueADT queue);
extern "C" int QueueIsEmpty(queueADT queue);
extern "C" int QueueIsFull(queueADT queue);
extern "C" int QueuePeek(queueADT queue);

queueADT	slirpq;


//unsigned char tbuff[2000];	//temporary packet buffer
//int tbufflen=0;
int slirp_inited=0;
/*
static SDL_sem *slirp_sem = NULL;
static SDL_mutex *slirp_packet_xfer = NULL;
*/
static SDL_mutex *slirp_select_fill_mutex = NULL;
static SDL_mutex *slirp_select_poll_mutex = NULL;
static SDL_mutex *slirp_queue_mutex = NULL;
static SDL_mutex *slirp_mutex = NULL;


#include "cpu_emulation.h"
#include "main.h"
#include "macos_util.h"
#include "prefs.h"
#include "user_strings.h"
#include "ether.h"
#include "ether_defs.h"

#define DEBUG 0
#include "debug.h"

#define MONITOR 0


// List of attached protocols
struct NetProtocol {
	NetProtocol *next;
	uint16 type;
	uint32 handler;
};

static NetProtocol *prot_list = NULL;


// Global variables

static bool thread_active = false;			// Flag: Packet reception thread installed
//static sem_t int_ack;						// Interrupt acknowledge semaphore
SDL_sem *int_ack=NULL;
static bool is_ethertap=0;					// Flag: Ethernet device is ethertap
static bool is_pcap=0;					// Flag: Ethernet device is ethertap
static bool is_slirp=0;					// Flag: Ethernet device is ethertap

// Prototypes
//static void *receive_func(void *arg);
int receive_func(void *arg);


/*
 *  Find protocol in list
 */

static NetProtocol *find_protocol(uint16 type)
{
	// All 802.2 types are the same
	if (type <= 1500)
		type = 0;

	// Search list (we could use hashing here but there are usually only three
	// handlers installed: 0x0000 for AppleTalk and 0x0800/0x0806 for TCP/IP)
	NetProtocol *p = prot_list;
	while (p) {
		if (p->type == type)
			return p;
		p = p->next;
	}
	return NULL;
}


/*
 *  Remove all protocols
 */

static void remove_all_protocols(void)
{
	NetProtocol *p = prot_list;
	while (p) {
		NetProtocol *next = p->next;
		delete p;
		p = next;
	}
	prot_list = NULL;
}


/*
 *  Initialization
 */

void EtherInit(void)
{
	int nonblock = 1;
	char str[256];
	char dev_name[16];
	char errbuf[PCAP_ERRBUF_SIZE];

	// Do nothing if no Ethernet device specified
	const char *name = PrefsFindString("ether");

	if (name == NULL)
		return;

	// Is it Ethertap?
	is_ethertap = (strncmp(name, "tun", 3) == 0);
	if(!is_ethertap)
	is_ethertap = (strncmp(name, "tap", 3) == 0);
	is_slirp = (strncmp(name, "slirp", 5) == 0);
	if(!(is_ethertap || is_slirp))
		is_pcap=1;

	if(is_ethertap)
		{
		printf("Sorry this version has removed Tun/Tap support.\n");
		return;	//This is missing
		}
	printf("EtherInit with [%s%s%s]\n",is_ethertap?"ethertap(not implimented)":"",is_slirp ? "SLiRP":"",is_pcap ? "PCAP":"");

	/*** Open the Pcap device ****/
	if (is_pcap)
		{
		int rc;
		memset(errbuf,0,sizeof(errbuf));
#ifdef WIN32
		 hLib = LoadLibraryA(lib_name);
			if(hLib==0)
			{printf("Failed to load %s\n",lib_name);is_pcap=0;return ;}
		_pcap_lib_version =(PCAP_LIB_VERSION)GetProcAddress(hLib,"pcap_lib_version");
		_pcap_open_live=(PCAP_OPEN_LIVE)GetProcAddress(hLib,"pcap_open_live");		
		_pcap_sendpacket=(PCAP_SENDPACKET)GetProcAddress(hLib,"pcap_sendpacket");		
		_pcap_setnonblock=(PCAP_SETNONBLOCK)GetProcAddress(hLib,"pcap_setnonblock");	
		_pcap_next=(PCAP_NEXT)GetProcAddress(hLib,"pcap_next");		
		_pcap_close=(PCAP_CLOSE)GetProcAddress(hLib,"pcap_close");
		_pcap_getnonblock=(PCAP_GETNONBLOCK)GetProcAddress(hLib,"pcap_getnonblock");
		_pcap_compile=(PCAP_COMPILE)GetProcAddress(hLib,"pcap_compile");
		_pcap_setfilter=(PCAP_SETFILTER)GetProcAddress(hLib,"pcap_setfilter");
#else
		hLib =  hLib = dlopen(lib_name, RTLD_NOW);
                        if(hLib==0)
                        {printf("Failed to load %s\n",lib_name);is_pcap=0;return ;}
                _pcap_open_live=(PCAP_OPEN_LIVE)dlsym(hLib,"pcap_open_live");
                _pcap_sendpacket=(PCAP_SENDPACKET)dlsym(hLib,"pcap_sendpacket");
                _pcap_setnonblock=(PCAP_SETNONBLOCK)dlsym(hLib,"pcap_setnonblock");
                _pcap_next=(PCAP_NEXT)dlsym(hLib,"pcap_next");
                _pcap_close=(PCAP_CLOSE)dlsym(hLib,"pcap_close");
		_pcap_lib_version =(PCAP_LIB_VERSION)dlsym(hLib,"pcap_lib_version");
		_pcap_getnonblock=(PCAP_GETNONBLOCK)dlsym(hLib,"pcap_getnonblock");
		_pcap_compile=(PCAP_COMPILE)dlsym(hLib,"pcap_compile");
		_pcap_setfilter=(PCAP_SETFILTER)dlsym(hLib,"pcap_setfilter");

#endif
		if(_pcap_lib_version && _pcap_open_live && _pcap_sendpacket && _pcap_setnonblock && _pcap_next && _pcap_close && _pcap_getnonblock)
			{
			printf("Pcap version [%s]\n",_pcap_lib_version());
		//char *device, int snaplen,int promisc, int to_ms, char *errbuf);


		//BLOCKING
		if((pcap=_pcap_open_live(name,1518,1,-1,errbuf))==0)
		//NONBLOCKING
		//if((pcap=_pcap_open_live(name,1518,1,15,errbuf))==0)
			{printf("ethernet.c: pcap_open_live error on %s!\n",name);exit(-1);}
			}
		else	{	
		printf("%d %d %d %d %d %d %d\n",_pcap_lib_version, _pcap_open_live,_pcap_sendpacket,_pcap_setnonblock,_pcap_next,_pcap_close,_pcap_getnonblock);
			is_pcap=0;
			}

		//Time to check that we are in non-blocking mode.
#if 0
		rc=_pcap_getnonblock(pcap,errbuf);
		printf("pcap is currently in %s mode\n",rc? "non-blocking":"blocking");
		switch(rc)
		{
		case 0:
			printf("Setting interface to non-blocking mode..");
			rc=_pcap_setnonblock(pcap,1,errbuf);
			if(rc==0)	{  //no errors!
					printf("..");
					rc=_pcap_getnonblock(pcap,errbuf);
					if(rc==1)	{
						printf("..!",rc);
						}
					else{printf("unable to set pcap into non-blocking mode!\nContinuining anyways.\n");}
					}//end set nonblock
			else{printf("There was an unexpected error of [%s]\n\nexiting.\n",errbuf);exit(0);}
			printf("\n");
			break;
		case 1:
			printf("non blocking\n");
			break;
		default:
			printf("this isn't right!!!\n");
			exit(0);
			break;
		}
#endif

	} 	//end if is_pcap


	if (is_slirp)
		{
		int rc;
		rc=slirp_init();
		slirp_select_fill_mutex=SDL_CreateMutex();
		slirp_select_poll_mutex=SDL_CreateMutex();
		slirp_inited=1;
		}


	// Get Ethernet address
	if (!is_ethertap) {
#ifdef UNIX
		pid_t p = getpid();	// If configured for multicast, ethertap requires that the lower 32 bit of the Ethernet address are our PID
#else
		int p=GetCurrentProcessId();
#endif
		ether_addr[0] = 0xfe;
		ether_addr[1] = 0xfd;
		ether_addr[2] = p >> 24;
		ether_addr[3] = p >> 16;
		ether_addr[4] = p >> 8;
		ether_addr[5] = p;
	printf("Ethernet address %02x %02x %02x %02x %02x %02x\n", ether_addr[0], ether_addr[1], ether_addr[2], ether_addr[3], ether_addr[4], ether_addr[5]);
	} else
	D(bug("Ethernet address %02x %02x %02x %02x %02x %02x\n", ether_addr[0], ether_addr[1], ether_addr[2], ether_addr[3], ether_addr[4], ether_addr[5]));

#if 1
	/*	Install a packet filter to reduce incomming traffic to our poor mac	**/
//(((ether dst 09:00:07:ff:ff:ff) or (ether dst ff:ff:ff:ff:ff:ff) or (ether dst fe:fd:00:00:16:48))) 
	if(is_pcap)	{
		if(_pcap_compile && _pcap_setfilter) {	//we can do this!
		struct bpf_program fp;
		char filter_exp[255];
		printf("Building packet filter...");
		sprintf(filter_exp,"(((ether dst 09:00:07:ff:ff:ff) or (ether dst ff:ff:ff:ff:ff:ff) or (ether dst %02x:%02x:%02x:%02x:%02x:%02x)))", \
		ether_addr[0], ether_addr[1], ether_addr[2], ether_addr[3], ether_addr[4], ether_addr[5]);

		//I'm doing a MAC level filter so TCP/IP doesn't matter.
		if (_pcap_compile(pcap, &fp, filter_exp, 0, 0xffffffff) == -1) {
			printf("\nCouldn't compile filter\n");
			}
			else	{
			printf("...");
			if (_pcap_setfilter(pcap, &fp) == -1) {
			printf("\nError installing pcap filter.\n");
				}//end of set_filter failure
			else	{
				printf("...!\n");
				}
			}
		//printf("Using filter\n%s\n",filter_exp);
		//scanf(filter_exp);	//pause
		}
		else
		{printf("Your platform lacks pcap_compile & pcap_setfilter\n");}
	}//end creating a capture filter
#endif

	//slirpq for everyone now.
	slirpq = QueueCreate();
	slirp_queue_mutex = SDL_CreateMutex();
	slirp_mutex=SDL_CreateMutex();

	int_ack=SDL_CreateSemaphore(0);
	SDL_CreateThread(receive_func,NULL);

	// Everything OK
	net_open = true;
	return;

open_error:
	if (thread_active) {
		//pthread_cancel(ether_thread);
		//pthread_join(ether_thread, NULL);
		//sem_destroy(&int_ack);
		//thread_active = false;
		//SDL_KillThread(receive_func);
		SDL_DestroySemaphore(int_ack);
	}
}


/*
 *  Deinitialization
 */

void EtherExit(void)
{
printf("EtherExit called\n");
	// Stop reception thread
	if (thread_active) {
		//pthread_cancel(ether_thread);
		//pthread_join(ether_thread, NULL);
		//sem_destroy(&int_ack);
		SDL_KillThread((SDL_Thread*)receive_func);
		SDL_DestroySemaphore(int_ack);
		thread_active = false;
		//DO NICE SHUTDOWN STUFF...
		if(is_slirp)
			{slirp_exit(0);}
		if(is_pcap)
			{_pcap_close(pcap);}
	}

	// Remove all protocols
	remove_all_protocols();
printf("Removed all protocols\n");
}


/*
 *  Reset
 */

void EtherReset(void)
{
	remove_all_protocols();
}


/*
 *  Add multicast address
 */

int16 ether_add_multicast(uint32 pb)
{
		return noErr;
}


/*
 *  Delete multicast address
 */

int16 ether_del_multicast(uint32 pb)
{
		return noErr;
}


/*
 *  Attach protocol handler
 */

int16 ether_attach_ph(uint16 type, uint32 handler)
{
	// Already attached?
	NetProtocol *p = find_protocol(type);
	if (p != NULL)
		return lapProtErr;
	else {
		// No, create and attach
		p = new NetProtocol;
		p->next = prot_list;
		p->type = type;
		p->handler = handler;
		prot_list = p;
		return noErr;
	}
}


/*
 *  Detach protocol handler
 */

int16 ether_detach_ph(uint16 type)
{
	NetProtocol *p = find_protocol(type);
	if (p != NULL) {
		NetProtocol *q = prot_list;
		if (p == q) {
			prot_list = p->next;
			delete p;
			return noErr;
		}
		while (q) {
			if (q->next == p) {
				q->next = p->next;
				delete p;
				return noErr;
			}
			q = q->next;
		}
		return lapProtErr;
	} else
		return lapProtErr;
}


/*
 *  Transmit raw ethernet packet
 */

int16 ether_write(uint32 wds)
{
	int rc;
	// Set source address
	uint32 hdr = ReadMacInt32(wds + 2);
	Host2Mac_memcpy(hdr + 6, ether_addr, 6);

	// Copy packet to buffer
	uint8 packet[1516], *p = packet;
	int len = 0;

	for (;;) {
		int w = ReadMacInt16(wds);
		if (w == 0)
			break;
		Mac2Host_memcpy(p, ReadMacInt32(wds + 2), w);
		len += w;
		p += w;
		wds += 6;
	}

#if MONITOR
	bug("Sending Ethernet packet:\n");
	for (int i=0; i<len; i++) {
		bug("%02x ", packet[i]);
	}
	bug("\n");
#endif

	// Transmit packet
	if(is_slirp){
		//printf("s");
		SDL_LockMutex(slirp_mutex);
		slirp_input(packet,len);
		SDL_UnlockMutex(slirp_mutex);
		//printf("S");
		return noErr;}
	if(is_pcap){
		int rc;
		SDL_LockMutex(slirp_mutex);
		rc=_pcap_sendpacket(pcap, packet, len);
		SDL_UnlockMutex(slirp_mutex);
		if(rc<0){
                printf("WARNING: Couldn't transmit packet\n");
                return excessCollsns; }
	} else
		return noErr;
return noErr;
}


/*
 *  Packet reception thread
 */
int receive_func(void *arg)
{
	const unsigned char *data;
	struct pcap_pkthdr h;

	for (;;) {
	if(is_slirp){

		if(QueuePeek(slirpq)>0){
		SetInterruptFlag(INTFLAG_ETHER);
		TriggerInterrupt();
		// Wait for interrupt acknowledge by EtherInterrupt()
		SDL_SemWait(int_ack);
		}//endpeek

	}//end slirp
	if(is_pcap){
		SDL_LockMutex(slirp_queue_mutex);
		data=_pcap_next(pcap,&h);
		SDL_UnlockMutex(slirp_queue_mutex);
		if(data==0x0){goto WTF;}	//dont know why this is happening!
//printf("pcap_next just got a packet!\t%d long\t\r",h.caplen);
	if(h.caplen>0)
		{
		if(h.caplen>1516)
			h.caplen=1516;
		if((memcmp(data+6,ether_addr,6))==0)
			{D(bug("ether: we just saw ourselves\n"));}
		else
			{
			struct queuepacket *p;
			p=(struct queuepacket *)malloc(sizeof(struct queuepacket));
			p->len=h.caplen;
			memcpy(p->data,data,h.caplen);
			SDL_LockMutex(slirp_queue_mutex);
			QueueEnter(slirpq,p);
			SDL_UnlockMutex(slirp_queue_mutex);

			SetInterruptFlag(INTFLAG_ETHER);
			TriggerInterrupt();
			// Wait for interrupt acknowledge by EtherInterrupt()
			SDL_SemWait(int_ack);

			}
		}
	}
#if 1
#ifdef UNIX
	if(is_slirp)
	usleep(1);	//this value feels too magical.  1 for pcap
#else
	if(is_slirp)
	Sleep(1);
#endif
#endif
	WTF:
	#ifdef UNIX
	usleep(1);	//this value feels too magical.
	#else
	Sleep(1);
	#endif
    }//end for
}




/*
 *  Ethernet interrupt - activate deferred tasks to call IODone or protocol handlers
 */

void EtherInterrupt(void)
{
ssize_t length;
struct queuepacket *qp;
if((!is_slirp)&&(!is_pcap))
	return;
	D(bug("EtherIRQ\n"));

	// Call protocol handler for received packets
	uint8 packet[1516];
	while(QueuePeek(slirpq)>0)
		{
		SDL_LockMutex(slirp_queue_mutex);
		qp=QueueDelete(slirpq);
		SDL_UnlockMutex(slirp_queue_mutex);
		D(bug("inQ:%d  got a %dbyte packet @%d\n",QueuePeek(slirpq),qp->len,qp));

		//data=p->data;
		//length=p->len;

		if (qp->len < 14)
			break;

		//memset(packet,0x0,sizeof(packet));
		//memcpy(packet,data,length);
		memcpy(packet,qp->data,qp->len);

#if MONITOR
		bug("Receiving Ethernet packet:\n");
		for (int i=0; i<length; i++) {
			bug("%02x ", packet[i]);
		}
		bug("\n");
#endif

		// Pointer to packet data (Ethernet header)
		uint8 *p = packet;

		// Get packet type
		uint16 type = ntohs(*(uint16 *)(p + 12));

		// Look for protocol
		NetProtocol *prot = find_protocol(type);

		if (prot == NULL)
			continue;

		// No default handler
		if (prot->handler == 0)
			continue;

		// Copy header to RHA
		Host2Mac_memcpy(ether_data + ed_RHA, p, 14);
		D(bug(" header %08lx%04lx %08lx%04lx %04lx\n", ReadMacInt32(ether_data + ed_RHA), ReadMacInt16(ether_data + ed_RHA + 4), ReadMacInt32(ether_data + ed_RHA + 6), ReadMacInt16(ether_data + ed_RHA + 10), ReadMacInt16(ether_data + ed_RHA + 12)));

		// Call protocol handler
		M68kRegisters r;
		r.d[0] = type;									// Packet type
		r.d[1] = qp->len - 14;							// Remaining packet length (without header, for ReadPacket)
		r.a[0] = (uint32)p + 14;						// Pointer to packet (host address, for ReadPacket)
		r.a[3] = ether_data + ed_RHA + 14;				// Pointer behind header in RHA
		r.a[4] = ether_data + ed_ReadPacket;			// Pointer to ReadPacket/ReadRest routines
		D(bug(" calling protocol handler %08lx, type %08lx, length %08lx, data %08lx, rha %08lx, read_packet %08lx\n", prot->handler, r.d[0], r.d[1], r.a[0], r.a[3], r.a[4]));
		Execute68k(prot->handler, &r);
		free(qp);
	} 

	// Acknowledge interrupt to reception thread
	D(bug(" EtherIRQ done\n"));
	SDL_SemPost(int_ack);
}

//SLIRP STUFF
int slirp_can_output(void)
{
return slirp_inited;
}

void slirp_output (const unsigned char *pkt, int pkt_len)
{
struct queuepacket *p;
p=(struct queuepacket *)malloc(sizeof(struct queuepacket));
p->len=pkt_len;
memcpy(p->data,pkt,pkt_len);
SDL_LockMutex(slirp_queue_mutex);
QueueEnter(slirpq,p);
SDL_UnlockMutex(slirp_queue_mutex);
D(bug("slirp_output %d @%d\n",pkt_len,p));
}

// Instead of calling this and crashing some times
// or experencing jitter, this is called by the 
// 60Hz clock which seems to do the job.
void slirp_tic(void)
{
       int ret2,nfds;
        struct timeval tv;
        fd_set rfds, wfds, xfds;
        int timeout;
        nfds=-1;

      if(slirp_inited)
              {
        FD_ZERO(&rfds);
        FD_ZERO(&wfds);
        FD_ZERO(&xfds);
	//printf("f");
	SDL_LockMutex(slirp_select_fill_mutex);
       timeout=slirp_select_fill(&nfds,&rfds,&wfds,&xfds); //this can crash
	SDL_UnlockMutex(slirp_select_fill_mutex);
	//printf("F");

	if(timeout<0)
		timeout=500;
       tv.tv_sec=0;
       tv.tv_usec = timeout;    //basilisk default 10000

       ret2 = select(nfds + 1, &rfds, &wfds, &xfds, &tv);
        if(ret2>=0){
	//printf("p");
	SDL_LockMutex(slirp_select_poll_mutex);
       slirp_select_poll(&rfds, &wfds, &xfds);
	SDL_UnlockMutex(slirp_select_poll_mutex);
	//printf("P");
          }
       }//end if slirp inited
}
