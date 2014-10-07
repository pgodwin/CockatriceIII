#include "sysdeps.h"

//#include <pthread.h>
#include <errno.h>


#include "cpu_emulation.h"
#include "main.h"
#include "adb.h"
#include "macos_util.h"
#include "prefs.h"
#include "user_strings.h"
#include "video.h"

#define DEBUG 0
#include "debug.h"

#include <SDL/SDL.h>
static SDL_Surface *SDLscreen = NULL;
static bool use_keycodes = false;	// Flag: Use keycodes rather than keysyms
static int keycode_table[256];		// X keycode -> Mac keycode translation table

// Global variables
static int32 frame_skip;
static int32 quitcount=0;
int depth;	//how deep is the display
// Prefs items

static int16 mouse_wheel_mode = 1;
static int16 mouse_wheel_lines = 3;


static bool ctrl_down = false;						// Flag: Ctrl key pressed
static bool caps_on = false;						// Flag: Caps Lock on
static bool quit_full_screen = false;				// Flag: DGA close requested from redraw thread
static bool emerg_quit = false;						// Flag: Ctrl-Esc pressed, emergency quit requested from MacOS thread
static bool emul_suspended = false;					// Flag: Emulator suspended

static uint8 *the_buffer;                                                       // Mac frame buffer
static bool redraw_thread_active = false;                       // Flag: Redraw thread installed
static volatile bool redraw_thread_cancel = false;      // Flag: Cancel Redraw thread
//static pthread_t redraw_thread;                                         // Redraw thread
//prototypes

static bool init_window(int width, int height);
void set_video_monitor(int width, int height, int bytes_per_row);
static bool is_modifier_key(SDL_KeyboardEvent const & e);
static int event2keycode(SDL_KeyboardEvent const &ev, bool key_down);
void doevents(void);
static int kc_decode(SDL_keysym const & ks, bool key_down);
static bool is_ctrl_down(SDL_keysym const & ks);





////////////////////////////////////////

void video_set_palette(uint8 *pal)
{
        int r, g, b;
	int ncolors;
	int i;
    SDL_Color *colors;


        /* Allocate 256 color palette */
        ncolors = 256;
        colors  = (SDL_Color *)malloc(ncolors*sizeof(SDL_Color));

        /* Set a 3,3,2 color cube */
/*
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
*/
        for (int i=0; i<256; i++) {
                colors[i].r = pal[i*3] * 0x0101;
                colors[i].g = pal[i*3+1] * 0x0101;
                colors[i].b = pal[i*3+2] * 0x0101;
        }

        /* Note: A better way of allocating the palette might be
           to calculate the frequency of colors in the image
           and create a palette based on that information.
        */
    /* Set colormap, try for all the colors, but don't worry about it */
    SDL_SetColors(SDLscreen, colors, 0, ncolors);
}

bool VideoInit(bool classic)
{
const char *mode_str;
	if (classic)
                mode_str = "win/512/342";
        else
                mode_str = PrefsFindString("screen");


D(bug(" VideoInit %d\n",classic));
if (classic)
	depth =1;
else depth=8;
        int width = 512, height = 384;
        //display_type = DISPLAY_WINDOW;
        if (mode_str) {
                if (sscanf(mode_str, "win/%d/%d", &width, &height) == 2)
                        //display_type = DISPLAY_WINDOW;
	sscanf(mode_str,"win/%d/%d",&width,&height);
	}
	if(!init_window(width,height))
		return false;
#if !REAL_ADDRESSING
        // Set variables for UAE memory mapping
        MacFrameBaseHost = the_buffer;
        MacFrameSize = VideoMonitor.bytes_per_row * VideoMonitor.y;

        // No special frame buffer in Classic mode (frame buffer is in Mac RAM)
        if (classic)
                MacFrameLayout = FLAYOUT_NONE;
#endif

/*        redraw_thread_active = (pthread_create(&redraw_thread, NULL, redraw_func, NULL) == 0);
        if (!redraw_thread_active)
                printf("FATAL: cannot create redraw thread\n");
        return redraw_thread_active;
*/
return true;
}

