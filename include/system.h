#include <stdint.h>     /* [u]int*_t */

#ifndef _SYSTEM_H
#define _SYSTEM_H

#define RAM_SZ      4096    /* amount of memory    */
#define TIMER_HZ      60    /* timer ticks per sec */

/* registers */
struct chip8_regs {
    uint8_t  V[16];     /* general-purpose registers (VF = flag register) */
    uint16_t I;         /* memory address register   */

    uint8_t DT;         /* delay timer */
    uint8_t ST;         /* sound timer */

    uint16_t PC;        /* program counter */
    uint8_t  SP;        /* stack pointer (stack = 16 words) */
};

#define VF V[15]

/* public API */
int32_t init_system(uint16_t, uint16_t, char *, uint16_t, uint8_t);
int32_t sys_start(uint16_t, uint16_t);

#endif /* _SYSTEM_H */

