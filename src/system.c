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

#include <fcntl.h>      /* open                         */
#include <unistd.h>     /* read, close                  */
#include <string.h>     /* memset, memmove              */
#include <sys/stat.h>   /* fstat                        */
#include <sys/mman.h>   /* m[un]map                     */
#include <arpa/inet.h>  /* ntohs, htons                 */
#include <time.h>       /* time, timer_{create,settime} */
#include <stdlib.h>     /* [s]random                    */
#include <signal.h>     /* sigval                       */
#include <SDL2/SDL.h>   /* SDL_PollEvent                */
#include <portaudio.h>  /* portaudio                    */

#include "system.h"
#include "display.h"
#include "sound.h"
#include "util.h"

/******************************************************************************
 **************************** INTERNAL STRUCTURES *****************************
 ******************************************************************************/

static void              *ram;              /* system RAM                 */
static uint16_t          stack[16];         /* system stack (out-of-RAM)  */
static struct chip8_regs regs = { 0 };      /* system registers           */
static timer_t           cpu_timerid;       /* cpu timer                  */
static timer_t           delay_timerid;     /* delay timer                */
static timer_t           sound_timerid;     /* sound timer                */
static uint16_t          font_offset;       /* font sprites offset in RAM */
static uint16_t          ref_interval;      /* screen refresh interval    */
static uint8_t           new_shift;         /* use new shift operations   */
static uint8_t           lazy_render;       /* lazy_render                */
static uint8_t           quit = 0;          /* breaks main system loop    */

/* key state */
static uint8_t key_state[16] = { [0 ... 15] = 0 };

/* key map (chip8 key -> SDL keycode) *
 *        1 2 3 C  |  1 2 3 4         *
 *        4 5 6 D  |  Q W E R         *
 *        7 8 9 E  |  A S D F         *
 *        A 0 B F  |  Z X C V         */
static SDL_Scancode key_map[16] = {
    [0x0] = SDL_SCANCODE_X,
    [0x1] = SDL_SCANCODE_1,
    [0x2] = SDL_SCANCODE_2,
    [0x3] = SDL_SCANCODE_3,
    [0x4] = SDL_SCANCODE_Q,
    [0x5] = SDL_SCANCODE_W,
    [0x6] = SDL_SCANCODE_E,
    [0x7] = SDL_SCANCODE_A,
    [0x8] = SDL_SCANCODE_S,
    [0x9] = SDL_SCANCODE_D,
    [0xa] = SDL_SCANCODE_Z,
    [0xb] = SDL_SCANCODE_C,
    [0xc] = SDL_SCANCODE_4,
    [0xd] = SDL_SCANCODE_R,
    [0xe] = SDL_SCANCODE_F,
    [0xf] = SDL_SCANCODE_V,
};

/* font sprites; to be copied in emulated system RAM */
static const uint8_t font_sprites[] = {
    0xF0, 0x90, 0x90, 0x90, 0xF0,   /* 0 */
    0x20, 0x60, 0x20, 0x20, 0x70,   /* 1 */
    0xF0, 0x10, 0xF0, 0x80, 0xF0,   /* 2 */
    0xF0, 0x10, 0xF0, 0x10, 0xF0,   /* 3 */
    0x90, 0x90, 0xF0, 0x10, 0x10,   /* 4 */
    0xF0, 0x80, 0xF0, 0x10, 0xF0,   /* 5 */
    0xF0, 0x80, 0xF0, 0x90, 0xF0,   /* 6 */
    0xF0, 0x10, 0x20, 0x40, 0x40,   /* 7 */
    0xF0, 0x90, 0xF0, 0x90, 0xF0,   /* 8 */
    0xF0, 0x90, 0xF0, 0x10, 0xF0,   /* 9 */
    0xF0, 0x90, 0xF0, 0x90, 0x90,   /* A */
    0xE0, 0x90, 0xE0, 0x90, 0xE0,   /* B */
    0xF0, 0x80, 0x80, 0x80, 0xF0,   /* C */
    0xE0, 0x90, 0x90, 0x90, 0xE0,   /* D */
    0xF0, 0x80, 0xF0, 0x80, 0xF0,   /* E */
    0xF0, 0x80, 0xF0, 0x80, 0x80    /* F */
};