// Init window mode
static bool init_window(int width, int height)
{
D(bug(" init_window w%d,h%d d%d\n",width,height,depth));
        // Set absolute mouse mode
        ADBSetRelMouseMode(false);

        // Read frame skip prefs
        frame_skip = PrefsFindInt32("frameskip");
        if (frame_skip == 0)
                frame_skip = 1;
//SDL
        int flags;
        if(SDL_Init(SDL_INIT_VIDEO)<0)
                exit(0);
        flags=(SDL_SWSURFACE|SDL_HWPALETTE);
        if (!(SDLscreen = SDL_SetVideoMode(width, height, 8, flags)))
        printf("VID: Couldn't set video mode: %s\n", SDL_GetError());
        SDL_WM_SetCaption("Basilisk II","Basilisk II");
//SDL

                int bytes_per_row = width;
                switch (depth) {
                        case 1:
                                bytes_per_row /= 8;
                                break;
                        case 15:
                        case 16:
                                bytes_per_row *= 2;
                                break;
                        case 24:
                        case 32:
                                bytes_per_row *= 4;
                                break;
                }
		D(bug(" bytes per row %d\n",bytes_per_row));
        the_buffer = (uint8 *)malloc((height + 2) * bytes_per_row);
        //the_buffer_copy = (uint8 *)malloc((height + 2) * img->bytes_per_line);

set_video_monitor(width, height, bytes_per_row);//img->bytes_per_line);

#if REAL_ADDRESSING
        VideoMonitor.mac_frame_base = (uint32)the_buffer;
        MacFrameLayout = FLAYOUT_DIRECT;
#else
        VideoMonitor.mac_frame_base = MacFrameBaseMac;
#endif
        return true;
}


// Set VideoMonitor according to video mode
void set_video_monitor(int width, int height, int bytes_per_row)
{
        int layout = FLAYOUT_DIRECT;
	D(bug("set_video_monitor %d %d %d\n",width,height,bytes_per_row));
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
        //        MacFrameLayout = layout;
        //else
                MacFrameLayout = FLAYOUT_DIRECT;

}


void VideoExit(void)
{}

void VideoInterrupt(void)
{
memcpy(SDLscreen->pixels,the_buffer,VideoMonitor.bytes_per_row*VideoMonitor.y);
SDL_UpdateRect(SDLscreen,0,0,0,0);
doevents();
}

void doevents(void)
{
 SDL_Event event;
	int mb,x,y;
int emul_suspended=0;
    while(SDL_PollEvent(&event))
    {
        switch (event.type) {
	case SDL_KEYDOWN: {
			int code = -1;
			if (use_keycodes && !is_modifier_key(event.key)) {
				if (event2keycode(event.key, true) != -2)	// This is called to process the hotkeys
					code = keycode_table[event.key.keysym.scancode & 0xff];
			} else
				code = event2keycode(event.key, true);
			if (code >= 0) {
				if (!emul_suspended) {
					if (code == 0x39) {	// Caps Lock pressed
						if (caps_on) {
							ADBKeyUp(code);
							caps_on = false;
						} else {
							ADBKeyDown(code);
							caps_on = true;
						}
					} else
						ADBKeyDown(code);
					if (code == 0x36)
						ctrl_down = true;
				} else {
				//	if (code == 0x31)
				//		drv->resume();	// Space wakes us up
				}
			}
			break;
		}
	case SDL_KEYUP: {
				int code = -1;
				if (use_keycodes && !is_modifier_key(event.key)) {
					if (event2keycode(event.key, false) != -2)	// This is called to process the hotkeys
						code = keycode_table[event.key.keysym.scancode & 0xff];
				} else
					code = event2keycode(event.key, false);
				if (code >= 0) {
					if (code == 0x39) {	// Caps Lock released
						if (caps_on) {
							ADBKeyUp(code);
							caps_on = false;
						} else {
							ADBKeyDown(code);
							caps_on = true;
						}
					} else
						ADBKeyUp(code);
					if (code == 0x36)
						ctrl_down = false;
				}
				break;
			}
	break;
	// Mouse button
	case SDL_MOUSEBUTTONDOWN: {
			unsigned int button = event.button.button;
			if (button < 4)
				ADBMouseDown(button - 1);
			else if (button < 6) {	// Wheel mouse
				if (mouse_wheel_mode == 0) {
					int key = (button == 5) ? 0x79 : 0x74;	// Page up/down
					ADBKeyDown(key);
					ADBKeyUp(key);
				} else {
					int key = (button == 5) ? 0x3d : 0x3e;	// Cursor up/down
					for(int i=0; i<mouse_wheel_lines; i++) {
						ADBKeyDown(key);
						ADBKeyUp(key);
					}
				}
			}
			break;
		}
		case SDL_MOUSEBUTTONUP: {
			unsigned int button = event.button.button;
			if (button < 4)
				ADBMouseUp(button - 1);
			break;
		}
	case SDL_MOUSEMOTION:
	ADBMouseMoved(event.motion.x, event.motion.y);
	break;	

	case SDL_QUIT:
		quitcount++;
		ADBKeyDown(0x7f);	// Power key
		ADBKeyUp(0x7f);
		if(quitcount>5)
			exit(0);	//this should be the nice shutdown
	break;

	default:
	break;
	}//end switch
   }//end while
}


