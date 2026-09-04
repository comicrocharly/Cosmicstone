//
// breakout.c
//
// Breakout - a portable Arkanoid-style easter-egg game for QMK OLED displays.
//
// The whole game lives in this file: state machine, fixed-step physics,
// input handling, scoring and OLED rendering. There is no board specific
// code: the display field is derived from the OLED driver geometry, and
// every gameplay parameter is overridable from the host keymap.
//
// Copyright (c) 2025
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "breakout.h"

#ifdef BREAKOUT_ENABLE

/* The OLED driver keeps the current rotation in a file-scope global.
 * It is not re-declared in the header, so declare it here. */
extern oled_rotation_t oled_rotation;

/* ------------------------------------------------------------------ */
/* Tunables (override from the host keymap via OPT_DEFS)              */
/* ------------------------------------------------------------------ */

#ifndef BREAKOUT_STEP_MS         // physics tick in ms (16 ~= 60 fps)
#define BREAKOUT_STEP_MS 16
#endif
#ifndef BREAKOUT_PADDLE_WIDTH
#define BREAKOUT_PADDLE_WIDTH 8
#endif
#ifndef BREAKOUT_PADDLE_SPEED    // key-based speed, px per tick
#define BREAKOUT_PADDLE_SPEED 1
#endif
#ifndef BREAKOUT_PADDLE_BOTTOM   // distance of the paddle from the field bottom
#define BREAKOUT_PADDLE_BOTTOM 4
#endif
#ifndef BREAKOUT_ENCODER_STEP    // paddle move per encoder notch
#define BREAKOUT_ENCODER_STEP 3
#endif
#ifndef BREAKOUT_BALL_SIZE
#define BREAKOUT_BALL_SIZE 2
#endif
#ifndef BREAKOUT_BALL_SLOW_LEVELS // levels 1..N run at half speed (gentle start)
#define BREAKOUT_BALL_SLOW_LEVELS 2
#endif
#ifndef BREAKOUT_BALL_SPEED      // base vertical speed (level 1), px per tick
#define BREAKOUT_BALL_SPEED 1
#endif
#ifndef BREAKOUT_BALL_SPEED_EVERY // levels per +1 px/tick speed-up
#define BREAKOUT_BALL_SPEED_EVERY 8
#endif
#ifndef BREAKOUT_BALL_SPEED_MAX   // speed cap
#define BREAKOUT_BALL_SPEED_MAX 3
#endif
#ifndef BREAKOUT_FLIP_Y           // 1: render with a vertically flipped Y axis
#define BREAKOUT_FLIP_Y 0
#endif
#ifndef BREAKOUT_BRICK_ROWS
#define BREAKOUT_BRICK_ROWS 5
#endif
#ifndef BREAKOUT_BRICK_COLS
#define BREAKOUT_BRICK_COLS 4
#endif
#ifndef BREAKOUT_BRICK_HEIGHT
#define BREAKOUT_BRICK_HEIGHT 3
#endif
#ifndef BREAKOUT_BRICK_TOP       // y of the first brick row (2 HUD rows + 1 empty gap row above)
#define BREAKOUT_BRICK_TOP 24
#endif
#ifndef BREAKOUT_START_LIVES
#define BREAKOUT_START_LIVES 3
#endif
#ifndef BREAKOUT_READY_MS        // ball sits on the paddle for this long
#define BREAKOUT_READY_MS 700
#endif
#ifndef BREAKOUT_PAUSE_MS        // pause shown between levels
#define BREAKOUT_PAUSE_MS 900
#endif
#ifndef BREAKOUT_RESYNC_MS        // full-frame re-flush period, 0 = off
#define BREAKOUT_RESYNC_MS 500
#endif
#ifndef BREAKOUT_HUD_BLINK_MS     // lives/level blink duration on update (2 blinks), 0 = off
#define BREAKOUT_HUD_BLINK_MS 500
#endif
#ifndef BREAKOUT_SPLASH_BLINK_MS  // splash "START" blink duration per phase (slow) - 0 = no blink
#define BREAKOUT_SPLASH_BLINK_MS 750
#endif
#ifndef BREAKOUT_BRICK_SCORE
#define BREAKOUT_BRICK_SCORE 10
#endif
#ifndef BREAKOUT_LEVEL_SCORE
#define BREAKOUT_LEVEL_SCORE 0
#endif
#ifndef BREAKOUT_COMBO_BONUS_PCT // per extra combo brick: % of the previous combo bricks' total points
#define BREAKOUT_COMBO_BONUS_PCT 10
#endif

#define BREAKOUT_BRICK_MAX (BREAKOUT_BRICK_ROWS * BREAKOUT_BRICK_COLS)
#define BREAKOUT_PADDLE_HEIGHT 2
#define BREAKOUT_HUD_HEIGHT (1 * OLED_FONT_HEIGHT) // level + lives row at the top (score is game-over only)