/******************************************************************************
 ****************************** HELPER FUNCTIONS ******************************
 ******************************************************************************/

/* update_keystate - updates key_state with currently pressed keys
 *  @return : index in key_state of newly pressed key (if any)
 *            or someting in the range [0x10; 0xff] (if none)
 *
 * NOTE: if more than one keys are newly pressed, only the one with the lowest
 *       index in key_map will be reported (via return); otherwise, all key
 *       state changes will be reflected in key_state
 * NOTE: this is supposed to be a lazy state update, invoked when encountering
 *       specific instructions (i.e.: EX9E, EXA1, FX0A) to avoid tanking the
 *       performance
 */
static uint8_t
update_keystate(void)
{
    uint8_t       ret = 0xff;   /* return value                     */
    const uint8_t *new_state;   /* SDL key state internal structure */

    /* get key state via SDL (points to internal SDL structure) */
    new_state = SDL_GetKeyboardState(NULL);

    /* for each mapped key */
    for (size_t i = 0; i < sizeof(key_state); i++) {
        /* first newly pressed key */
        if (!key_state[i] && new_state[key_map[i]] && ret == 0xff)
            ret = i;

        /* update state for current key */
        key_state[i] = new_state[key_map[i]];
    }

    return ret;
}

/******************************************************************************
 ************************** INSTRUCTION INTERPRETERS **************************
 ******************************************************************************/

/* 00E0 - clear screen
 */
static inline void
ins_00E0(void)
{
    clear_screen();

    /* if employing lazy rendering, force a screen refresh right now */
    if (lazy_render)
        refresh_display();
}

/* 00EE - return from subroutine
 */
static inline void
ins_00EE(void)
{
    regs.PC = stack[--regs.SP];
}

/* 1NNN - jump to address NNN
 *  @nnn : destination address
 */
static inline void
ins_1NNN(uint16_t nnn)
{
    regs.PC = nnn;
}

/* 2NNN - call subroutine at NNN
 *  @nnn : address of subroutine
 */
static inline void
ins_2NNN(uint16_t nnn)
{
    stack[regs.SP++] = regs.PC;
    regs.PC = nnn;
}

/* 3XKK - skip next ins if Vx equals KK
 *  @x  : register index
 *  @kk : value for comparison
 */
static inline void
ins_3XKK(uint8_t x, uint8_t kk)
{
    regs.PC += 2 * (regs.V[x] == kk);
}

/* 4XKK - skip next ins if Vx does not equal KK
 *  @x  : register index
 *  @kk : value for comparison
 */
static inline void
ins_4XKK(uint8_t x, uint8_t kk)
{
    regs.PC += 2 * (regs.V[x] != kk);
}

/* 5XY0 - skip next inst if Vx equals Vy
 *  @x : register index
 *  @y : register index
 */
static inline void
ins_5XY0(uint8_t x, uint8_t y)
{
    regs.PC += 2 * (regs.V[x] == regs.V[y]);
}

/* 6XKK - set value of VX register to KK
 *  @x  : register index
 *  @nn : new register value
 */
static inline void
ins_6XKK(uint8_t x, uint8_t kk)
{
    regs.V[x] = kk;
}

/* 7XNN - add KK to Vx
 *  @x  : register index
 *  @nn : added value
 */
static inline void
ins_7XKK(uint8_t x, uint8_t kk)
{
    regs.V[x] += kk;
}

/* 8XY0 - copy value of Vy into Vx
 *  @x : register index
 *  @y : register index
 */
static inline void
ins_8XY0(uint8_t x, uint8_t y)
{
    regs.V[x] = regs.V[y];
}

/* 8XY1 - load Vx OR Vy into Vx
 *  @x : register index
 *  @y : register index
 *
 * NOTE: must clear Vf (quirk)
 */
static inline void
ins_8XY1(uint8_t x, uint8_t y)
{
    regs.V[x] |= regs.V[y];
    regs.VF = 0x00;
}

/* 8XY2 - load Vx AND Vy into Vx
 *  @x : register index
 *  @y : register index
 *
 * NOTE: must clear Vf (quirk)
 */
static inline void
ins_8XY2(uint8_t x, uint8_t y)
{
    regs.V[x] &= regs.V[y];
    regs.VF = 0x00;
}

