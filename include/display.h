#include <stdint.h>

#ifndef _DISPLAY_H
#define _DISPLAY_H

/* public API */
int32_t init_display(uint16_t, uint8_t);
void    clear_screen(void);
uint8_t display_sprite(uint8_t, uint8_t, uint8_t *, uint8_t);
void    refresh_display(void);

#endif /* _DISPLAY_H */