#if (BREAKOUT_BRICK_MAX > 32)
#error "Breakout: at most 32 bricks (BREAKOUT_BRICK_ROWS * BREAKOUT_BRICK_COLS <= 32)"
#endif

typedef enum {
    ST_IDLE = 0, // not running
    ST_SPLASH,   // title screen: big paddle + ball, "START" blinking
    ST_READY,    // ball glued to the paddle, about to launch
    ST_PLAY,     // ball in flight
    ST_PAUSE,    // short pause between levels
    ST_OVER,     // game-over screen, waiting for the exit key
} state_t;

/* ---------------------------- game state ---------------------------- */

static state_t state = ST_IDLE;
static uint32_t last_tick;
static uint32_t state_start;
static uint8_t field_w;
static uint8_t field_h;
static uint8_t paddle_x;
static int16_t ball_x, ball_y, ball_dx, ball_dy;
static uint8_t prev_ball_x, prev_ball_y;
static bool prev_ball_drawn;
static uint8_t prev_paddle_x;
static bool prev_paddle_drawn;
static uint8_t lives;
static uint8_t level;
static uint16_t score; // capped at 9999 (the game over screen shows 4 digits)
static uint32_t bricks; // bit n = brick (row * BREAKOUT_BRICK_COLS + col) is alive
static uint16_t combo_total; // points earned by the previous bricks of the current combo
static bool level_clean;    // no life lost since the current level started
static bool key_left, key_right;
static bool trigger_1, trigger_2;
static bool hud_dirty;
static uint32_t last_resync;
static uint8_t ball_phase; // tick parity counter for the half-speed start
static bool hud_blink_active; // lives/level counter blink in progress
static bool hud_blink_off;    // current blink phase (true: counter hidden)
static bool hud_blink_lives;  // true: lives counter, false: level counter
static uint32_t hud_blink_start;
static bool splash_on;        // splash "START" visible phase
static uint32_t splash_phase_start;

/* ---------------------------- helpers ---------------------------- */

static void field_init(void) {
#if defined(BREAKOUT_FIELD_WIDTH) && defined(BREAKOUT_FIELD_HEIGHT)
    field_w = BREAKOUT_FIELD_WIDTH;
    field_h = BREAKOUT_FIELD_HEIGHT;
#else
    // A 90 or 270 degree rotation swaps the display axes, so a 128x32 panel
    // mounted vertically yields a 32 wide x 128 tall field.
    if (oled_rotation == OLED_ROTATION_90 || oled_rotation == OLED_ROTATION_270) {
        field_w = OLED_DISPLAY_HEIGHT;
        field_h = OLED_DISPLAY_WIDTH;
    }
    else {
        field_w = OLED_DISPLAY_WIDTH;
        field_h = OLED_DISPLAY_HEIGHT;
    }
#endif
}

static uint8_t paddle_top(void) {
    return (field_h > (BREAKOUT_PADDLE_BOTTOM + BREAKOUT_PADDLE_HEIGHT)) ? (field_h - BREAKOUT_PADDLE_BOTTOM) : 0;
}

static uint8_t brick_width(void) {
    return field_w / BREAKOUT_BRICK_COLS;
}

/* The game logic works in "field space": y grows downwards, the HUD is at
 * the top, the paddle at the bottom. On some rotation settings the driver
 * maps y = 0 to the *physical bottom* of the panel, so the field would look
 * upside down. BREAKOUT_FLIP_Y mirrors the Y axis at render time only; the
 * physics never notice. */
static uint8_t yflip(uint8_t y, uint8_t h) {
#if BREAKOUT_FLIP_Y
    if (y + h > field_h) {
        return 0;
    }
    return (uint8_t)(field_h - y - h);
#else
    (void)h;
    return y;
#endif
}

/* The driver's text cursor sits on the font grid, not on pixels:
 * oled_set_cursor(col, line) counts col in characters (each
 * OLED_FONT_WIDTH px) and line in pages (each OLED_FONT_HEIGHT px rows),
 * while rect()/oled_write_pixel work in plain pixels. Text positions are
 * therefore quantized to the font grid; the helpers below convert from
 * field pixel space. */
static uint8_t page_of(uint8_t y) {
    return (uint8_t)(yflip(y, OLED_FONT_HEIGHT) / OLED_FONT_HEIGHT);
}

static void rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, bool on) {
    uint8_t sy = yflip(y, h);
    for (uint8_t dy = 0; dy < h; dy++) {
        for (uint8_t dx = 0; dx < w; dx++) {
            if ((x + dx) < field_w && (sy + dy) < field_h) {
                oled_write_pixel(x + dx, sy + dy, on);
            }
        }
    }
}

