// SPDX-License-Identifier: MIT

#ifndef _VNC_H
#define _VNC_H

#include <stdint.h>

#define VNC_KEY_QUEUE_SIZE 16

struct vnc_key_event {
    uint8_t mac_keycode;   /* Mac virtual key code (0x00-0x7F) */
    uint8_t mac_char;      /* ASCII char or 0 for non-printable */
    uint8_t down;          /* 1 = keyDown, 0 = keyUp */
    uint16_t modifiers;    /* modifier state snapshot (Mac format) */
};

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
    /* Keyboard ring buffer: VNC thread writes at key_head, CPU thread reads at key_tail */
    struct vnc_key_event key_queue[VNC_KEY_QUEUE_SIZE];
    volatile uint8_t key_head;       /* next write index (VNC thread) */
    volatile uint8_t key_tail;       /* next read index (CPU thread) */
    uint16_t key_modifiers;          /* running modifier state (VNC thread only) */
};

void vnc_start(struct vnc_config *cfg);
void vnc_stop(void);

#endif /* _VNC_H */
