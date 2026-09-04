//
// breakout.h
//
// Breakout - a portable Arkanoid-style easter-egg game for QMK OLED displays.
//
// Copyright (c) 2025
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Integration in three steps (full details in README.md):
//
//   1. Copy breakout.c and breakout.h into your keymap folder.
//   2. Enable and configure the game from your keymap rules.mk:
//        OPT_DEFS += -DBREAKOUT_ENABLE
//        OPT_DEFS += -DBREAKOUT_TRIGGER_LAYER=2
//        OPT_DEFS += -DBREAKOUT_TRIGGER_KEYCODE_1=KC_ESC
//        OPT_DEFS += -DBREAKOUT_TRIGGER_KEYCODE_2=KC_DEL
//        OPT_DEFS += -DBREAKOUT_RESTART_KEYCODE=KC_E
//   3. Wire the four hooks into your keymap:
//        process_record_user()    -> if (breakout_on_key_record(keycode, record)) return false;
//        encoder_update_user()    -> if (breakout_on_encoder(clockwise)) { /* skip normal action */ }
//        housekeeping_task_user() -> breakout_on_housekeeping();
//        oled_task_user()         -> if (breakout_on_oled()) return false;
//
// When the game is not running, every hook is a no-op, so a host keymap
// behaves exactly as before.
//

#pragma once

#ifdef BREAKOUT_ENABLE

#include QMK_KEYBOARD_H
#include "oled_driver.h"

/* ------------------------------------------------------------------ */
/* Configuration - override from the host keymap (rules.mk OPT_DEFS).  */
/* ------------------------------------------------------------------ */

/* Layer on which the two trigger keycodes must be reachable. */
#ifndef BREAKOUT_TRIGGER_LAYER
#define BREAKOUT_TRIGGER_LAYER 2
#endif

/* The two keycodes that, pressed together while on the trigger layer,
 * start the game. The first one is also the in-game EXIT key.
 * NOTE: these are keycodes, so they must be mapped on the trigger layer
 * of the host keymap (any physical key can produce them). */
#ifndef BREAKOUT_TRIGGER_KEYCODE_1
#define BREAKOUT_TRIGGER_KEYCODE_1 KC_ESC
#endif
#ifndef BREAKOUT_TRIGGER_KEYCODE_2
#define BREAKOUT_TRIGGER_KEYCODE_2 KC_DEL
#endif

/* In-game RESTART key: a new game from level 1, any time (including the
 * game-over screen). Consumed while the game is running. */
#ifndef BREAKOUT_RESTART_KEYCODE
#define BREAKOUT_RESTART_KEYCODE KC_E
#endif

/* Paddle movement keys (consumed while the game is running). */
#ifndef BREAKOUT_KEY_LEFT
#define BREAKOUT_KEY_LEFT KC_S
#endif
#ifndef BREAKOUT_KEY_RIGHT
#define BREAKOUT_KEY_RIGHT KC_F
#endif

/* Field size override. By default the field is derived from the display
 * geometry and the current OLED rotation (a 128x32 panel mounted
 * vertically gives a 32 x 128 field). Define both to force the size, e.g.
 * for QMK versions where the driver rotation global is not accessible. */
/* #define BREAKOUT_FIELD_WIDTH  32 */
/* #define BREAKOUT_FIELD_HEIGHT 128 */

/* ---------------------------- public API ---------------------------- */

/* Call at the very top of process_record_user().
 * Returns true if the key was consumed by the game (host should
 * `return false` immediately). */
bool breakout_on_key_record(uint16_t keycode, keyrecord_t *record);

/* Call from encoder_update_user(). Returns true if the encoder event was
 * consumed by the game (paddle moved). */
bool breakout_on_encoder(bool clockwise);

/* Call from housekeeping_task_user(). Runs the fixed-step physics/render. */
void breakout_on_housekeeping(void);

/* Call from oled_task_user(). Returns true while the game is on screen;
 * the host should skip its own drawing and still `return false` so the
 * driver flushes the framebuffer. */
bool breakout_on_oled(void);

/* Convenience: true while the game is running (any state). */
bool breakout_is_active(void);

#endif // BREAKOUT_ENABLE