static void field_clear(void) {
    rect(0, 0, field_w, field_h, false);
}

static void ball_clear(void) {
    if (prev_ball_drawn) {
        if (prev_ball_y < BREAKOUT_HUD_HEIGHT) {
            hud_dirty = true; // the ball was over the HUD, redraw it
        }
        rect(prev_ball_x, prev_ball_y, BREAKOUT_BALL_SIZE, BREAKOUT_BALL_SIZE, false);
        prev_ball_drawn = false;
    }
}

static void ball_draw(void) {
    ball_clear();
    rect((uint8_t)ball_x, (uint8_t)ball_y, BREAKOUT_BALL_SIZE, BREAKOUT_BALL_SIZE, true);
    prev_ball_x = (uint8_t)ball_x;
    prev_ball_y = (uint8_t)ball_y;
    prev_ball_drawn = true;
}

static void paddle_clear(void) {
    if (prev_paddle_drawn) {
        rect(prev_paddle_x, paddle_top(), BREAKOUT_PADDLE_WIDTH, BREAKOUT_PADDLE_HEIGHT, false);
        prev_paddle_drawn = false;
    }
}

static void paddle_draw(void) {
    // Unconditional clear of the previous position + redraw, every tick.
    // This guarantees the old paddle image never lingers (no ghosting),
    // whatever the previous state was.
    paddle_clear();
    rect(paddle_x, paddle_top(), BREAKOUT_PADDLE_WIDTH, BREAKOUT_PADDLE_HEIGHT, true);
    prev_paddle_x = paddle_x;
    prev_paddle_drawn = true;
}

static void text_at(const char *str, uint8_t col, uint8_t y) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%s", str);
    oled_set_cursor(col, page_of(y));
    oled_write(buf, false);
}

/* The fixed, always-on HUD, a single row at the top:
 *   level ("L1", from the left) and lives ("x3", right aligned).
 * The score is only shown on the game-over screen.
 * No intermediate screens: the ball-on-paddle delay and the between-levels
 * pause are pure timers, nothing else is drawn. */
static void hud_draw(void) {
    char buf[8];
    // Level (left). Blinks (is hidden) while its own blink is in the off phase.
    snprintf(buf, sizeof(buf), "L%u", level);
    uint8_t lw = (uint8_t)(strlen(buf) * OLED_FONT_WIDTH);
    if (hud_blink_active && !hud_blink_lives && hud_blink_off) {
        rect(0, 0, lw, OLED_FONT_HEIGHT, false);
    }
    else {
        text_at(buf, 0, 0);
    }
    // Lives (right). Same blink rule for its own blink.
    snprintf(buf, sizeof(buf), "x%u", lives);
    uint8_t lw2 = (uint8_t)(strlen(buf) * OLED_FONT_WIDTH);
    uint8_t col = (field_w > lw2) ? (uint8_t)((field_w - lw2) / OLED_FONT_WIDTH) : 0;
    if (hud_blink_active && hud_blink_lives && hud_blink_off) {
        rect((uint8_t)(col * OLED_FONT_WIDTH), 0, lw2, OLED_FONT_HEIGHT, false);
    }
    else {
        text_at(buf, col, 0);
    }
}

/* Blink the lives or level counter twice (two on/off cycles) after an update. */
static void hud_blink(bool is_lives) {
    if (BREAKOUT_HUD_BLINK_MS == 0) {
        return;
    }
    hud_blink_lives = is_lives;
    hud_blink_start = timer_read();
    hud_blink_off = false;
    hud_blink_active = true;
    hud_dirty = true;
}

static void draw_bricks(void) {
    uint8_t bw = brick_width();
    for (uint8_t r = 0; r < BREAKOUT_BRICK_ROWS; r++) {
        for (uint8_t c = 0; c < BREAKOUT_BRICK_COLS; c++) {
            // Only the bricks that are still alive.
            if (!(bricks & (1u << (r * BREAKOUT_BRICK_COLS + c)))) {
                continue;
            }
            rect((uint8_t)(c * bw), (uint8_t)(BREAKOUT_BRICK_TOP + r * BREAKOUT_BRICK_HEIGHT), bw, BREAKOUT_BRICK_HEIGHT, true);
        }
    }
}

static void refill_bricks(void) {
    bricks = (BREAKOUT_BRICK_MAX >= 32) ? 0xFFFFFFFFu : ((1u << BREAKOUT_BRICK_MAX) - 1u);
    draw_bricks();
    level_clean = true; // a new level starts clean
    combo_total = 0;   // ...and with no combo
}