/* 8XY3 - load Vx XOR Vy into Vx
 *  @x : register index
 *  @y : register index
 *
 * NOTE: must clear Vf (quirk)
 */
static inline void
ins_8XY3(uint8_t x, uint8_t y)
{
    regs.V[x] ^= regs.V[y];
    regs.VF = 0x00;
}

/* 8XY4 - add Vx and Vy into Vx; VF = carry
 *  @x : register index
 *  @y : register index
 */
static inline void
ins_8XY4(uint8_t x, uint8_t y)
{
    uint8_t Vx, Vy; /* backup in case of register collision */

    Vx = regs.V[x];
    Vy = regs.V[y];

    regs.VF = (Vx + Vy) > 0xff;
    regs.V[x] = Vx + Vy;
}

/* 8XY5 - subtract Vy from Vx into Vx; VF = NOT borrow
 *  @x : register index
 *  @y : register index
 */
static inline void
ins_8XY5(uint8_t x, uint8_t y)
{
    uint8_t Vx, Vy; /* backup in case of register collision */

    Vx = regs.V[x];
    Vy = regs.V[y];

    regs.VF = Vx > Vy;
    regs.V[x] = Vx - Vy;
}

/* 8XY6 - copy Vy into Vx and shift Vx right by 1; VF = popped bit
 *  @x : register index
 *  @y : register index
 *
 * NOTE: may prove incompatible with CHIP-48 or SUPER-CHIP programs
 *       copying Vy into Vx is ignored in these architectures
 *       see the `--new-shift | -n` option for compatibility
 */
static inline void
ins_8XY6(uint8_t x, uint8_t y)
{
    uint8_t Vy;     /* backup in case of register collision */

    /* use new implementation of shift operations */
    if (new_shift)
        y = x;

    Vy = regs.V[y];

    /* in case Vx == Vf, carry overrides shifted value */
    regs.V[x] = Vy >> 1;
    regs.VF = Vy & 0x01;
}

/* 8XY7 - subtract Vx from Vy into Vx; VF = NOT borrow
 *  @x : register index
 *  @y : register index
 */
static inline void
ins_8XY7(uint8_t x, uint8_t y)
{
    uint8_t Vx, Vy; /* backup in case of register collision */

    Vx = regs.V[x];
    Vy = regs.V[y];

    regs.VF = Vy > Vx;
    regs.V[x] = Vy - Vx;
}

/* 8XYE - copy Vy into Vx and shift Vx left by 1; VF = popped bit
 *  @x : register index
 *  @y : register index
 *
 * NOTE: may prove incompatible with CHIP-48 or SUPER-CHIP programs
 *       copying Vy into Vx is ignored in these architectures
 *       see the `--new-shift | -n` option for compatibility
 */
static inline void
ins_8XYE(uint8_t x, uint8_t y)
{
    uint8_t Vy;     /* backup in case of register collision */

    /* use new implementation of shift operations */
    if (new_shift)
        y = x;

    Vy = regs.V[y];

    /* in case Vx == Vf, carry overrides shifted value */
    regs.V[x] = Vy << 1;
    regs.VF = (Vy & 0x80) >> 7;
}

/* 9XY0 - skip next inst if Vx does not equal Vy
 *  @x : register index
 *  @y : register index
 */

static inline void
ins_9XY0(uint8_t x, uint8_t y)
{
    regs.PC += 2 * (regs.V[x] != regs.V[y]);
}
/* ANNN - set value of I register
 *  @nnn : new register value
 */
static inline void
ins_ANNN(uint16_t nnn)
{
    regs.I = nnn;
}

/* BNNN - jump to address NNN + V0
 *  @nnn : destination address
 */
static inline void
ins_BNNN(uint16_t nnn)
{
    regs.PC = (nnn + regs.V[0]) & 0x0fff;
}

/* CXKK - load a random value AND KK into Vx
 *  @x  : register index
 *  @kk : bit mask
 */
static inline void
ins_CXKK(uint8_t x, uint8_t kk)
{
    regs.V[x] = random() & kk;
}

/* DXYN - display at (Vx, Vy) an N-byte sprite starting at I; VF = collision
 *  @x : register index
 *  @y : register index
 *  @n : size of sprite [bytes]
 *
 * The value of individual pixels is XORed.
 * A pixel deactivation marks a collision.
 */