static bool is_modifier_key(SDL_KeyboardEvent const & e)
{
	switch (e.keysym.sym) {
	case SDLK_NUMLOCK:
	case SDLK_CAPSLOCK:
	case SDLK_SCROLLOCK:
	case SDLK_RSHIFT:
	case SDLK_LSHIFT:
	case SDLK_RCTRL:
	case SDLK_LCTRL:
	case SDLK_RALT:
	case SDLK_LALT:
	case SDLK_RMETA:
	case SDLK_LMETA:
	case SDLK_LSUPER:
	case SDLK_RSUPER:
	case SDLK_MODE:
	case SDLK_COMPOSE:
		return true;
	}
	return false;
}


static int event2keycode(SDL_KeyboardEvent const &ev, bool key_down)
{
	return kc_decode(ev.keysym, key_down);
}

/*
 *  Translate key event to Mac keycode, returns -1 if no keycode was found
 *  and -2 if the key was recognized as a hotkey
 */

static int kc_decode(SDL_keysym const & ks, bool key_down)
{
	switch (ks.sym) {
	case SDLK_a: return 0x00;
	case SDLK_b: return 0x0b;
	case SDLK_c: return 0x08;
	case SDLK_d: return 0x02;
	case SDLK_e: return 0x0e;
	case SDLK_f: return 0x03;
	case SDLK_g: return 0x05;
	case SDLK_h: return 0x04;
	case SDLK_i: return 0x22;
	case SDLK_j: return 0x26;
	case SDLK_k: return 0x28;
	case SDLK_l: return 0x25;
	case SDLK_m: return 0x2e;
	case SDLK_n: return 0x2d;
	case SDLK_o: return 0x1f;
	case SDLK_p: return 0x23;
	case SDLK_q: return 0x0c;
	case SDLK_r: return 0x0f;
	case SDLK_s: return 0x01;
	case SDLK_t: return 0x11;
	case SDLK_u: return 0x20;
	case SDLK_v: return 0x09;
	case SDLK_w: return 0x0d;
	case SDLK_x: return 0x07;
	case SDLK_y: return 0x10;
	case SDLK_z: return 0x06;

//	case SDLK_1: case SDLK_EXCLAIM: return 0x12;
//	case SDLK_2: case SDLK_AT: return 0x13;
//	case SDLK_3: case SDLK_numbersign: return 0x14;
//	case SDLK_3: case '#': return 0x14;
//	case SDLK_4: case SDLK_DOLLAR: return 0x15;
//	case SDLK_4: case '$': return 0x15;
//	case SDLK_5: case SDLK_percent: return 0x17;
	case SDLK_1: return 0x12;
	case SDLK_2: return 0x13;
	case SDLK_3: return 0x14;
	case SDLK_4: return 0x15;
	case SDLK_5: return 0x17;
	case SDLK_6: return 0x16;
	case SDLK_7: return 0x1a;
	case SDLK_8: return 0x1c;
	case SDLK_9: return 0x19;
	case SDLK_0: return 0x1d;

//	case SDLK_BACKQUOTE: case SDLK_asciitilde: return 0x0a;
	case SDLK_BACKQUOTE: return 0x32;
	case SDLK_BACKSLASH: return 0x0a;
	case SDLK_MINUS: case SDLK_UNDERSCORE: return 0x1b;
	case SDLK_EQUALS: case SDLK_PLUS: return 0x18;
//	case SDLK_bracketleft: case SDLK_braceleft: return 0x21;
	case SDLK_LEFTBRACKET: return 0x21;
//	case SDLK_bracketright: case SDLK_braceright: return 0x1e;
	case SDLK_RIGHTBRACKET: return 0x1e;
//	case SDLK_BACKSLASH: case SDLK_bar: return 0x2a;
	case SDLK_SEMICOLON: case SDLK_COLON: return 0x29;
//	case SDLK_apostrophe: case SDLK_QUOTEDBL: return 0x27;
	case SDLK_COMMA: case SDLK_LESS: return 0x2b;
	case SDLK_PERIOD: case SDLK_GREATER: return 0x2f;
	case SDLK_SLASH: case SDLK_QUESTION: return 0x2c;

	case SDLK_TAB: /*if (is_ctrl_down(ks)) {
					if (!key_down) 
						drv->suspend(); 	
					return -2;} 
					else */return 0x30;
	case SDLK_RETURN: return 0x24;
	case SDLK_SPACE: return 0x31;
	case SDLK_BACKSPACE: return 0x33;

	case SDLK_DELETE: return 0x75;
	case SDLK_INSERT: return 0x72;
	case SDLK_HOME: case SDLK_HELP: return 0x73;
	case SDLK_END: return 0x77;
	case SDLK_PAGEUP: return 0x74;
	case SDLK_PAGEDOWN: return 0x79;

	case SDLK_LCTRL: return 0x36;
	case SDLK_RCTRL: return 0x36;
	case SDLK_LSHIFT: return 0x38;
	case SDLK_RSHIFT: return 0x38;
#if (defined(__APPLE__) && defined(__MACH__))
	case SDLK_LALT: return 0x3a;
	case SDLK_RALT: return 0x3a;
	case SDLK_LMETA: return 0x37;
	case SDLK_RMETA: return 0x37;
#else
	case SDLK_LALT: return 0x37;
	case SDLK_RALT: return 0x37;
	case SDLK_LMETA: return 0x3a;
	case SDLK_RMETA: return 0x3a;
#endif
	case SDLK_MENU: return 0x32;
	case SDLK_CAPSLOCK: return 0x39;
	case SDLK_NUMLOCK: return 0x47;

	case SDLK_UP: return 0x3e;
	case SDLK_DOWN: return 0x3d;
	case SDLK_LEFT: return 0x3b;
	case SDLK_RIGHT: return 0x3c;

	case SDLK_ESCAPE: if (is_ctrl_down(ks)) {if (!key_down) { quit_full_screen = true; emerg_quit = true; } return -2;} else return 0x35;

	//case SDLK_F1: if (is_ctrl_down(ks)) {if (!key_down) SysMountFirstFloppy(); return -2;} else return 0x7a;
	case SDLK_F1: return 0x7a;
	case SDLK_F2: return 0x78;
	case SDLK_F3: return 0x63;
	case SDLK_F4: return 0x76;
	//case SDLK_F5: if (is_ctrl_down(ks)) {if (!key_down) drv->toggle_mouse_grab(); return -2;} else return 0x60;
	case SDLK_F5: return 0x60;
	case SDLK_F6: return 0x61;
	case SDLK_F7: return 0x62;
	case SDLK_F8: return 0x64;
	case SDLK_F9: return 0x65;
	case SDLK_F10: return 0x6d;
	case SDLK_F11: return 0x67;
	case SDLK_F12: return 0x6f;

	case SDLK_PRINT: return 0x69;
	case SDLK_SCROLLOCK: return 0x6b;
	case SDLK_PAUSE: return 0x71;

	case SDLK_KP0: return 0x52;
	case SDLK_KP1: return 0x53;
	case SDLK_KP2: return 0x54;
	case SDLK_KP3: return 0x55;
	case SDLK_KP4: return 0x56;
	case SDLK_KP5: return 0x57;
	case SDLK_KP6: return 0x58;
	case SDLK_KP7: return 0x59;
	case SDLK_KP8: return 0x5b;
	case SDLK_KP9: return 0x5c;
	case SDLK_KP_PERIOD: return 0x41;
	case SDLK_KP_PLUS: return 0x45;
	case SDLK_KP_MINUS: return 0x4e;
	case SDLK_KP_MULTIPLY: return 0x43;
	case SDLK_KP_DIVIDE: return 0x4b;
	case SDLK_KP_ENTER: return 0x4c;
	case SDLK_KP_EQUALS: return 0x51;
	}
	D(bug("Unhandled SDL keysym: %d\n", ks.sym));
	return -1;
}

static bool is_ctrl_down(SDL_keysym const & ks)
{
	return ctrl_down || (ks.mod & KMOD_CTRL);
}

