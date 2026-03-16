// SPDX-License-Identifier: MIT

#define _GNU_SOURCE
#include "vnc.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <rfb/rfb.h>

#define MAC_SCREEN_W  512
#define MAC_SCREEN_H  342
#define MAC_SCREEN_STRIDE  (MAC_SCREEN_W / 8)  /* 64 bytes per row */
#define SCRNBASE_ADDR  0x0824  /* Mac low-memory global: pointer to screen buffer */
#define MTEMP_ADDR     0x0828  /* MTemp: cursor position target {v, h} */
#define RAWMOUSE_ADDR  0x082C  /* RawMouse: raw mouse position {v, h} */
#define CRSRNEW_ADDR   0x08CE  /* CrsrNew: non-zero = new position pending */
#define CSRCOUPLE_ADDR 0x08CF  /* CrsrCouple: $FF = cursor follows mouse */
#define MBSTATE_ADDR   0x0172  /* MBState: mouse button state, bit 7: 0=down 1=up */
#define VNC_FRAME_MS  33  /* ~30fps */

static struct vnc_config *vnc_cfg;
static pthread_t vnc_tid;

static uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

static void write_be16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v);
}

static void expand_screen(const uint8_t *ram, uint32_t ram_size, char *framebuf) {
    uint32_t scrn_base = read_be32(ram + SCRNBASE_ADDR);

    /* Sanity check: ScrnBase must fit within RAM */
    if (scrn_base + MAC_SCREEN_STRIDE * MAC_SCREEN_H > ram_size) {
        /* Screen pointer not valid yet — fill with white */
        memset(framebuf, 0xFF, MAC_SCREEN_W * MAC_SCREEN_H);
        return;
    }

    const uint8_t *src = ram + scrn_base;
    char *dst = framebuf;

    for (int row = 0; row < MAC_SCREEN_H; row++) {
        for (int col = 0; col < MAC_SCREEN_STRIDE; col++) {
            uint8_t byte = src[row * MAC_SCREEN_STRIDE + col];
            /* Mac convention: bit set = black (0x00), bit clear = white (0xFF) */
            for (int bit = 7; bit >= 0; bit--) {
                *dst++ = (byte & (1 << bit)) ? 0x00 : (char)0xFF;
            }
        }
    }
}

static void vnc_ptr_event(int buttonMask, int x, int y, rfbClientPtr cl) {
    (void)cl;

    /* Clamp to Mac SE screen bounds */
    if (x < 0) x = 0;
    if (x > MAC_SCREEN_W - 1) x = MAC_SCREEN_W - 1;
    if (y < 0) y = 0;
    if (y > MAC_SCREEN_H - 1) y = MAC_SCREEN_H - 1;

    uint8_t *ram = vnc_cfg->ram_base;

    /* Write MTemp and RawMouse: Point {v=y, h=x} as big-endian int16 pairs */
    write_be16(ram + MTEMP_ADDR,     (uint16_t)y);
    write_be16(ram + MTEMP_ADDR + 2, (uint16_t)x);
    write_be16(ram + RAWMOUSE_ADDR,     (uint16_t)y);
    write_be16(ram + RAWMOUSE_ADDR + 2, (uint16_t)x);

    /* Signal new cursor position to VBL cursor task */
    ram[CRSRNEW_ADDR]  = 0xFF;
    ram[CSRCOUPLE_ADDR] = 0xFF;

    /* Button: update MBState for direct readers */
    uint8_t new_button = (buttonMask & 1) ? 1 : 0;
    ram[MBSTATE_ADDR] = new_button ? 0x00 : 0x80;

    /* On transition, signal CPU thread to inject event into queue */
    if (new_button != vnc_cfg->mouse_button) {
        vnc_cfg->mouse_event_x = (uint16_t)x;
        vnc_cfg->mouse_event_y = (uint16_t)y;
        __sync_synchronize();
        if (new_button)
            vnc_cfg->mouse_pending |= 1;  /* mouseDown (bit 0) */
        else
            vnc_cfg->mouse_pending |= 2;  /* mouseUp (bit 1) */
        vnc_cfg->mouse_button = new_button;
    }
}

/* ── Keyboard: X11 keysym → Mac virtual keycode ────────────────────── */

/* Mac modifier bit masks (evtQModifiers high byte) */
#define MAC_MOD_CMD    0x0100  /* bit 8: cmdKey */
#define MAC_MOD_SHIFT  0x0200  /* bit 9: shiftKey */
#define MAC_MOD_ALPHA  0x0400  /* bit 10: alphaLock */
#define MAC_MOD_OPT    0x0800  /* bit 11: optionKey */
#define MAC_MOD_CTRL   0x1000  /* bit 12: controlKey */