static inline void
ins_DXYN(uint8_t x, uint8_t y, uint8_t n)
{
    regs.VF = display_sprite(regs.V[x], regs.V[y], ram + regs.I, n);

    /* if employing lazy rendering, force a screen refresh right now */
    if (lazy_render)
        refresh_display();
}

/* EX9E - skip next ins if the Vx key is pressed
 *  @x : register index
 */
static inline void
ins_EX9E(uint8_t x)
{
    /* lazy keystate update */
    update_keystate();

    regs.PC += 2 * key_state[regs.V[x]];
}

/* EXA1 - skip next ins if the Vx key is not pressed
 *  @x : register index
 */
static inline void
ins_EXA1(uint8_t x)
{
    /* lazy keystate update */
    update_keystate();

    regs.PC += 2 * !key_state[regs.V[x]];
}

/* FX07 - store DT to Vx
 *  @x : register index
 */
static inline void
ins_FX07(uint8_t x)
{
    int32_t ans;                    /* answer                         */
    struct itimerspec interval;     /* Delay Timer remaining interval */

    /* clear Vx in preparation for a sudden abort in case of error */
    regs.V[x] = 0;

    /* get remaining time until DT expires */
    ans = timer_gettime(delay_timerid, &interval);
    RET(ans, , "unable to query timer (%s)", strerror(errno));

    /* get DT counter value from remaining timespan */
    regs.V[x] = interval.it_value.tv_sec  * 60 +
                interval.it_value.tv_nsec * 60 / 1e9;
}

/* FX0A - wait for key press; store its code into Vx
 *  @x : register index
 *
 * NOTE: this instruction is blocking!
 */
static inline void
ins_FX0A(uint8_t x)
{
    regs.V[x] = update_keystate();

    /* repeat this instruction if no new key press registered */
    if (regs.V[x] > 0x0f)
        regs.PC -= 2;
}

/* FX15 - load DT from Vx
 *  @x : register index
 */
static inline void
ins_FX15(uint8_t x)
{
    int32_t           ans;          /* answer               */
    struct itimerspec interval = {  /* Delay Timer interval */
        .it_value = {                   /* initial timer expiration @60Hz */
            .tv_sec  = regs.V[x] / 60,
            .tv_nsec = regs.V[x] % 60 * 1e9 / 60,
        },
        .it_interval = {                /* no subsequent expiration */
            .tv_sec  = 0,
            .tv_nsec = 0,
        },
    };

    /* arm timer */
    ans = timer_settime(delay_timerid, 0, &interval, NULL);
    RET(ans, , "unable to arm timer (%s)", strerror(errno));
}

/* FX18 - load ST from Vx
 *  @x : register index
 */
static inline void
ins_FX18(uint8_t x)
{
    int32_t           ans;          /* answer               */
    struct itimerspec interval = {  /* Sound Timer interval */
        .it_value = {                   /* initial timer expiration @60Hz */
            .tv_sec  = regs.V[x] / 60,
            .tv_nsec = regs.V[x] % 60 * 1e9 / 60,
        },
        .it_interval = {                /* no subsequent expiration */
            .tv_sec  = 0,
            .tv_nsec = 0,
        },
    };

    /* arm timer */
    ans = timer_settime(sound_timerid, 0, &interval, NULL);
    RET(ans, , "unable to arm timer (%s)", strerror(errno));

    /* start sound playback */
    ans = start_playback();
    RET(ans, , "unable to start playback");
}

/* FX1E - add Vx to I; set VF if I overflows
 *  @x : register index
 */
static inline void
ins_FX1E(uint8_t x)
{
    regs.I += regs.V[x];
    regs.VF = regs.I > 0x0fff;
    regs.I &= 0x0fff;
}

/* FX29 - load address of digit in Vx to I
 *  @x : register index
 */
static inline void
ins_FX29(uint8_t x)
{
    regs.I = font_offset + 5 * (regs.V[x] & 0x0f);
}

/* FX33 - store BCD representation of Vx at address I
 *  @x : register index
 */
static inline void
ins_FX33(uint8_t x)
{
    uint8_t *_ram = (uint8_t *) ram;

    _ram[regs.I + 0] = (regs.V[x] / 100) % 10;
    _ram[regs.I + 1] = (regs.V[x] /  10) % 10;
    _ram[regs.I + 2] = (regs.V[x] /   1) % 10;
}

