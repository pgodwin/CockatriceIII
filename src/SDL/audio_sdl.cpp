/*
 *  audio_dummy.cpp - Audio support, dummy implementation
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

/*#include "sysdeps.h"
#include "prefs.h"
#include "audio.h"
#include "audio_defs.h"*/

#include <SDL/SDL_audio.h>
#include <SDL/SDL_thread.h>
#include <SDL/SDL_byteorder.h>

#include "sysdeps.h"
#include "cpu_emulation.h"
#include "main.h"
#include "prefs.h"
#include "user_strings.h"
#include "audio.h"
#include "audio_defs.h"



#define DEBUG 0
#include "debug.h"

// The currently selected audio parameters (indices in audio_sample_rates[] etc. vectors)
static int audio_sample_rate_index = 0;
static int audio_sample_size_index = 0;
static int audio_channel_count_index = 0;

static uint8 silence_byte;                                                      // Byte value to use to fill sound buffers with silence
static SDL_sem *audio_irq_done_sem = NULL;			// Signal from interrupt to streaming thread: data block read

//Prototypes
static void close_audio(void);
static void stream_func(void *arg, uint8 *stream, int stream_len);

// Supported sample rates, sizes and channels
int audio_num_sample_rates = 1;
uint32 audio_sample_rates[] = {44100 << 16};
int audio_num_sample_sizes = 1;
uint16 audio_sample_sizes[] = {16};
int audio_num_channel_counts = 1;
uint16 audio_channel_counts[] = {2};


/*
 *  Initialization
 */

// Set AudioStatus to reflect current audio stream format
static void set_audio_status_format(void)
{
        AudioStatus.sample_rate = audio_sample_rates[audio_sample_rate_index];
        AudioStatus.sample_size = audio_sample_sizes[audio_sample_size_index];
        AudioStatus.channels = audio_channel_counts[audio_channel_count_index];
}

static bool open_sdl_audio(void)
{
        SDL_AudioSpec desired, obtained;
        int desired_bits=16;

        /* Set up the desired format */
        desired.freq = 44100;   //11025? 22050?
        switch (desired_bits) {
                case 8:
                        desired.format = AUDIO_U8;
                        break;
                case 16:
                        if ( SDL_BYTEORDER == SDL_BIG_ENDIAN )
                                desired.format = AUDIO_S16MSB;
                        else
                                desired.format = AUDIO_S16LSB;
                        break;
                default:
                        printf("not 8 or 16 sound!?\n");
                        return false;
        }
        desired.channels = 2;
        desired.samples = 4096;
        desired.callback = stream_func;

        /* Open the audio device */
        if ( SDL_OpenAudio(&desired, &obtained) < 0 ) {
                printf("Couldn't open SDL audio: %s\n", SDL_GetError());
                return false;
        }
        switch (obtained.format) {
                case AUDIO_U8:
                        /* Supported */
                        break;
                case AUDIO_S16LSB:
                case AUDIO_S16MSB:
                        if ( ((obtained.format == AUDIO_S16LSB) &&
                             (SDL_BYTEORDER == SDL_LIL_ENDIAN)) ||
                             ((obtained.format == AUDIO_S16MSB) &&
                             (SDL_BYTEORDER == SDL_BIG_ENDIAN)) ) {
                                /* Supported */
                                break;
                        }
                        /* Unsupported, fall through */;
                default:
                        /* Not supported -- force SDL to do our bidding */
                        SDL_CloseAudio();
                        if ( SDL_OpenAudio(&desired, NULL) < 0 ) {
                                printf("Couldn't open SDL audio: %s\n",
                                                        SDL_GetError());
                                return false;
                        }
                        memcpy(&obtained, &desired, sizeof(desired));
                        break;
        }
        SDL_PauseAudio(0);
	D(bug("SDL_Audio inited!\n"));

        D(bug("sample bits %d\n",obtained.format&0xff));
        D(bug("speed/freq %d\n",obtained.freq));
        D(bug("channels %d\n",obtained.channels));
        D(bug("samples %d\n",obtained.samples));
	printf("SDL_Audio inited %dbit, %dHz, %d channels\n",obtained.format&0xff,obtained.freq,obtained.channels);
        // Init audio status and feature flags
        AudioStatus.sample_rate = audio_sample_rates[0];
        AudioStatus.sample_size = audio_sample_sizes[0];
        AudioStatus.channels = audio_channel_counts[0];
        AudioStatus.mixer = 0;
        AudioStatus.num_sources = 0;

        silence_byte = obtained.silence;

        audio_component_flags = cmpWantsRegisterMessage | kStereoOut | k16BitOut;



        // Sound buffer size = 4096 frames
        audio_frames_per_block = desired.samples;
        return true;
}



static bool open_audio(void)
{
        // Try to open SDL audio
        if (!open_sdl_audio()) {
                //WarningAlert(GetString(STR_NO_AUDIO_WARN));
		printf("audio error!\n");
                return false;
        }

        // Device opened, set AudioStatus
        set_audio_status_format();

        // Everything went fine
        audio_open = true;
        return true;
}