/* Pixel-exact text, for the game-over screen: the driver's text cursor sits
 * on the font grid (multiples of OLED_FONT_WIDTH), so a 24-px word like
 * "GAME" can never be truly centered in a 32-px field. The game therefore
 * carries the few glyphs it needs here (the same 6x8 font the driver ships,
 * LSB = top pixel) and draws them at plain pixel coordinates. */
#define BREAKOUT_PX_FONT_W 6
static const uint8_t px_font[] PROGMEM = { // '0'..'9', 'A','D','E','G','M','O','P','R','S','T','V'
    0x3E, 0x51, 0x49, 0x45, 0x3E, 0x00, // '0'
    0x00, 0x42, 0x7F, 0x40, 0x00, 0x00, // '1'
    0x72, 0x49, 0x49, 0x49, 0x46, 0x00, // '2'
    0x21, 0x41, 0x49, 0x4D, 0x33, 0x00, // '3'
    0x18, 0x14, 0x12, 0x7F, 0x10, 0x00, // '4'
    0x27, 0x45, 0x45, 0x45, 0x39, 0x00, // '5'
    0x3C, 0x4A, 0x49, 0x49, 0x31, 0x00, // '6'
    0x41, 0x21, 0x11, 0x09, 0x07, 0x00, // '7'
    0x36, 0x49, 0x49, 0x49, 0x36, 0x00, // '8'
    0x46, 0x49, 0x49, 0x29, 0x1E, 0x00, // '9'
    0x7C, 0x12, 0x11, 0x12, 0x7C, 0x00, // 'A'
    0x7F, 0x41, 0x41, 0x41, 0x3E, 0x00, // 'D'
    0x7F, 0x49, 0x49, 0x49, 0x41, 0x00, // 'E'
    0x3E, 0x41, 0x41, 0x51, 0x73, 0x00, // 'G'
    0x7F, 0x02, 0x1C, 0x02, 0x7F, 0x00, // 'M'
    0x3E, 0x41, 0x41, 0x41, 0x3E, 0x00, // 'O'
    0x7F, 0x09, 0x09, 0x09, 0x06, 0x00, // 'P'
    0x7F, 0x09, 0x19, 0x29, 0x46, 0x00, // 'R'
    0x26, 0x49, 0x49, 0x49, 0x32, 0x00, // 'S'
    0x03, 0x01, 0x7F, 0x01, 0x03, 0x00, // 'T'
    0x1F, 0x20, 0x40, 0x20, 0x1F, 0x00, // 'V'
};

static int16_t px_font_idx(char c) {
    if (c >= '0' && c <= '9') {
        return (int16_t)(c - '0');
    }
    switch (c) {
        case 'A': return 10;
        case 'D': return 11;
        case 'E': return 12;
        case 'G': return 13;
        case 'M': return 14;
        case 'O': return 15;
        case 'P': return 16;
        case 'R': return 17;
        case 'S': return 18;
        case 'T': return 19;
        case 'V': return 20;
        default: return -1;
    }
}

/* One pixel-exact character at (x, y). invert draws black-on-white.
 * Unknown characters are simply skipped. */
static void px_char(char c, uint8_t x, uint8_t y, bool invert) {
    int16_t gi = px_font_idx(c);
    if (gi < 0) {
        return;
    }
    uint8_t sy = yflip(y, OLED_FONT_HEIGHT);
    for (uint8_t cx = 0; cx < BREAKOUT_PX_FONT_W; cx++) {
        uint8_t col = pgm_read_byte(&px_font[(uint8_t)(gi * BREAKOUT_PX_FONT_W + cx)]);
        for (uint8_t ry = 0; ry < OLED_FONT_HEIGHT; ry++) {
            if ((x + cx) < field_w && (sy + ry) < field_h) {
                bool ink = ((col >> ry) & 1) != 0;
                oled_write_pixel((uint8_t)(x + cx), (uint8_t)(sy + ry), ink ^ invert);
            }
        }
    }
}

/* A whole word, pixel-exact, starting at (x, y). */
static void px_text(PGM_P str, uint8_t x, uint8_t y, bool invert) {
    for (uint8_t ci = 0;; ci++) {
        char c = (char)pgm_read_byte(str + ci);
        if (!c) {
            break;
        }
        px_char(c, (uint8_t)(x + ci * BREAKOUT_PX_FONT_W), y, invert);
    }
}