/* FX55 - store V0-x at address I
 *  @x : register index
 *
 * NOTE: I must be incremented (quirk)
 */
static inline void
ins_FX55(uint8_t x)
{
    memmove(ram + regs.I, regs.V, x + 1);
    regs.I += x + 1;
}

/* FX65 - load V0-x from address I
 *  @x : register index
 *
 * NOTE: I must be incremented (quirk)
 */
static inline void
ins_FX65(uint8_t x)
{
    memmove(regs.V, ram + regs.I, x + 1);
    regs.I += x + 1;
}

/******************************************************************************
 ********************************* INTERNALS **********************************
 ******************************************************************************/

/* consume_ins - executes one instruction and updates internal state
 *  @data : user data (if any)
 *
 * NOTE: this is registered as a callback to a POSIX interval timer.
 */
static void
consume_ins(union sigval data)
{
    static   size_t     cycle = 0;          /* cycle count          */
    static   uint64_t   rbp = 0;            /* first call frame RBP */
    register uint64_t   _rbp asm("rbp");    /* current RBP          */
    uint16_t            ins;                /* fetched instruction  */
    SDL_Event           ev;                 /* SDL event            */
    struct itimerspec   interval = { 0 };   /* timer disarmer       */
    int32_t             ans;                /* answer               */
    uint16_t            nnn;                /* ls 3 nibbles         */
    uint8_t             kk;                 /* ls 2 nibbles         */
    uint8_t             n;                  /* ls 1 nibble          */
    uint8_t             x;                  /* Vx reg index         */
    uint8_t             y;                  /* Vy reg index         */

    /* process SDL events (interested only in quit event) */
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
            case SDL_QUIT:
                /* disarm CPU timer; don't care about the rest */
                ans = timer_settime(cpu_timerid, 0, &interval, NULL);
                DIE(ans, "unable to disarm timer (%s)", strerror(errno));

                /* set quit condition */
                quit = 1;
        }
    }

    /* initialize reference RBP (once) */
    if (unlikely(!rbp))
        rbp = _rbp;

    /* more than one call frame means that we've preempted ourselves */
    if (rbp != _rbp) {
        WAR("CPU frequency may be too high (rbp=%#lx, _rbp=%#lx)", rbp, _rbp);
        return;
    }

    /* fetch instruction and change byte order to match host's */
    ins = ntohs(*(uint16_t *)(ram + regs.PC));
    regs.PC += 2;

    /* get easy access to potential instructions parameters */
    nnn = ins & 0x0fff;
    kk  = ins & 0x00ff;
    n   = ins & 0x000f;
    x   = (ins & 0x0f00) >> 8;
    y   = (ins & 0x00f0) >> 4;

    /* start decoding the instruction by class (first nibble) */
    switch ((ins & 0xf000) >> 12) {
        case 0x0:
            switch (ins) {
                case 0x00e0:    /* CLS */
                    ins_00E0();
                    break;
                case 0x00ee:    /* RET */
                    ins_00EE();
                    break;
                default:
                    RET(1, , "unknown instruction %04hx", ins);
            }
            break;
        case 0x1:   /* JP addr */
            ins_1NNN(nnn);
            break;
        case 0x2:   /* CALL addr */
            ins_2NNN(nnn);
            break;
        case 0x3:   /* SE Vx, byte */
            ins_3XKK(x, kk);
            break;
        case 0x4:   /* SNE Vx, byte */
            ins_4XKK(x, kk);
            break;
        case 0x5:
            switch (ins & 0x000f) {
                case 0x0:   /* SE Vx, Vy */
                    ins_5XY0(x, y);
                    break;
                default:
                    RET(1, , "unknown instruction %04hx", ins);
            }
            break;
        case 0x6:   /* LD Vx, byte */
            ins_6XKK(x, kk);
            break;
        case 0x7:   /* ADD Vx, byte */
            ins_7XKK(x, kk);
            break;
        case 0x8:
            switch (ins & 0x000f) {
                case 0x0:   /* LD Vx, Vy */
                    ins_8XY0(x, y);
                    break;
                case 0x1:   /* OR Vx, Vy */
                    ins_8XY1(x, y);
                    break;
                case 0x2:   /* AND Vx, Vy */
                    ins_8XY2(x, y);
                    break;
                case 0x3:   /* XOR Vx, Vy */
                    ins_8XY3(x, y);
                    break;
                case 0x4:   /* ADD Vx, Vy */
                    ins_8XY4(x, y);
                    break;
                case 0x5:   /* SUB Vx, Vy */
                    ins_8XY5(x, y);
                    break;
                case 0x6:   /* SHR Vx, Vy */
                    ins_8XY6(x, y);
                    break;
                case 0x7:   /* SUBN Vx, Vy */
                    ins_8XY7(x, y);
                    break;
                case 0xe:   /* SHL Vx, Vy */
                    ins_8XYE(x, y);
                    break;
                default:
                    RET(1, , "unknown instruction %04hx", ins);
            }
            break;
        case 0x9:
            switch (ins & 0x000f) {
                case 0x0:   /* SNE Vx, Vy */
                    ins_9XY0(x, y);
                    break;
                default:
                    RET(1, , "unknown instruction %04hx", ins);
            }
            break;
        case 0xa:   /* LD I, addr */
            ins_ANNN(nnn);
            break;
        case 0xb:   /* JP V0, addr */
            ins_BNNN(nnn);
            break;
        case 0xc:   /* RND Vx, byte */
            ins_CXKK(x, kk);
            break;
        case 0xd:   /* DRW Vx, Vy, nibble */
            ins_DXYN(x, y, n);
            break;
        case 0xe:
            switch (ins & 0x00ff) {
                case 0x9e:  /* SKP Vx */
                    ins_EX9E(x);
                    break;
                case 0xa1:  /* SKNP Vx */
                    ins_EXA1(x);
                    break;
                default:
                    RET(1, , "unknown instruction %04hx", ins);
            }
            break;
        case 0xf:
            switch (ins & 0x00ff) {
                case 0x07:  /* LD Vx, DT */
                    ins_FX07(x);
                    break;
                case 0x0a:  /* LD Vx, K */
                    ins_FX0A(x);
                    break;
                case 0x15:  /* LD DT, Vx */
                    ins_FX15(x);
                    break;
                case 0x18:  /* LD ST, Vx */
                    ins_FX18(x);
                    break;
                case 0x1e:  /* ADD I, Vx */
                    ins_FX1E(x);
                    break;
                case 0x29:  /* LD F, Vx */
                    ins_FX29(x);
                    break;
                case 0x33:  /* LD B, Vx */
                    ins_FX33(x);
                    break;
                case 0x55:  /* LD [I], Vx */
                    ins_FX55(x);
                    break;
                case 0x65:  /* LD Vx, [I] */
                    ins_FX65(x);
                    break;
            }
            break;
    }

    /* every so often, force display update to avoid artifacts */
    if (!lazy_render && (cycle++ % ref_interval == 0))
        refresh_display();
}

