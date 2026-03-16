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

    rfbInitServer(screen);
    printf("[VNC] Password: \"mac\"\n");
    printf("[VNC] Server listening on port %d (%dx%d 8bpp grayscale)\n",
           cfg->port, MAC_SCREEN_W, MAC_SCREEN_H);

    while (cfg->running && rfbIsActive(screen)) {
        expand_screen(cfg->ram_base, cfg->ram_size, screen->frameBuffer);
        rfbMarkRectAsModified(screen, 0, 0, MAC_SCREEN_W, MAC_SCREEN_H);
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