static void over_screen(void) {
    // Dedicated screen, top to bottom: the final score, GAME and OVER in
    // black-on-white (one white line on top, between the words, and on the
    // bottom), and the restart hint. Score and banners are drawn
    // pixel-exact, so they are truly centered in the field.
    uint8_t pages = (uint8_t)(field_h / OLED_FONT_HEIGHT);
    uint8_t base = (uint8_t)((pages - 4) / 2);
    uint8_t game_y = (uint8_t)((base + 1) * OLED_FONT_HEIGHT);
    uint8_t tw = (uint8_t)(4 * BREAKOUT_PX_FONT_W); // "GAME" / "OVER" width
    uint8_t tx = (field_w > tw) ? (uint8_t)((field_w - tw) / 2) : 0;
    field_clear();
    // Score, two pages above the banner (top of the block).
    uint8_t score_y = (uint8_t)(((base >= 2) ? (base - 2) : 0) * OLED_FONT_HEIGHT);
    uint16_t s = score;
    for (uint8_t i = 0; i < 4; i++) {
        // Digits are extracted least-significant first: draw them from
        // the rightmost column leftwards (units in the last column).
        px_char((char)('0' + (s % 10)), (uint8_t)(tx + (3 - i) * BREAKOUT_PX_FONT_W), score_y, false);
        s /= 10;
    }
    // One white rectangle around both words (1px margin all around: the
    // font's top row has ink, so the band provides the top white line; the
    // between/bottom lines are the font's always-blank bottom rows).
    uint8_t bx = (tx > 0) ? (uint8_t)(tx - 1) : 0;
    rect(bx, (uint8_t)(game_y - 1), (uint8_t)(tw + 2), (uint8_t)(2 * OLED_FONT_HEIGHT + 1), true);
    px_text(PSTR("GAME"), tx, game_y, true);
    px_text(PSTR("OVER"), tx, (uint8_t)(game_y + OLED_FONT_HEIGHT), true);
    // Restart hint, pixel-exact centered like the score and the banners.
    if ((base + 5) < pages) {
        uint8_t hw = (uint8_t)(5 * BREAKOUT_PX_FONT_W); // "PRESS"
        px_text(PSTR("PRESS"), (field_w > hw) ? (uint8_t)((field_w - hw) / 2) : 0, (uint8_t)((base + 5) * OLED_FONT_HEIGHT), false);
    }
    if ((base + 6) < pages) {
        px_text(PSTR("E"), (field_w > BREAKOUT_PX_FONT_W) ? (uint8_t)((field_w - BREAKOUT_PX_FONT_W) / 2) : 0, (uint8_t)((base + 6) * OLED_FONT_HEIGHT), false);
    }
}

/* Splash screen, shown when the game is first entered through the trigger
 * combo (never on in-game restarts). Top: a big version of the paddle with
 * the ball floating on top, centered. Bottom: "START", blinking slowly.
 * The in-game restart key starts the actual game. */
static uint8_t splash_start_y(void) {
    uint8_t pages = (uint8_t)(field_h / OLED_FONT_HEIGHT);
    return (uint8_t)((pages - pages / 4 - 1) * OLED_FONT_HEIGHT);
}

/* The "START" word, shown or hidden (slow blink). */
static void splash_text_set(bool on) {
    uint8_t sw = (uint8_t)(5 * BREAKOUT_PX_FONT_W); // "START"
    uint8_t sx = (field_w > sw) ? (uint8_t)((field_w - sw) / 2) : 0;
    uint8_t sy = splash_start_y();
    if (on) {
        px_text(PSTR("START"), sx, sy, false);
    }
    else {
        rect(sx, sy, sw, OLED_FONT_HEIGHT, false);
    }
}

static void splash_draw(void) {
    uint8_t pages = (uint8_t)(field_h / OLED_FONT_HEIGHT);
    uint8_t pw = (uint8_t)(BREAKOUT_PADDLE_WIDTH * 3); // big paddle
    uint8_t ph = (uint8_t)(BREAKOUT_PADDLE_HEIGHT * 3);
    uint8_t px = (field_w > pw) ? (uint8_t)((field_w - pw) / 2) : 0;
    uint8_t py = (uint8_t)((pages / 4) * OLED_FONT_HEIGHT); // top area
    uint8_t bs = (uint8_t)(BREAKOUT_BALL_SIZE * 3);
    uint8_t bx = (field_w > bs) ? (uint8_t)((field_w - bs) / 2) : 0;
    uint8_t by = (py > bs) ? (uint8_t)(py - bs) : 0;
    field_clear();
    rect(bx, by, bs, bs, true); // ball, floating in the middle
    rect(px, py, pw, ph, true); // big paddle
    splash_text_set(splash_on);
}

/* Add points without ever exceeding the 4-digit display cap. */
static void add_score(uint16_t v) {
    if (score < 10000) {
        score = (uint16_t)(score + v);
        if (score > 9999) {
            score = 9999;
        }
    }
}

/* ---------------------------- game flow ---------------------------- */