/* delay_timeout - callback for the 60Hz sound timer expiration
 *  @data : user data (if any)
 *
 * Silences the buzzer tone that was created by FX18.
 */
static void
sound_timeout(union sigval data)
{
    int32_t ans;    /* answer */

    ans = stop_playback();
    RET(ans, , "unable to stop playback");
}

/******************************************************************************
 ************************* PUBLIC API IMPLEMENTATION **************************
 ******************************************************************************/

/* init_system - allocates system RAM, maps ROM, initializes font sprites
 *  @rom_off       : ROM map offset into RAM [bytes]
 *  @_font_offset  : font sprites offset into RAM [bytes]
 *  @rom_path      : path to ROM file
 *  @_ref_interval : screen refresh interval
 *  @_lazy_render  : lazy redering, rather than at specific intervals
 *
 *  @return : starting address of the system RAM
 */
int32_t
init_system(uint16_t rom_off,
            uint16_t _font_offset,
            char     *rom_path,
            uint16_t _ref_interval,
            uint8_t  _new_shift,
            uint8_t  _lazy_render)
{
    int32_t           fd;           /* ROM file descriptor */
    struct stat       statbuf;      /* fstat result buffer */
    ssize_t           ans;          /* answer              */
    int32_t           ret = -1;     /* function status     */
    struct sigevent   ev = {        /* notification method */
        .sigev_notify            = SIGEV_THREAD,    /* handle in (this) thread */
        .sigev_value.sival_ptr   = NULL,            /* argument for handler    */
        .sigev_notify_function   = consume_ins,     /* handler function        */
        .sigev_notify_attributes = NULL,            /* new thread attributes   */
    };

    /* seed pseudo-RNG */
    srandom(time(NULL));

    /* store font offset in global static storage */
    font_offset = _font_offset;

    /* store refresh interval in global static storage */
    ref_interval = _ref_interval;

    /* store shift instruction flavor in global static storage */
    new_shift = _new_shift;

    /* store lazy rendering preference in global static storage */
    lazy_render = _lazy_render;

    /* create CPU, sound, delay timers */
    ans = timer_create(CLOCK_MONOTONIC, &ev, &cpu_timerid);
    RET(ans, -1, "unable to create cpu timer (%s)", strerror(errno));

    ev.sigev_notify_function = sound_timeout;
    ans = timer_create(CLOCK_MONOTONIC, &ev, &sound_timerid);
    RET(ans, -1, "unable to create sound timer (%s)", strerror(errno));

    /* NOTE: don't do anything on DT expiring */
    ev.sigev_notify          = SIGEV_NONE;
    ev.sigev_notify_function = NULL;
    ans = timer_create(CLOCK_MONOTONIC, &ev, &delay_timerid);
    RET(ans, -1, "unable to create delay timer (%s)", strerror(errno));

    /* open ROM file */
    fd = open(rom_path, O_RDONLY);
    RET(fd == -1, -1, "unable to open ROM (%s)", strerror(errno));

    /* determine ROM size */
    ans = fstat(fd, &statbuf);
    GOTO(ans == -1, clean_fd, "unable to stat ROM (%s)", strerror(errno));
    GOTO(statbuf.st_size + rom_off > RAM_SZ, clean_fd, "ROM is too large");

    /* allocate emulated system RAM */
    ram = mmap(NULL, RAM_SZ, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS, fd, 0);
    GOTO(ram == MAP_FAILED, clean_fd, "unable to allocate RAM (%s)",
         strerror(errno));

    /* zero out emulated system RAM before proceeding */
    memset(ram, 0x00, RAM_SZ);

    /* read contents of ROM into RAM */
    ans = read(fd, ram + rom_off, statbuf.st_size);
    GOTO(ans == -1, clean_ram, "unable to read ROM (%s)", strerror(errno));
    GOTO(ans != statbuf.st_size, clean_ram, "unable to fully read ROM");

    /* copy font sprites into RAM */
    memmove(ram + font_offset, font_sprites, sizeof(font_sprites));

    /* everything went well; bypass emultated system RAM cleanup */
    ret = 0;
    goto clean_fd;

clean_ram:
    /* reclaim emulated system RAM */
    munmap(ram, RAM_SZ);
    ram = NULL;
clean_fd:
    /* close ROM file */
    close(fd);

    return ret;
}

/* sys_start - begins execution of the loaded ROM
 *  @freq : number of instructions executed per second
 *  @pc   : program entry point (most likely ROM map offset)
 *
 *  @return : 0 if everything went well
 */
int32_t
sys_start(uint16_t freq, uint16_t pc)
{
    int32_t           ans;          /* answer              */
    struct itimerspec interval = {  /* CPU timout interval */
        .it_value = {                   /* initial timer expiration  */
            .tv_sec  = 0,
            .tv_nsec = 1e7,
        },
        .it_interval = {                /* subsequent timer interval */
            .tv_sec  = freq == 1,
            .tv_nsec = (1e9 / freq) * (freq != 1),
        },
    };

    /* set initial PC register value */
    regs.PC = pc;

    /* arm timer */
    ans = timer_settime(cpu_timerid, 0, &interval, NULL);
    RET(ans, -1, "unable to arm timer (%s)", strerror(errno));

    /* staying busy while CPU timer callback is idle                  *
     * NOTE: -O2 optimizes this busy loop in a retarded way.          *
     *       the check is done once at entry, then it jumps in place. *
     *       the loop's BB must contain at least one instruction to   *
     *       force the recheck of quit's value.                       */
    while (!quit)
        asm volatile("nop");

    return 0;
}

