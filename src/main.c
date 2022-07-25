#include <stdint.h>     /* [u]int*_t       */

#include "cli_args.h"
#include "system.h"
#include "display.h"
#include "util.h"



int32_t main(int32_t argc, char *argv[])
{
    int32_t ans;    /* answer */

    /* parse command line arguments */
    argp_parse(&argp, argc, argv, 0, 0, &settings);
    DIE(!settings.rom_path,  "No ROM provided");
    DIE(!settings.scale_f,   "Scale factor 0 not allowed");
    DIE(!settings.frequency, "CPU frequency 0 not allowed");
    DIE(!settings.ref_int,   "screen refresh interval 0 not allowed");

    /* initialize system RAM */
    ans = init_system(settings.rom_off,   settings.font_off,
                      settings.rom_path,  settings.ref_int,
                      settings.new_shift, settings.lazy_render);
    DIE(ans, "unable to initialize system");

    /* initialize display */
    ans = init_display(settings.scale_f, settings.lazy_render);
    DIE(ans, "unable to initialize display");

    /* start the CPU */
    ans = sys_start(settings.frequency, settings.rom_off);
    DIE(ans, "unable to initialize system CPU");

    return 0;
}