/* Common reset for a new game (score, lives, level, field). */
static void game_setup(void) {
    field_init();
    score = 0;
    lives = BREAKOUT_START_LIVES;
    level = 1;
    trigger_1 = trigger_2 = false;
    key_left = key_right = false;
    field_clear();
    paddle_clear();
    ball_clear();
    paddle_x = (field_w - BREAKOUT_PADDLE_WIDTH) / 2;
    refill_bricks();
    hud_blink_active = false;
    hud_dirty = true;
    ball_phase = 0;
    last_resync = timer_read();
}

static void game_start(void) {
    game_setup();
    state = ST_READY;
    state_start = timer_read();
    last_tick = state_start;
}

/* First entry through the trigger combo: the splash screen. */
static void splash_enter(void) {
    game_setup();
    hud_dirty = false; // the splash has no HUD
    splash_on = true;
    splash_phase_start = timer_read();
    state = ST_SPLASH;
    splash_draw();
}

static void game_exit(void) {
    state = ST_IDLE;
    trigger_1 = trigger_2 = false;
    key_left = key_right = false;
    ball_clear();
    paddle_clear();
    field_clear(); // let the host HUD redraw its own content
}

static void new_life(void) {
    ball_clear();
    ball_phase = 0;
    state = ST_READY;
    state_start = timer_read();
    ball_dx = 0;
    ball_dy = 0;
    ball_x = (int16_t)paddle_x + (BREAKOUT_PADDLE_WIDTH - BREAKOUT_BALL_SIZE) / 2;
    ball_y = (int16_t)paddle_top() - BREAKOUT_BALL_SIZE;
}

/* Ball vertical speed for the current level: base speed plus one px/tick
 * every BREAKOUT_BALL_SPEED_EVERY levels, capped. */
static uint8_t ball_speed(void) {
    uint8_t s = BREAKOUT_BALL_SPEED + (uint8_t)((level - 1) / BREAKOUT_BALL_SPEED_EVERY);
    if (s > BREAKOUT_BALL_SPEED_MAX) {
        s = BREAKOUT_BALL_SPEED_MAX;
    }
    return s;
}

static void step_paddle(void) {
    int16_t nx = (int16_t)paddle_x;
    if (key_left) {
        nx -= BREAKOUT_PADDLE_SPEED;
    }
    if (key_right) {
        nx += BREAKOUT_PADDLE_SPEED;
    }
    if (nx < 0) {
        nx = 0;
    }
    if (nx > (int16_t)field_w - BREAKOUT_PADDLE_WIDTH) {
        nx = (int16_t)field_w - BREAKOUT_PADDLE_WIDTH;
    }
    paddle_x = (uint8_t)nx;
}

static void brick_collision(void) {
    uint8_t bw = brick_width();
    for (uint8_t r = 0; r < BREAKOUT_BRICK_ROWS; r++) {
        int16_t ry = (int16_t)(BREAKOUT_BRICK_TOP + r * BREAKOUT_BRICK_HEIGHT);
        if (ball_y + BREAKOUT_BALL_SIZE <= ry || ball_y >= ry + BREAKOUT_BRICK_HEIGHT) {
            continue;
        }
        for (uint8_t c = 0; c < BREAKOUT_BRICK_COLS; c++) {
            uint32_t bit = 1u << (r * BREAKOUT_BRICK_COLS + c);
            if (!(bricks & bit)) {
                continue;
            }
            int16_t cx = (int16_t)(c * bw);
            if (ball_x + BREAKOUT_BALL_SIZE <= cx || ball_x >= cx + bw) {
                continue;
            }
            bricks &= ~bit;
            rect((uint8_t)cx, (uint8_t)ry, bw, BREAKOUT_BRICK_HEIGHT, false);
            // Brick: base score + combo bonus (a percentage of the total
            // points earned by the previous bricks of this combo).
            uint32_t pts = BREAKOUT_BRICK_SCORE + (uint32_t)combo_total * BREAKOUT_COMBO_BONUS_PCT / 100;
            add_score((uint16_t)pts);
            if (combo_total < 9999) {
                combo_total = (uint16_t)(combo_total + pts);
                if (combo_total > 9999) {
                    combo_total = 9999;
                }
            }
            hud_dirty = true;
            ball_dy = -ball_dy; // one bounce per tick
            if (bricks == 0) {
                // Level cleared without losing a life: bonus + one extra
                // life (capped at BREAKOUT_START_LIVES).
                if (level_clean) {
                    add_score(BREAKOUT_LEVEL_SCORE);
                    if (lives < BREAKOUT_START_LIVES) {
                        lives++;
                        hud_blink(true);
                    }
                }
                ball_clear();
                state = ST_PAUSE;
                state_start = timer_read();
            }
            return;
        }
    }
}

