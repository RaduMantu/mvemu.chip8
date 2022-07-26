#include <argp.h>
#include <stdint.h>

#ifndef _CLI_ARGS_H
#define _CLI_ARGS_H

struct user_settings {
    char     *rom_path;        /* location of ROM file                        */
    int32_t  audio_idx;        /* audio device index                          */
    float    tone_freq;        /* buzzer tone frequency                       */
    uint16_t rom_off;          /* RAM offset at which the ROM is loaded       */
    uint16_t font_off;         /* RAM offset at which font sprites are loaded */
    uint16_t scale_f;          /* window scale factor                         */
    uint16_t frequency;        /* CPU frequency                               */
    uint16_t ref_int;          /* screen refresh interval                     */
    uint8_t  new_shift : 1;    /* use new implementation of shift operations  */
    uint8_t  lazy_render : 1;  /* refresh screen only on DXYN (not regularly) */
};

extern struct argp          argp;
extern struct user_settings settings;

#endif /* _CLI_ARGS_H */

