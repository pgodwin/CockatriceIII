#include "sysdeps.h"
#include <SDL\SDL.h>
#include <SDL\SDL_thread.h>

#include <stdio.h>
#include <stdlib.h>
#include <io.h>
#include <direct.h>		//for _chdir


#include "cpu_emulation.h"
#include "sys.h"
#include "rom_patches.h"
#include "xpram.h"
#include "timer.h"
#include "video.h"
#include "prefs.h"
#include "prefs_editor.h"
#include "macos_util.h"
#include "user_strings.h"
#include "version.h"
#include "main.h"

#define DEBUG 0
#include "debug.h"

const char ROM_FILE_NAME[] = "ROM";


// CPU and FPU type, addressing mode
int CPUType;
bool CPUIs68060;
int FPUType;
bool TwentyFourBitAddressing;

//Threads...
SDL_Thread *xpram_thread = NULL;
SDL_Thread *tick_func1Hz_thread = NULL;
SDL_Thread *tick_func60Hz_thread = NULL;
SDL_Thread *tick_funcxxx_thread = NULL;

static volatile bool tick_thread_cancel = false;	// Flag: Cancel 60Hz thread
static volatile bool xpram_thread_cancel = false;
/*
 *  Interrupt flags (must be handled atomically!)
 */
uint32 InterruptFlags = 0;

// Prototypes
int xpram_func(void *arg);
int tick_func1Hz(void *arg);
int tick_func60Hz(void *arg);
int tick_funcxxx(void *arg);		//from unix
static void one_tickbbb(...);	//from unix
void fixstdio(void);
//else where
void usleep(long waitTime);
extern void slirp_tic(void);	//to keep slirp happy


int SDL_main(int argc, char *argv[])
{

	//	_chdir("c:\\test\\");
	// Initialize variables
	RAMBaseHost = NULL;
	ROMBaseHost = NULL;
	srand(time(NULL));
	tzset();

	fixstdio();

	// Print some info
	printf(GetString(STR_ABOUT_TEXT1), VERSION_MAJOR, VERSION_MINOR);
	printf(" %s\n", GetString(STR_ABOUT_TEXT2));

	// Parse arguments
	for (int i=1; i<argc; i++) {
		if (strcmp(argv[i], "-break") == 0 && ++i < argc)
			ROMBreakpoint = strtol(argv[i], NULL, 0);
		else if (strcmp(argv[i], "-rominfo") == 0)
			PrintROMInfo = true;
	}

	if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_CDROM) < 0)
	printf("VID: Couldn't load SDL: %s", SDL_GetError());

	
	// Read preferences
	PrefsInit();

	// Init system routines
	SysInit();

		// Show preferences editor
	if (!PrefsFindBool("nogui"))
		if (!PrefsEditor())
			QuitEmulator();

	// Read RAM size
	RAMSize = PrefsFindInt32("ramsize") & 0xfff00000;	// Round down to 1MB boundary
	if (RAMSize < 1024*1024) {
		WarningAlert(GetString(STR_SMALL_RAM_WARN));
		RAMSize = 1024*1024;
	}

	// Create areas for Mac RAM and ROM
	RAMBaseHost = new uint8[RAMSize];
	ROMBaseHost = new uint8[0x100000];

	memset(ROMBaseHost,0xAA,0x100000);

	// Get rom file path from preferences
	const char *rom_path = PrefsFindString("rom");

	// Load Mac ROM
	int rom_fd = _open(rom_path ? rom_path : ROM_FILE_NAME, _O_RDONLY|_O_BINARY);
	//int rom_fd = _open("c:\\test\\ROM",_O_RDONLY|_O_BINARY );
	if (rom_fd < 0) {
		ErrorAlert(GetString(STR_NO_ROM_FILE_ERR));
		QuitEmulator();
	}

	printf(GetString(STR_READING_ROM_FILE));
	ROMSize = _lseek(rom_fd, 0L, SEEK_END);
	if (ROMSize != 64*1024 && ROMSize != 128*1024 && ROMSize != 256*1024 && ROMSize != 512*1024 && ROMSize != 1024*1024) {
		ErrorAlert(GetString(STR_ROM_SIZE_ERR));
		close(rom_fd);
		QuitEmulator();
	}
	int dontcare=_lseek(rom_fd, 0L, SEEK_SET);
	D(bug("don't care rewind %d\n",dontcare));
	int x= _read(rom_fd, ROMBaseHost, ROMSize);	//if (_read(rom_fd, &ROMBaseHost, ROMSize) != (ssize_t)ROMSize) {
	if(x!=ROMSize)
	{
		ErrorAlert(GetString(STR_ROM_FILE_READ_ERR));
		close(rom_fd);
		QuitEmulator();
	}

	// Initialize everything
	if (!InitAll())
		QuitEmulator();

	
	// Start XPRAM watchdog thread
	//right now dont need it
	xpram_thread=SDL_CreateThread(xpram_func,NULL);
	//tick_func1Hz_thread=SDL_CreateThread(tick_func1Hz,NULL);
	//tick_func60Hz_thread=SDL_CreateThread(tick_func60Hz,NULL);
	tick_funcxxx_thread=SDL_CreateThread(tick_funcxxx,NULL);
	
	// Start 68k and jump to ROM boot routine
	Start680x0();

	QuitEmulator();
	return 0;
}



