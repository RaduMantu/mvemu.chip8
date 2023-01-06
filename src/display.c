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

#include <SDL2/SDL.h>           /* SDL API       */
#include <SDL2/SDL_pixels.h>    /* SDL pixel ops */
#include <stdint.h>             /* [u]int*_t     */
#include <alloca.h>             /* alloca        */

#include "display.h"
#include "util.h"

/* inactive pixel color */
#define DARK_R      0x2a
#define DARK_G      0x47
#define DARK_B      0x33
#define DARK_COLOR  DARK_R, DARK_G, DARK_B

/* active pixel color */
#define LIGHT_R     0x4b
#define LIGHT_G     0x69
#define LIGHT_B     0x33
#define LIGHT_COLOR LIGHT_R, LIGHT_G, LIGHT_B

/******************************************************************************
 **************************** INTERNAL STRUCTURES *****************************
 ******************************************************************************/

static SDL_Window   *window;
static SDL_Renderer *render;

/* logical screen state */
static uint8_t pixels[32 * 64] = { [0 ... 2047] = 0x00 };

/******************************************************************************
 ************************* PUBLIC API IMPLEMENTATION **************************
 ******************************************************************************/

/* init_display - initializes SDL2-based display
 *  @sf : window scaling factor
 *
 *  @return : 0 if everything went well
 */
int32_t init_display(uint16_t sf)
{
    int ans;    /* answer */

    /* create a window object */
    window = SDL_CreateWindow("CHIP8",
                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                64 * sf, 32 *  sf, SDL_WINDOW_SHOWN);
    RET(!window, -1, "unable to create window (%s)",
         SDL_GetError());

    /* create an accelerated rendering context */
    render = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    GOTO(!render, clean_window, "unable to create rendering context (%s)",
         SDL_GetError());

    /* set renderer scale factor */
    ans = SDL_RenderSetScale(render, sf, sf);
    GOTO(ans, clean_renderer, "unable to set rendering scale factor (%s)",
         SDL_GetError());

    /* clear initial screen (first instruction should be 00E0 anyway) */
    clear_screen();

    return 0;

    /* error cleanup */
clean_renderer:
    SDL_DestroyRenderer(render);
clean_window:
    SDL_DestroyWindow(window);

    return -1;
}

/* clear_screen - resets the screen texture to the default color (black)
 */
void clear_screen(void)
{
    /* deactivate all pixels */
    SDL_SetRenderDrawColor(render, DARK_COLOR, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(render);

    /* clear logical representation */
    memset(pixels, 0x00, sizeof(pixels));
}

/* display_sprite - flips pixels according to sprite data
 *  @x   : horizontal starting location on screen
 *  @y   : vertical starting location on screen
 *  @src : start of sprite data in host memory
 *  @n   : size of spirte [bytes]
 *
 *  @return : 1 if any pixels were turned off
 */
uint8_t display_sprite(uint8_t x, uint8_t y, uint8_t *src, uint8_t n)
{
    uint8_t   _x;                   /* individual x coordinate         */
    uint8_t   _y;                   /* individual y coordinate         */
    uint8_t   *pxp;                 /* pixel pointer                   */
    uint8_t   npx;                  /* new pixel state (from sprite)   */
    uint8_t   collision = 0;        /* 1 if any pixel flipped off      */

    /* for each line in the sprite */
    for (size_t i = 0; i < n; i++) {
        /* for each pixel in the sprite's current line */
        for (size_t j = 0; j < 8; j++) {
            _x = (x + j) % 64;      /* current column (w/ wraparound) */
            _y = (y + i) % 32;      /* current line   (w/ wraparound) */

            /* retrieve current pixel and xor it with appropriate sprite bit */
            pxp   = &pixels[_y * 64 + _x];
            npx   = (src[i] >> (7 - j)) & 0x01;
            *pxp ^= npx;

            pixels[_y * 64 + _x] = *pxp;

            /* determine if collision occurred at least once */
            collision |= (!*pxp && npx);
        }
    }

    return collision;
}

/* refresh_display - forces rendering the texture on screen
 *
 * This should be called in the main system loop to avoid artifacts.
 */
void refresh_display(void)
{
    /* deactivate all pixels */
    SDL_SetRenderDrawColor(render, DARK_COLOR, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(render);

    /* redraw each active pixel */
    SDL_SetRenderDrawColor(render, LIGHT_COLOR, SDL_ALPHA_OPAQUE);
    for (size_t i = 0; i < 32; i++) {
        for (size_t j = 0; j < 64; j++) {
            /* skip dark pixels */
            if (!pixels[i * 64 + j])
                continue;

            /* draw active pixels */
            SDL_RenderDrawPoint(render, j, i);
        }
    }

    /* present buffer */
    SDL_RenderPresent(render);
}

