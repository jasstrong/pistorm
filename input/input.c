// SPDX-License-Identifier: MIT

#include <linux/input.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "platforms/platforms.h"
#include "input.h"

#define NONE 0x80

static int lshift = 0, rshift = 0,/* lctrl = 0, rctrl = 0,*/ lalt = 0, altgr = 0, capslk = 0;
extern int mouse_fd;
extern int keyboard_fd;

int handle_modifier(struct input_event *ev) {
  int *target_modifier = NULL;
  if (ev->value != KEYPRESS_REPEAT && (ev->code == KEY_LEFTSHIFT || ev->code == KEY_RIGHTSHIFT || ev->code == KEY_LEFTALT || ev->code == KEY_RIGHTALT || ev->code == KEY_LEFTCTRL || ev->code == KEY_RIGHTCTRL)) {
    switch(ev->code) {
      case KEY_LEFTSHIFT:
        target_modifier = &lshift;
        break;
      case KEY_RIGHTSHIFT:
        target_modifier = &rshift;
        break;
      case KEY_LEFTALT:
        target_modifier = &lalt;
        break;
      case KEY_RIGHTALT:
        target_modifier = &altgr;
        break;
      case KEY_LEFTCTRL:
        target_modifier = &lshift;
        break;
      case KEY_RIGHTCTRL:
        target_modifier = &rshift;
        break;
    }
    *target_modifier = (ev->value == KEYPRESS_RELEASE) ? 0 : 1;
    return 1;
  } else if (ev->value == KEYPRESS_PRESS && ev->code == KEY_CAPSLOCK) {
    capslk = !capslk;
    return 1;
  } else {
    return 0;
  }
}

#define KEYCASE(a, b, c)case a: return (((lshift || rshift) && !capslk) || (!(lshift || rshift) && capslk)) ? (c) : (b);

/**
 * translates keycodes back into a simpler enumerable value for handling emulator command events
 *
 * @param *struct/input_event  ev  pointer to input layer event structure
 * @return char
 */
char char_from_input_event(struct input_event *ev) {
  switch(ev->code) {
    KEYCASE(KEY_A, 'a', 'A');
    KEYCASE(KEY_B, 'b', 'B');
    KEYCASE(KEY_C, 'c', 'C');
    KEYCASE(KEY_D, 'd', 'D');
    KEYCASE(KEY_E, 'e', 'E');
    KEYCASE(KEY_F, 'f', 'F');
    KEYCASE(KEY_G, 'g', 'G');
    KEYCASE(KEY_H, 'h', 'H');
    KEYCASE(KEY_I, 'i', 'I');
    KEYCASE(KEY_J, 'j', 'J');
    KEYCASE(KEY_K, 'k', 'K');
    KEYCASE(KEY_L, 'l', 'L');
    KEYCASE(KEY_M, 'm', 'M');
    KEYCASE(KEY_N, 'n', 'N');
    KEYCASE(KEY_O, 'o', 'O');
    KEYCASE(KEY_P, 'p', 'P');
    KEYCASE(KEY_Q, 'q', 'Q');
    KEYCASE(KEY_R, 'r', 'R');
    KEYCASE(KEY_S, 's', 'S');
    KEYCASE(KEY_T, 't', 'T');
    KEYCASE(KEY_U, 'u', 'U');
    KEYCASE(KEY_V, 'v', 'V');
    KEYCASE(KEY_W, 'w', 'W');
    KEYCASE(KEY_X, 'x', 'X');
    KEYCASE(KEY_Y, 'y', 'Y');
    KEYCASE(KEY_Z, 'z', 'Z');
    KEYCASE(KEY_1, '1', '!');
    KEYCASE(KEY_2, '2', '@');
    KEYCASE(KEY_3, '3', '#');
    KEYCASE(KEY_4, '4', '$');
    KEYCASE(KEY_5, '5', '%');
    KEYCASE(KEY_6, '6', '^');
    KEYCASE(KEY_7, '7', '&');
    KEYCASE(KEY_8, '8', '*');
    KEYCASE(KEY_9, '9', '(');
    KEYCASE(KEY_0, '0', ')');
    KEYCASE(KEY_F12, 0x1B, 0x1B);
    KEYCASE(KEY_PAUSE, 0x01, 0x01);
    default:
      return 0;
  }
}

int get_key_char(char *c, char *code, char *event_type)
{
  if (keyboard_fd == -1)
    return 0;

  struct input_event ie;
  while(read(keyboard_fd, &ie, sizeof(struct input_event)) != -1) {
    if (ie.type == EV_KEY) {
      handle_modifier(&ie);
      char ret = char_from_input_event(&ie);
      *c = ret;
      *code = ie.code;
      *event_type = ie.value;
      return 1;
    }
  }

  return 0;
}

uint8_t mouse_x = 0, mouse_y = 0;

int get_mouse_status(uint8_t *x, uint8_t *y, uint8_t *b, uint8_t *e) {
  uint8_t mouse_ev[4];
  if (read(mouse_fd, &mouse_ev, 4) != -1) {
    *b = ((uint8_t *)&mouse_ev)[0];
    *e = ((uint8_t *)&mouse_ev)[3];

    mouse_x += ((uint8_t *)&mouse_ev)[1];
    *x = mouse_x;
    mouse_y += (-((uint8_t *)&mouse_ev)[2]);
    *y = mouse_y;
    return 1;
  }

  return 0;
}

static uint8_t queued_keypresses = 0, queue_output_pos = 0, queue_input_pos = 0;
static uint8_t queued_keys[256];
static uint8_t queued_events[256];

void clear_keypress_queue() {
  memset(queued_keys, 0x80, 256);
  memset(queued_events, 0x80, 256);
  queued_keypresses = 0;
  queue_output_pos = 0;
  queue_input_pos = 0;
}

int queue_keypress(uint8_t keycode, uint8_t event_type, uint8_t platform) {
  (void)platform;
  char *keymap = NULL;
  if (keymap != NULL) {
    if (keymap[keycode] != NONE) {
      if (queued_keypresses < 255) {
        // printf("Keypress queued, matched %.2X to host key code %.2X\n", keycode, keymap[keycode]);
        if (keycode == KEY_CAPSLOCK) {
          if (event_type == KEYPRESS_RELEASE) {
            return 0;
          }
          event_type = capslk;
        }
        queued_keys[queue_output_pos] = keymap[keycode];
        queued_events[queue_output_pos] = event_type;
        queue_output_pos++;
        queued_keypresses++;
        return 1;
      }
    }
  }
  return 0;
}

int get_num_kb_queued() {
  return queued_keypresses;
}

void pop_queued_key(uint8_t *c, uint8_t *t) {
  if (queued_keypresses == 0) {
    *c = NONE;
    *t = NONE;
    return;
  }
  *c = queued_keys[queue_input_pos];
  *t = queued_events[queue_input_pos];
  queue_input_pos++;
  queued_keypresses--;
  return;
}

int grab_device(int fd) {
  int rc = 0;
  rc = ioctl(fd, EVIOCGRAB, (void *)1);
  return rc;
}

int release_device(int fd) {
  int rc = 0;
  rc = ioctl(fd, EVIOCGRAB, (void *)0);
  return rc;
}
