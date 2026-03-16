// SPDX-License-Identifier: MIT

#ifndef _VNC_H
#define _VNC_H

#include <stdint.h>

struct vnc_config {
    uint8_t *ram_base;      /* pointer to emulated RAM */
    uint32_t ram_size;       /* size of emulated RAM in bytes */
    int port;                /* VNC listen port (default 5900) */
    int enabled;             /* set by config parser */
    int running;             /* set while VNC thread is alive */
    volatile uint8_t mouse_button; /* current button: 1=down, 0=up */
    volatile uint8_t mouse_pending; /* pending event: 1=mouseDown, 2=mouseUp, 0=none */
    volatile uint16_t mouse_event_x; /* position for pending event */
    volatile uint16_t mouse_event_y;
};

void vnc_start(struct vnc_config *cfg);
void vnc_stop(void);

#endif /* _VNC_H */
