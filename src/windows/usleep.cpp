#include <windows.h>
#include <stdio.h>

#include <windows.h>
#define DEBUG 0
#include "debug.h"

/*
void usleep(long usec)//__int64 usec) 
{ 
    HANDLE timer; 
    LARGE_INTEGER ft; 

    ft.QuadPart = -(10*usec); // Convert to 100 nanosecond interval, negative value indicates relative time

    timer = CreateWaitableTimer(NULL, TRUE, NULL); 
    SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0); 
    WaitForSingleObject(timer, INFINITE); 
    CloseHandle(timer); 
}*/

void usleep(long waitTime) {
	D(bug("usleep %d or %d\n",waitTime,waitTime/10000));
	int time=0;
	time=waitTime/10000;
	Sleep(time);
	/*
    __int64 time1 = 0, time2 = 0, freq = 0;

    QueryPerformanceCounter((LARGE_INTEGER *) &time1);
    QueryPerformanceFrequency((LARGE_INTEGER *)&freq);

    do {
        QueryPerformanceCounter((LARGE_INTEGER *) &time2);
		Sleep(5);
    } while((time2-time1) < waitTime);*/
}