static void step_ball(void) {
    ball_x += ball_dx;
    ball_y += ball_dy;

    // Walls: left, right, top. The top wall is the bottom edge of the HUD,
    // so the ball never enters the area where the score and the lives
    // counter are drawn.
    if (ball_x <= 0) {
        ball_x = 0;
        if (ball_dx < 0) {
            ball_dx = -ball_dx;
        }
    }
    else if (ball_x + BREAKOUT_BALL_SIZE >= (int16_t)field_w) {
        ball_x = (int16_t)field_w - BREAKOUT_BALL_SIZE;
        if (ball_dx > 0) {
            ball_dx = -ball_dx;
        }
    }
    if (ball_y <= BREAKOUT_HUD_HEIGHT) {
        ball_y = BREAKOUT_HUD_HEIGHT;
        if (ball_dy < 0) {
            ball_dy = -ball_dy;
        }
    }

    brick_collision();
    if (state != ST_PLAY) {
        return;
    }

    // Paddle catch.
    int16_t py = (int16_t)paddle_top();
    if (ball_dy > 0 && ball_y + BREAKOUT_BALL_SIZE >= py && ball_y < py &&
        ball_x + BREAKOUT_BALL_SIZE > (int16_t)paddle_x &&
        ball_x < (int16_t)paddle_x + BREAKOUT_PADDLE_WIDTH) {
        ball_y = py - BREAKOUT_BALL_SIZE;
        ball_dy = -ball_speed();
        combo_total = 0; // the paddle touch resets the combo
        int16_t rel = (int16_t)(ball_x + BREAKOUT_BALL_SIZE / 2) - (int16_t)(paddle_x + BREAKOUT_PADDLE_WIDTH / 2);
        if (rel <= -(BREAKOUT_PADDLE_WIDTH / 3)) {
            ball_dx = -2;
        }
        else if (rel >= BREAKOUT_PADDLE_WIDTH / 3) {
            ball_dx = 2;
        }
        else {
            ball_dx = rel < 0 ? -1 : 1;
        }
    }

    // Bottom: lose a life.
    if (ball_y > (int16_t)field_h) {
        ball_clear();
        combo_total = 0;
        level_clean = false; // a life was lost: this level is no longer clean
        if (lives > 1) {
            lives--;
            hud_blink(true);
            new_life();
        }
        else {
            hud_blink_active = false; // no blinking on the game over screen
            hud_dirty = false;
            state = ST_OVER;
            state_start = timer_read();
            over_screen();
        }
    }
}

/* ---------------------------- public API ---------------------------- */

bool breakout_on_key_record(uint16_t keycode, keyrecord_t *record) {
    bool pressed = record->event.pressed;

    if (state == ST_IDLE) {
        // Idle: the two trigger keycodes are only *tracked*, never consumed,
        // so they keep their normal role on the trigger layer. The game
        // starts when both are held down together.
        if ((layer_state & (1u << BREAKOUT_TRIGGER_LAYER)) == 0) {
            return false;
        }
        if (keycode == BREAKOUT_TRIGGER_KEYCODE_1) {
            trigger_1 = pressed;
            if (trigger_1 && trigger_2) {
                splash_enter();
            }
            return false;
        }
        if (keycode == BREAKOUT_TRIGGER_KEYCODE_2) {
            trigger_2 = pressed;
            if (trigger_1 && trigger_2) {
                splash_enter();
            }
            return false;
        }
        return false;
    }

    // Game active: the trigger keycodes are reserved. Their presses are
    // consumed (K1 exits, K2 does nothing); their releases are passed to
    // the host, so keys that were pressed before the game started are
    // never left stuck on the host machine.
    if (keycode == BREAKOUT_TRIGGER_KEYCODE_1) {
        if (pressed) {
            game_exit(); // exit key
        }
        return pressed;
    }
    if (keycode == BREAKOUT_TRIGGER_KEYCODE_2) {
        return pressed;
    }
    if (keycode == BREAKOUT_RESTART_KEYCODE) {
        if (pressed) {
            game_start(); // restart key: new game from level 1, any time
        }
        return true;
    }
    if (keycode == BREAKOUT_KEY_LEFT) {
        key_left = pressed;
        return true;
    }
    if (keycode == BREAKOUT_KEY_RIGHT) {
        key_right = pressed;
        return true;
    }
    return false;
}

bool breakout_on_encoder(bool clockwise) {
    if (state != ST_READY && state != ST_PLAY) {
        return false;
    }
    int16_t nx = (int16_t)paddle_x + (clockwise ? BREAKOUT_ENCODER_STEP : -BREAKOUT_ENCODER_STEP);
    if (nx < 0) {
        nx = 0;
    }
    if (nx > (int16_t)field_w - BREAKOUT_PADDLE_WIDTH) {
        nx = (int16_t)field_w - BREAKOUT_PADDLE_WIDTH;
    }
    paddle_x = (uint8_t)nx;
    return true;
}

