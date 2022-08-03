#include <stdint.h>

#ifndef _SOUND_H
#define _SOUND_H

#define Fs  44100       /* audio sample rate [Hz] */

/* public API */
int32_t init_audio(int32_t, float);
int32_t terminate_audio(void);
int32_t list_audio_devs(void);
int32_t start_playback(void);
int32_t stop_playback(void);

#endif
