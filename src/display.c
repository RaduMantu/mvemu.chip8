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

/******************************************************************************
 **************************** INTERNAL STRUCTURES *****************************
 ******************************************************************************/

static SDL_Window   *window;
static SDL_Renderer *render;
static SDL_Texture  *texture;

/* we need to maintain a copy of the screen state to avoid querying the SDL *
 * texture; accessing the value is needed for flipping active pixels        */
static uint8_t pixels[32 * 64] = { [0 ... 2047] = 0x00 };

static uint32_t win_w;              /* scaled window width   */
static uint32_t win_h;              /* scaled window height  */
static uint16_t scale_f;            /* window scaling factor */

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
    /* initialize local copies for window parameters */
    win_w       = 64 * sf;
    win_h       = 32 * sf;
    scale_f     = sf;

    /* create a window object */
    window = SDL_CreateWindow("CHIP8",
                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                win_w, win_h, SDL_WINDOW_SHOWN);
    GOTO(!window, clean_params, "unable to create window (%s)",
         SDL_GetError());

    /* create an accelerated rendering context */
    render = SDL_CreateRenderer(window, -1,
                SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
    GOTO(!render, clean_window, "unable to create rendering context (%s)",
         SDL_GetError());

    /* create a texture (to stretch to the window size) */
    texture = SDL_CreateTexture(render, SDL_PIXELFORMAT_RGBA8888,
                    SDL_TEXTUREACCESS_TARGET,
                    64, 32);
    GOTO(!texture, clean_renderer, "unable to create texture (%s)",
         SDL_GetError());

    /* clear initial screen (first instruction should be 00E0 anyway) */
    clear_screen();

    return 0;

    /* error cleanup */
clean_renderer:
    SDL_DestroyRenderer(render);
clean_window:
    SDL_DestroyWindow(window);
clean_params:
    win_w   = 0;
    win_h   = 0;
    scale_f = 0;

    return -1;
}

/* clear_screen - resets the screen texture to the default color (black)
 */
void clear_screen(void)
{
    /* set rendering target to texture */
    SDL_SetRenderTarget(render, texture);
    /* deactivate all pixels */
    SDL_SetRenderDrawColor(render, 0x00, 0x00, 0x00, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(render);
    /* disassociate texture from renderer */
    SDL_SetRenderTarget(render, NULL);

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
    SDL_Point *points[] = {         /* [0] = deactivate; [1] = activate */
        alloca(8 * n * sizeof(SDL_Point)),
        alloca(8 * n * sizeof(SDL_Point)),
    };
    size_t    lens[] = { 0, 0 };    /* lengths of (de)activation lists */
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

            /* determine if collision occurred at least once */
            collision |= (!*pxp && npx);

            /* add pixel to the (de)activation list */
            points[*pxp][lens[*pxp]].x = _x;
            points[*pxp][lens[*pxp]].y = _y;
            lens[*pxp]++;
        }
    }

    /* set rendering target to texture */
    SDL_SetRenderTarget(render, texture);
    /* deactivate pixels */
    SDL_SetRenderDrawColor(render, 0x00, 0x00, 0x00, SDL_ALPHA_OPAQUE);
    SDL_RenderDrawPoints(render, points[0], lens[0]);
    /* activate pixels */
    SDL_SetRenderDrawColor(render, 0xff, 0xff, 0xff, SDL_ALPHA_OPAQUE);
    SDL_RenderDrawPoints(render, points[1], lens[1]);
    /* disassociate texture from renderer */
    SDL_SetRenderTarget(render, NULL);

    return collision;
}

/* refresh_display - forces rendering the texture on screen
 *
 * This should be called in the main system loop to avoid artifacts.
 */
void refresh_display(void)
{
    SDL_RenderCopy(render, texture, NULL, NULL);
    SDL_RenderPresent(render);
}