void breakout_on_housekeeping(void) {
    if (state == ST_IDLE) {
        return;
    }
    oled_on(); // keep the display awake while the game is on screen

    uint32_t now = timer_read();

    switch (state) {
    case ST_SPLASH:
        // "START" slow blink: flip the phase, repaint the text.
        if (BREAKOUT_SPLASH_BLINK_MS > 0 && timer_elapsed(splash_phase_start) >= BREAKOUT_SPLASH_BLINK_MS) {
            splash_phase_start = now;
            splash_on = !splash_on;
            splash_text_set(splash_on);
        }
        break;
    case ST_READY:
        if (timer_elapsed(last_tick) >= BREAKOUT_STEP_MS) {
            last_tick = now;
            step_paddle();
            // Ball glued to the paddle.
            ball_x = (int16_t)paddle_x + (BREAKOUT_PADDLE_WIDTH - BREAKOUT_BALL_SIZE) / 2;
            ball_y = (int16_t)paddle_top() - BREAKOUT_BALL_SIZE;
            if (timer_elapsed(state_start) >= BREAKOUT_READY_MS) {
                state = ST_PLAY;
                state_start = now;
                last_tick = now;
                ball_dy = -ball_speed();
                ball_dx = (level & 1) ? 1 : -1;
            }
        }
        paddle_draw();
        ball_draw();
        break;
    case ST_PLAY:
        if (timer_elapsed(last_tick) >= BREAKOUT_STEP_MS) {
            last_tick = now;
            step_paddle();
            // Gentle start: on the first levels the ball advances one px
            // every second tick (half the tick rate).
            ball_phase = (uint8_t)(ball_phase + 1);
            if ((level > BREAKOUT_BALL_SLOW_LEVELS) || ((ball_phase & 1u) == 0u)) {
                step_ball();
            }
        }
        paddle_draw();
        if (state == ST_PLAY) { // the step may have changed the state
            ball_draw();
        }
        break;
    case ST_PAUSE:
        if (timer_elapsed(state_start) >= BREAKOUT_PAUSE_MS) {
            level++;
            hud_blink(false);
            refill_bricks();
            new_life();
        }
        break;
    case ST_OVER:
    default:
        break;
    }

    // Keep the HUD readable: while the ball (or, on odd geometries, the
    // paddle) is inside the HUD area, redraw the text on top of the moving
    // elements on every tick so the score is never erased or covered.
    if ((state == ST_READY || state == ST_PLAY) &&
        (paddle_top() < BREAKOUT_HUD_HEIGHT || ball_y < BREAKOUT_HUD_HEIGHT)) {
        hud_dirty = true;
    }

    // Lives/level blink: two on/off cycles after each update.
    if (hud_blink_active) {
        uint32_t e = timer_elapsed(hud_blink_start);
        if (e >= BREAKOUT_HUD_BLINK_MS) {
            hud_blink_active = false;
            hud_blink_off = false;
            hud_dirty = true;
        }
        else {
            uint32_t q = BREAKOUT_HUD_BLINK_MS / 4;
            if (q == 0) {
                q = 1;
            }
            bool off = ((e / q) & 1u) != 0u;
            if (off != hud_blink_off) {
                hud_blink_off = off;
                hud_dirty = true;
            }
        }
    }

    if (hud_dirty) {
        hud_dirty = false;
        if (state != ST_OVER && state != ST_SPLASH) {
            hud_draw(); // the splash and the game over screen have no HUD rows
        }
    }

    // Periodic full re-flush: rewrite the whole current frame from scratch
    // into the driver buffer. Every touched block gets re-sent to the
    // panel, overwriting whatever stale or corrupted content might have
    // lingered there (this is what used to leave a "ghost" of the paddle at
    // its old position). Uses only the public OLED API, so it works on any
    // board. BREAKOUT_RESYNC_MS = 0 disables it.
    if (BREAKOUT_RESYNC_MS > 0 && timer_elapsed(last_resync) >= BREAKOUT_RESYNC_MS) {
        last_resync = now;
        field_clear();
        if (state == ST_READY || state == ST_PLAY) {
            draw_bricks();
            hud_draw();
            paddle_draw();
            ball_draw();
        }
        else if (state == ST_PAUSE) {
            draw_bricks();
            hud_draw();
            paddle_draw();
        }
        else if (state == ST_OVER) {
            over_screen();
        }
        else if (state == ST_SPLASH) {
            splash_draw();
        }
    }
}

bool breakout_on_oled(void) {
    return state != ST_IDLE;
}

bool breakout_is_active(void) {
    return state != ST_IDLE;
}

#endif // BREAKOUT_ENABLE