/* ASCII 0x00-0x7F → Mac virtual keycode.  0xFF = unmapped. */
static const uint8_t ascii_to_mac_keycode[128] = {
    /* 0x00-0x0F  (ctrl chars) */
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    /* 0x10-0x1F  (ctrl chars) */
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    /* 0x20 ' '  0x21 !  0x22 "  0x23 #  0x24 $  0x25 %  0x26 &  0x27 ' */
       0x31,    0x12,   0x27,   0x14,   0x15,   0x17,   0x1A,   0x27,
    /* 0x28 (  0x29 )  0x2A *  0x2B +  0x2C ,  0x2D -  0x2E .  0x2F / */
       0x19,   0x1D,   0x1C,   0x18,   0x2B,   0x1B,   0x2F,   0x2C,
    /* 0x30 0  0x31 1  0x32 2  0x33 3  0x34 4  0x35 5  0x36 6  0x37 7 */
       0x1D,   0x12,   0x13,   0x14,   0x15,   0x17,   0x16,   0x1A,
    /* 0x38 8  0x39 9  0x3A :  0x3B ;  0x3C <  0x3D =  0x3E >  0x3F ? */
       0x1C,   0x19,   0x29,   0x29,   0x2B,   0x18,   0x2F,   0x2C,
    /* 0x40 @  A      B      C      D      E      F      G      */
       0x13,   0x00,   0x0B,   0x08,   0x02,   0x0E,   0x03,   0x05,
    /* H      I      J      K      L      M      N      O      */
       0x04,   0x22,   0x26,   0x28,   0x25,   0x2E,   0x2D,   0x1F,
    /* P      Q      R      S      T      U      V      W      */
       0x23,   0x0C,   0x0F,   0x01,   0x11,   0x20,   0x09,   0x0D,
    /* X      Y      Z      [      \      ]      ^      _      */
       0x07,   0x10,   0x06,   0x21,   0x2A,   0x1E,   0x16,   0x1B,
    /* 0x60 `  a      b      c      d      e      f      g      */
       0x32,   0x00,   0x0B,   0x08,   0x02,   0x0E,   0x03,   0x05,
    /* h      i      j      k      l      m      n      o      */
       0x04,   0x22,   0x26,   0x28,   0x25,   0x2E,   0x2D,   0x1F,
    /* p      q      r      s      t      u      v      w      */
       0x23,   0x0C,   0x0F,   0x01,   0x11,   0x20,   0x09,   0x0D,
    /* x      y      z      {      |      }      ~      DEL    */
       0x07,   0x10,   0x06,   0x21,   0x2A,   0x1E,   0x32,   0xFF,
};

static void vnc_kbd_event(rfbBool down, rfbKeySym keySym, rfbClientPtr cl) {
    (void)cl;

    uint8_t mac_keycode = 0xFF;
    uint8_t mac_char = 0;
    uint16_t mod_bit = 0;

    if (keySym >= 0x20 && keySym <= 0x7E) {
        /* Printable ASCII — keySym IS the ASCII code */
        mac_keycode = ascii_to_mac_keycode[keySym & 0x7F];
        mac_char = (uint8_t)keySym;
    } else {
        /* Special keys (X11 keysym 0xFF00+ range) */
        switch (keySym) {
        case 0xFF08: mac_keycode = 0x33; mac_char = 0x08; break; /* Backspace */
        case 0xFF09: mac_keycode = 0x30; mac_char = 0x09; break; /* Tab */
        case 0xFF0D: mac_keycode = 0x24; mac_char = 0x0D; break; /* Return */
        case 0xFF1B: mac_keycode = 0x35; mac_char = 0x1B; break; /* Escape */
        case 0xFFFF: mac_keycode = 0x75; mac_char = 0x7F; break; /* Fwd Delete */
        case 0xFF51: mac_keycode = 0x7B; mac_char = 0x1C; break; /* Left */
        case 0xFF52: mac_keycode = 0x7E; mac_char = 0x1E; break; /* Up */
        case 0xFF53: mac_keycode = 0x7C; mac_char = 0x1D; break; /* Right */
        case 0xFF54: mac_keycode = 0x7D; mac_char = 0x1F; break; /* Down */
        /* Modifiers */
        case 0xFFE1: case 0xFFE2:          /* Shift L/R */
            mac_keycode = 0x38; mod_bit = MAC_MOD_SHIFT; break;
        case 0xFFE3: case 0xFFE4:          /* Control L/R */
            mac_keycode = 0x3B; mod_bit = MAC_MOD_CTRL; break;
        case 0xFFE5:                        /* Caps Lock */
            mac_keycode = 0x39; mod_bit = MAC_MOD_ALPHA; break;
        case 0xFFE7: case 0xFFE8:          /* Meta L/R → Command */
        case 0xFFEB: case 0xFFEC:          /* Super L/R → Command */
            mac_keycode = 0x37; mod_bit = MAC_MOD_CMD; break;
        case 0xFFE9: case 0xFFEA:          /* Alt L/R → Option */
            mac_keycode = 0x3A; mod_bit = MAC_MOD_OPT; break;
        default:
            return;  /* unmapped */
        }
    }

    if (mac_keycode == 0xFF)
        return;

    /* Update running modifier state */
    if (mod_bit) {
        if (down)
            vnc_cfg->key_modifiers |= mod_bit;
        else
            vnc_cfg->key_modifiers &= ~mod_bit;
    }

    /* Only inject keyDown — Mac apps ignore keyUp, and injecting both doubles input */
    if (!down)
        return;

    /* Enqueue key event for CPU thread */
    uint8_t head = vnc_cfg->key_head;
    uint8_t next = (head + 1) % VNC_KEY_QUEUE_SIZE;
    if (next == vnc_cfg->key_tail)
        return;  /* queue full — drop keystroke */

    struct vnc_key_event *ke = &vnc_cfg->key_queue[head];
    ke->mac_keycode = mac_keycode;
    ke->mac_char = mac_char;
    ke->down = down ? 1 : 0;
    ke->modifiers = vnc_cfg->key_modifiers;
    __sync_synchronize();
    vnc_cfg->key_head = next;
}

