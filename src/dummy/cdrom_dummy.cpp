#include "sysdeps.h"

#define DEBUG 0
#include "debug.h"

//CDROM SHIT
void SysAllowRemoval(void *arg)
{
}

bool SysCDReadTOC(void *arg, uint8 *toc)
{
return FALSE;
}

bool SysCDGetPosition(void *arg, uint8 *pos)
{
return FALSE;
}

bool SysCDPlay(void *arg, uint8 start_m, uint8 start_s, uint8 start_f, uint8 end_m, uint8 end_s, uint8 end_f)
{
return FALSE;
}

bool SysCDPause(void *arg)
{
	return FALSE;
}

bool SysCDResume(void *arg)
{
	return FALSE;
}

bool SysCDStop(void *arg, uint8 lead_out_m, uint8 lead_out_s, uint8 lead_out_f)
{
	return FALSE;
}

bool SysCDScan(void *arg, uint8 start_m, uint8 start_s, uint8 start_f, bool reverse)
{
	return FALSE;
}

void SysCDSetVolume(void *arg, uint8 left, uint8 right)
{
	
}

void SysCDGetVolume(void *arg, uint8 &left, uint8 &right)
{
		left = right = 0;
}

void SysAddCDROMPrefs(void)
{}

void SysPreventRemoval(void *arg)
{
}