void ClearInterruptFlag(uint32 flag)
{
//	pthread_mutex_lock(&intflag_lock);
	InterruptFlags &= ~flag;
//	pthread_mutex_unlock(&intflag_lock);
}

void SetInterruptFlag(uint32 flag)
{
//	pthread_mutex_lock(&intflag_lock);
	InterruptFlags |= flag;
//	pthread_mutex_unlock(&intflag_lock);
}


void ErrorAlert(const char *p)
{
	printf("%s",p);
}

#if EMULATED_68K
void FlushCodeCache(void *start, uint32 size)
{
}
#endif


/*
 *  Display warning alert
 */

void WarningAlert(const char *text)
{
#if ENABLE_GTK
	if (PrefsFindBool("nogui") || x_display == NULL) {
		printf(GetString(STR_SHELL_WARNING_PREFIX), text);
		return;
	}
	display_alert(STR_WARNING_ALERT_TITLE, STR_GUI_WARNING_PREFIX, STR_OK_BUTTON, text);
#else
	printf(GetString(STR_SHELL_WARNING_PREFIX), text);
#endif
}


/*
 *  Display choice alert
 */

bool ChoiceAlert(const char *text, const char *pos, const char *neg)
{
	printf(GetString(STR_SHELL_WARNING_PREFIX), text);
	return false;	//!!
}


void fixstdio(void)
{

}




/*
 *  60Hz thread (really 60.15Hz)
 */

static void one_tickbbbb(...)
{
	static int tick_counter = 0;

	// Pseudo Mac 1Hz interrupt, update local time
	if (++tick_counter > 60) {
		tick_counter = 0;
		WriteMacInt32(0x20c, TimerDateTime());
	}

	// Trigger 60Hz interrupt
	if (ROMVersion != ROM_VERSION_CLASSIC || HasMacStarted()) {
		SetInterruptFlag(INTFLAG_60HZ);
		TriggerInterrupt();
		slirp_tic();
	}
}

int tick_funcxxx(void *arg)
{
	while (!tick_thread_cancel) {

		// Wait
#ifdef HAVE_NANOSLEEP
		struct timespec req = {0, 16625000};
		nanosleep(&req, NULL);
#else
		//usleep(16625);
		Sleep(16);
#endif

		// Action
		one_tickbbbb();
	}
	return NULL;
}
/*
 *  1Hz thread
 */

int tick_func1Hz(void *arg)
{
	DWORD mac_boot_secs = 0;
	time_t t, bias_minutes;
  int32 should_have_ticks;
	TIME_ZONE_INFORMATION tz;

	time( &t );

	memset( &tz, 0, sizeof(tz) );
	if(GetTimeZoneInformation( &tz ) == TIME_ZONE_ID_DAYLIGHT) {
		bias_minutes = tz.Bias + tz.DaylightBias;
	} else {
		bias_minutes = tz.Bias;
	}
	t = t - 60*bias_minutes + TIME_OFFSET;

	mac_boot_secs = t - GetTickCount()/1000 ;
	WriteMacInt32(0x20c, t);

	should_have_ticks = GetTickCount() + 1000;

	// Pseudo Mac 1Hz interrupt, update local time
	while (!tick_thread_cancel) {
		Sleep( should_have_ticks - (int)GetTickCount() );
		WriteMacInt32(0x20c, mac_boot_secs + GetTickCount()/1000);
		// WriteMacInt32( 0x20c, ReadMacInt32(0x20c)+1 );
		//SetInterruptFlag(INTFLAG_1HZ);		is anyone listening?
		//TriggerInterrupt();

/*		if(m_idle_timeout && !m_sleep_enabled) {
			if(m_idle_counter == 0) {
				m_sleep_enabled = true;
			} else {
				m_idle_counter--;
			}
		}*/

		should_have_ticks += 1000;
	}


	return NULL;
}

/*
 *  60Hz thread
 */

// This rendition runs at an averege of 58.82 Hz
//unsigned int WINAPI tick_func_accurate(LPVOID param)
int tick_func60Hz(void *arg)
{
	//int32 should_have_ticks = GetTickCount() + 100;
	int32 should_have_ticks = GetTickCount() + 10;

	Sleep(200);

	while (!tick_thread_cancel) {

		int stime = should_have_ticks - (int)GetTickCount();
		if(stime > 0) Sleep( stime );
		//should_have_ticks += 17;
		should_have_ticks += 8;

		// Trigger 60Hz interrupt
		SetInterruptFlag(INTFLAG_60HZ);
		TriggerInterrupt();
	}
	slirp_tic();

return NULL;
}


/*
 *  XPRAM watchdog thread (saves XPRAM every minute)
 */

int xpram_func(void *arg)
{
	uint8 last_xpram[256];
	memcpy(last_xpram, XPRAM, 256);

	while (!xpram_thread_cancel) {
		for (int i=0; i<60 && !xpram_thread_cancel; i++) {
#ifdef HAVE_NANOSLEEP
			struct timespec req = {1, 0};
			nanosleep(&req, NULL);
#else
			usleep(1000000);
#endif
		}
		if (memcmp(last_xpram, XPRAM, 256)) {
			memcpy(last_xpram, XPRAM, 256);
			SaveXPRAM();
		}
	}
	return 0;
}