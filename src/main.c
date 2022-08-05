/*
 * Copyright © 2022, Radu-Alexandru Mantu <andru.mantu@gmail.com>
 *
 * This file is part of mvemu.chip8.
 *
 * mvemu.chip8 is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * mvemu.chip8 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with mvemu.chip8. If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdint.h>     /* [u]int*_t       */

#include "cli_args.h"
#include "system.h"
#include "display.h"
#include "sound.h"
#include "util.h"

int32_t main(int32_t argc, char *argv[])
{
    int32_t ans;        /* answer    */
    int32_t ret = -1;   /* exit code */

    /* parse command line arguments */
    argp_parse(&argp, argc, argv, 0, 0, &settings);
    DIE(!settings.rom_path,  "No ROM provided");
    DIE(!settings.scale_f,   "Scale factor 0 not allowed");
    DIE(!settings.frequency, "CPU frequency 0 not allowed");
    DIE(!settings.ref_int,   "Screen refresh interval 0 not allowed");
    GOTO(settings.audio_idx < 0, invalid_audio_dev,
         "No audio device selected; pick from the following:");

    /* initialize sound system */
    ans = init_audio(settings.audio_idx, settings.tone_freq);
    DIE(ans, "unable to initialize sound system");

    /* initialize system RAM */
    ans = init_system(settings.rom_off,   settings.font_off,
                      settings.rom_path,  settings.ref_int,
                      settings.new_shift, settings.lazy_render);
    GOTO(ans, cleanup_sound, "unable to initialize system");

    /* initialize display */
    ans = init_display(settings.scale_f, settings.lazy_render);
    GOTO(ans, cleanup_sound, "unable to initialize display");

    /* start the CPU */
    ans = sys_start(settings.frequency, settings.rom_off);
    GOTO(ans, cleanup_sound, "unable to initialize system CPU");

    /* normal termination path */
    ret = 0;
    goto cleanup_sound;

invalid_audio_dev:
    list_audio_devs();

    /* cleanup procedure */
cleanup_sound:
    ans = terminate_audio();
    DIE(ans, "unable to terminate sound system");

    return ret;
}