static void *vnc_thread(void *arg) {
    struct vnc_config *cfg = (struct vnc_config *)arg;

    int argc = 0;
    rfbScreenInfoPtr screen = rfbGetScreen(&argc, NULL,
                                           MAC_SCREEN_W, MAC_SCREEN_H,
                                           8,   /* bitsPerSample */
                                           1,   /* samplesPerPixel */
                                           1);  /* bytesPerPixel */
    if (!screen) {
        printf("[VNC] Failed to create rfb screen\n");
        cfg->running = 0;
        return NULL;
    }

    screen->desktopName = "PiStorm Mac SE";
    screen->port = cfg->port;
    screen->ipv6port = cfg->port;
    screen->alwaysShared = TRUE;

    /* VNC password auth — required by macOS Screen Sharing */
    static char *vnc_password[] = { "mac", NULL };
    screen->authPasswdData = (void *)vnc_password;
    screen->passwordCheck = rfbCheckPasswordByList;

    /* 8-bit grayscale: R=G=B, all shifts 0, max 255 */
    screen->serverFormat.redShift   = 0;
    screen->serverFormat.greenShift = 0;
    screen->serverFormat.blueShift  = 0;
    screen->serverFormat.redMax     = 255;
    screen->serverFormat.greenMax   = 255;
    screen->serverFormat.blueMax    = 255;

    screen->frameBuffer = malloc(MAC_SCREEN_W * MAC_SCREEN_H);
    if (!screen->frameBuffer) {
        printf("[VNC] Failed to allocate framebuffer\n");
        rfbScreenCleanup(screen);
        cfg->running = 0;
        return NULL;
    }
    memset(screen->frameBuffer, 0xFF, MAC_SCREEN_W * MAC_SCREEN_H);

    screen->ptrAddEvent = vnc_ptr_event;
    screen->kbdAddEvent = vnc_kbd_event;

    rfbInitServer(screen);
    printf("[VNC] Password: \"mac\"\n");
    printf("[VNC] Server listening on port %d (%dx%d 8bpp grayscale)\n",
           cfg->port, MAC_SCREEN_W, MAC_SCREEN_H);

    while (cfg->running && rfbIsActive(screen)) {
        if (screen->clientHead) {
            expand_screen(cfg->ram_base, cfg->ram_size, screen->frameBuffer);
            rfbMarkRectAsModified(screen, 0, 0, MAC_SCREEN_W, MAC_SCREEN_H);
        }
        rfbProcessEvents(screen, VNC_FRAME_MS * 1000);  /* timeout in microseconds */
    }

    printf("[VNC] Server shutting down\n");
    free(screen->frameBuffer);
    screen->frameBuffer = NULL;
    rfbScreenCleanup(screen);
    cfg->running = 0;
    return NULL;
}

void vnc_start(struct vnc_config *cfg) {
    if (!cfg || !cfg->enabled || cfg->running)
        return;

    vnc_cfg = cfg;
    cfg->running = 1;

    int err = pthread_create(&vnc_tid, NULL, &vnc_thread, cfg);
    if (err != 0) {
        printf("[VNC] Failed to create thread: %s\n", strerror(err));
        cfg->running = 0;
    } else {
        pthread_setname_np(vnc_tid, "pistorm: vnc");
        printf("[VNC] Thread started\n");
    }
}

void vnc_stop(void) {
    if (!vnc_cfg || !vnc_cfg->running)
        return;

    printf("[VNC] Stopping...\n");
    vnc_cfg->running = 0;
    pthread_join(vnc_tid, NULL);
    printf("[VNC] Stopped\n");
}
