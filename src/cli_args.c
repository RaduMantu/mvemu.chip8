#include <stdio.h>      /* sscanf */
#include <string.h>     /* strdup */

#include "cli_args.h"
#include "util.h"


/* argp API global variables */
const char *argp_program_version     = "version 1.0";
const char *argp_program_bug_address = "<andru.mantu@gmail.com>";

/* command line arguments */
static struct argp_option options[] = {
    { "rom-offset",   'r', "UINT", 0, "ROM offset in memory (default:0x200)" },
    { "font-offset",  'f', "UINT", 0, "Sprites offset in memory (default:0x50)" },
    { "scale-factor", 's', "UINT", 0, "Window scale factor (default:10)" },
    { "cpu-freq",     'c', "HZ",   0, "CPU frequency (default:200)" },
    { "ref-int",      'i', "UINT", 0, "Screen refresh interval (default:20)" },
    { "new-shift",    'n', NULL,   0, "Use new SHL, SHR [1] (default:no)" },
    { "lazy-render",  'l', NULL,   0, "Refresh screen on DXYN, 00E0 (default:no)" },
    { 0 }
};

/* argument parser prototype */
static error_t parse_opt(int, char *, struct argp_state *);

/* description of accepted non-option arguments */
static char args_doc[] = "ROM_FILE";

/* program documentation */
static char doc[] =
    "mvemu.chip8 -- A CHIP-8 emulator"
    "\v"
    "[1] Originally, 8XY6 and 8XYE shifted Vy and stored the result into \n"
    "    Vx. New interpretations of these instructions ignore Vy and instead \n"
    "    perform the operation on Vx, directly.";

/* declaration of relevant structures */
struct argp          argp = { options, parse_opt, args_doc, doc };
struct user_settings settings = {
    .rom_path    = NULL,
    .rom_off     = 0x200,
    .font_off    = 0x50,
    .scale_f     = 10,
    .frequency   = 200,
    .ref_int     = 20,
    .new_shift   = 0,
    .lazy_render = 0,
};

/* parse_opt - parses one argument and updates relevant structures
 *  @key   : argument id
 *  @arg   : pointer to the actual argument
 *  @state : parsing state
 *
 *  @return : 0 if everything ok
 */
static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    switch (key) {
        /* offset at which ROM is loaded in memory (normally 0x200) */
        case 'r':
            sscanf(arg, "%hu", &settings.rom_off);
            break;
        /* offset at which font sprites are loaded in memory (normally 0x50) */
        case 'f':
            sscanf(arg, "%hu", &settings.font_off);
            break;
        /* window scale factor */
        case 's':
            sscanf(arg, "%hu", &settings.scale_f);
            break;
        /* CPU frequency */
        case 'c':
            sscanf(arg, "%hu", &settings.frequency);
            break;
        /* screen refresh interval */
        case 'i':
            sscanf(arg, "%hu", &settings.ref_int);
            break;
        /* use new implementation of shift operations */
        case 'n':
            settings.new_shift = 1;
            break;
        /* render screen only on DXYN or 00E0, not at regular intervals */
        case 'l':
            settings.lazy_render = 1;
            break;
        /* ROM file location (relative or absolute) */
        case ARGP_KEY_ARG:
            RET(settings.rom_path, -1, "Too many arguments");
            settings.rom_path = strdup(arg);
            break;
        defualt:            /* unknown argument */
            return ARGP_ERR_UNKNOWN;
    };

    return 0;
}

