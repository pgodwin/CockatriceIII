#include "sysdeps.h"
#include <SDL\SDL.h>

#include "cpu_emulation.h"
#include "main.h"
#include "adb.h"
#include "macos_util.h"
#include "prefs.h"
#include "user_strings.h"
#include "video.h"

static bool classic_mode = false;
static uint8 *the_buffer;							// Mac frame buffer

int depth;
int display_type=0;
#define DISPLAY_WINDOW 1
static SDL_Surface *screen = NULL;

/***** prototypes***/
void set_video_monitor(int width, int height, int bytes_per_row);
void xvideo_set_palette(void);

void VideoInterrupt(void)
{/*
	SDL_Event event;
    while(SDL_PollEvent(&event))
    {
        switch (event.type) {
	case SDL_KEYDOWN:
	case SDL_KEYUP:
		break;
	case SDL_QUIT:
		QuitEmulator();
	default:
		break;
		}
	}
//	memcpy(screen->pixels,the_buffer,320*200);
//    SDL_UpdateRect(screen, 0, 0, 0, 0);*/
}

bool VideoInit(bool classic)
{
	unsigned int flags;
	if(classic)
		classic_mode=true;


	    


	// Get screen mode from preferences
	const char *mode_str;
	if (classic)
		mode_str = "win/512/342";
	else
		mode_str = PrefsFindString("screen");

	// Determine type and mode
	int width = 512, height = 384;

	if (mode_str) {
		if (sscanf(mode_str, "win/%d/%d", &width, &height) == 2)
			display_type = DISPLAY_WINDOW;
	}

	//set_video_monitor(width, height, img->bytes_per_line, img->bitmap_bit_order == LSBFirst);
	set_video_monitor(width, height, width);

	#if !REAL_ADDRESSING
	// Set variables for UAE memory mapping
	the_buffer = (uint8 *)malloc(1024*1024*2);	//just grab some ram
	MacFrameBaseHost = the_buffer;
	MacFrameSize = VideoMonitor.bytes_per_row * VideoMonitor.y;
	#endif

#if 0
	flags = (SDL_SWSURFACE|SDL_HWPALETTE);

	if (!(screen = SDL_SetVideoMode(width, height, 8, flags)))
        printf("VID: Couldn't set video mode: %s\n", SDL_GetError());
	SDL_WM_SetCaption("Basilisk II","Basilisk II");
#endif
	//xvideo_set_palette();
	// No special frame buffer in Classic mode (frame buffer is in Mac RAM)
	if (classic)
		MacFrameLayout = FLAYOUT_NONE;

	

return true;
}


void VideoExit(void)
{
}	
void video_set_palette(unsigned char *){}
void xvideo_set_palette(void)
{
	int r, g, b;
	int ncolors;
	int i;
    SDL_Color *colors;


        /* Allocate 256 color palette */
        ncolors = 256;
        colors  = (SDL_Color *)malloc(ncolors*sizeof(SDL_Color));

        /* Set a 3,3,2 color cube */
        for ( r=0; r<8; ++r ) {
            for ( g=0; g<8; ++g ) {
                for ( b=0; b<4; ++b ) {
                    i = ((r<<5)|(g<<2)|b);
                    colors[i].r = r<<5;
                    colors[i].g = g<<5;
                    colors[i].b = b<<6;
                }
            }
        }
        /* Note: A better way of allocating the palette might be
           to calculate the frequency of colors in the image
           and create a palette based on that information.*/
        
    /* Set colormap, try for all the colors, but don't worry about it */
    SDL_SetColors(screen, colors, 0, ncolors);
}




// Set VideoMonitor according to video mode
void set_video_monitor(int width, int height, int bytes_per_row)
{
	int layout = FLAYOUT_DIRECT;
	switch (depth) {
		case 1:
			layout = FLAYOUT_DIRECT;
			VideoMonitor.mode = VMODE_1BIT;
			break;
		case 8:
			layout = FLAYOUT_DIRECT;
			VideoMonitor.mode = VMODE_8BIT;
			break;
		case 15:
			layout = FLAYOUT_HOST_555;
			VideoMonitor.mode = VMODE_16BIT;
			break;
		case 16:
			layout = FLAYOUT_HOST_565;
			VideoMonitor.mode = VMODE_16BIT;
			break;
		case 24:
		case 32:
			layout = FLAYOUT_HOST_888;
			VideoMonitor.mode = VMODE_32BIT;
			break;
	}
	VideoMonitor.x = width;
	VideoMonitor.y = height;
	VideoMonitor.bytes_per_row = bytes_per_row;
	//if (native_byte_order)
	//	MacFrameLayout = layout;
	//else
		MacFrameLayout = FLAYOUT_DIRECT;
}