void AudioInit(void)
{
        // Init audio status and feature flags
        AudioStatus.sample_rate = 44100 << 16;
        AudioStatus.sample_size = 16;
        AudioStatus.channels = 2;
        AudioStatus.mixer = 0;
        AudioStatus.num_sources = 0;
        audio_component_flags = cmpWantsRegisterMessage | kStereoOut | k16BitOut;

        // Sound disabled in prefs? Then do nothing
        if (PrefsFindBool("nosound"))
                return;

        // Init semaphore
        audio_irq_done_sem = SDL_CreateSemaphore(0);

        // Open and initialize audio device
        open_audio();
}



/*
 *  Deinitialization
 */

void AudioExit(void)
{
}


/*
 *  First source added, start audio stream
 */

void audio_enter_stream()
{
//printf("enter stream\n");
}


/*
 *  Last source removed, stop audio stream
 */

void audio_exit_stream()
{
}


/*
 *  MacOS audio interrupt, read next data block
 */

void AudioInterrupt(void)
{
        D(bug("AudioInterrupt\n"));

        // Get data from apple mixer
        if (AudioStatus.mixer) {
                M68kRegisters r;
                r.a[0] = audio_data + adatStreamInfo;
                r.a[1] = AudioStatus.mixer;
                Execute68k(audio_data + adatGetSourceData, &r);
                D(bug(" GetSourceData() returns %08lx\n", r.d[0]));
        } else
                WriteMacInt32(audio_data + adatStreamInfo, 0);

        // Signal stream function
        SDL_SemPost(audio_irq_done_sem);
        D(bug("AudioInterrupt done\n"));

}


/*
 *  Set sampling parameters
 *  "index" is an index into the audio_sample_rates[] etc. arrays
 *  It is guaranteed that AudioStatus.num_sources == 0
 */

void audio_set_sample_rate(int index)
{
	printf("change sample rate to %d, was %d\n",audio_sample_rate_index ,index);
        close_audio();
        audio_sample_rate_index = index;
        //return open_audio();
        open_audio();
}

void audio_set_sample_size(int index)
{
	printf("change sample size from %d to %d\n",audio_sample_size_index ,index);
        close_audio();
        audio_sample_size_index = index;
    //    return open_audio();
    	open_audio();
}

void audio_set_channels(int index)
{
        close_audio();
	printf("change channels from %d to %d\n",audio_channel_count_index  ,index);
        audio_channel_count_index = index;
   //     return open_audio();
        open_audio();
}


/*
 *  Get/set volume controls (volume values received/returned have the left channel
 *  volume in the upper 16 bits and the right channel volume in the lower 16 bits;
 *  both volumes are 8.8 fixed point values with 0x0100 meaning "maximum volume"))
 */

bool audio_get_main_mute(void)
{
	return false;
}

uint32 audio_get_main_volume(void)
{
	return 0x01000100;
}

bool audio_get_speaker_mute(void)
{
	return false;
}

uint32 audio_get_speaker_volume(void)
{
	return 0x01000100;
}

void audio_set_main_mute(bool mute)
{
}

void audio_set_main_volume(uint32 vol)
{
}

void audio_set_speaker_mute(bool mute)
{
}

void audio_set_speaker_volume(uint32 vol)
{
}


static void stream_func(void *arg, uint8 *stream, int stream_len)
{
if(AudioStatus.num_sources) {
	        // Trigger audio interrupt to get new buffer
                D(bug("stream: triggering irq\n"));
                SetInterruptFlag(INTFLAG_AUDIO);
                TriggerInterrupt();
                D(bug("stream: waiting for ack\n"));
                SDL_SemWait(audio_irq_done_sem);
                D(bug("stream: ack received\n"));

                // Get size of audio data
                uint32 apple_stream_info = ReadMacInt32(audio_data + adatStreamInfo);
                if (apple_stream_info) {
                        int work_size = ReadMacInt32(apple_stream_info + scd_sampleCount) * (AudioStatus.sample_size >> 3) * AudioStatus.channels;
                        D(bug("stream: work_size %d\n", work_size));
                        if (work_size > stream_len)
                                work_size = stream_len;
                        if (work_size == 0)
                                goto silence;
                        // Send data to audio device
                        Mac2Host_memcpy(stream, ReadMacInt32(apple_stream_info + scd_buffer), work_size);
                        if (work_size != stream_len)
                                memset((uint8 *)stream + work_size, silence_byte, stream_len - work_size);
                        D(bug("stream: data written\n"));
                } else
                        goto silence;

        } else {

                // Audio not active, play silence
silence: memset(stream, silence_byte, stream_len);
	}
//  }//end num sources
}

static void close_audio(void)
{
        // Close audio device
        SDL_CloseAudio();
        audio_open = false;
}
