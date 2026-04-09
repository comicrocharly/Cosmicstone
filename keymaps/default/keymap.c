#include QMK_KEYBOARD_H
#include "split_util.h"
#include "keyboards/bastardkb/charybdis/charybdis.h"

// Trackball custom keycodes
enum custom_keycodes {
    DPI_UP = SAFE_RANGE,
    DPI_DN,
    DPI_RST,
};


bool process_record_user(uint16_t keycode, keyrecord_t *record) {
   switch (keycode) {
    
        case LT(1, KC_1) ... LT(1, KC_0):
            if (record->tap.count == 0 && record->event.pressed) {
                
                uint16_t f_key = (keycode & 0xFF) - KC_1 + KC_F1;
                tap_code(f_key);
                return false;
            }
            break;

        case LT(1, KC_MINS):
            if (record->tap.count == 0 && record->event.pressed) {
                tap_code(KC_F11);
                return false;
            }
            break;

        case LT(1, KC_EQL):
            if (record->tap.count == 0 && record->event.pressed) {
                tap_code(KC_F12);
                return false;
            }
            break;
    
        case DPI_UP:
            if (record->event.pressed) {    
                charybdis_cycle_pointer_default_dpi(true);
            }
            return false;
        case DPI_DN:
            if (record->event.pressed) {
                charybdis_cycle_pointer_default_dpi(false);
            }
            return false;
        case DPI_RST:
            if (record->event.pressed) {
                charybdis_cycle_pointer_default_dpi(false);
                charybdis_cycle_pointer_default_dpi(false);
            }
            return false;

        case KC_TAB:
            if (record->event.pressed) {
                if (get_mods() & MOD_BIT(KC_RALT)) {
                    del_mods(MOD_BIT(KC_RALT));
                    add_mods(MOD_BIT(KC_LALT));
                    register_code(KC_TAB);
                    del_mods(MOD_BIT(KC_LALT));
                    add_mods(MOD_BIT(KC_RALT));
                    return false;
                }
            }
    
            return true;

    }
    return true;
}

// - Combos 
const uint16_t PROGMEM qmk_combo[] =  {MO(1), TG(2), KC_DEL, COMBO_END};
const uint16_t PROGMEM game_combo[] = {MO(1), TG(2), COMBO_END};

combo_t key_combos[] = {
    COMBO(qmk_combo, TG(4)),
    COMBO(game_combo, TG(3)),
};

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {

    //  LAYER 0 - BASE 
    [0] = LAYOUT(
        KC_ESC,  LT(1,KC_1), LT(1,KC_2),       LT(1,KC_3), LT(1,KC_4), LT(1,KC_5), LT(1,KC_6), LT(1,KC_7), LT(1,KC_8), LT(1,KC_9), LT(1,KC_0), LT(1,KC_MINS),
        KC_TAB,  KC_Q,       KC_W,             KC_E,       KC_R,       KC_T,       KC_Y,       KC_U,       KC_I,       KC_O,       KC_P,       LT(1,KC_EQL),
        KC_LSFT, KC_A,       KC_S,             KC_D,       KC_F,       KC_G,       KC_H,       KC_J,       KC_K,       KC_L,       KC_SCLN,    KC_QUOT,
        KC_LGUI, KC_Z,       KC_X,             KC_C,       KC_V,       KC_B,       KC_N,       KC_M,       KC_COMM,    KC_DOT,     KC_SLSH,    KC_DEL,
        KC_MUTE, MS_BTN1,    MT(MOD_LALT, KC_RALT), KC_LCTL,    KC_SPC,     KC_ENT,     KC_BSPC,    TG(2),      MO(1)
    ),

    //  LAYER 1 - NAVIGATION 
    [1] = LAYOUT(
        _______,              KC_F1,   KC_F2,   KC_F3,   KC_F4,    KC_F5,   KC_F6,   KC_F7,   KC_F8,   KC_F9,   KC_F10,  KC_F11,
        _______,              _______, _______, KC_UP,   _______,  _______, _______, _______, _______, _______, KC_PSCR, KC_F12,
        MT(MOD_LSFT,KC_CAPS), _______, KC_LEFT, KC_DOWN, KC_RIGHT, _______, _______, _______, _______, _______, _______, _______,
        _______,              _______, DRGSCRL, MS_BTN3, SNIPING,  _______, _______, _______, _______, _______, _______, _______,
        DPI_RST,              MS_BTN2, _______, _______, _______,  _______, _______, _______, _______
    ),

     // LAYER 2 - MEDIA 
    [2] = LAYOUT(
        _______, _______, _______, _______, _______,  _______, _______, KC_1,    KC_2,    KC_3,    _______, _______,
        _______, _______, _______, KC_UP,   _______,  _______, _______, KC_4,    KC_5,    KC_6,    _______, _______,
        _______, _______, KC_LEFT, KC_DOWN, KC_RIGHT, _______, _______, KC_7,    KC_8,    KC_9,    _______, _______,
        _______, _______, _______, _______, _______,  _______, _______, _______, KC_0,    _______, _______, _______,
        KC_MPLY, _______, _______, _______, _______,  _______, _______, _______, _______
    ),

    //  LAYER 3 - GAME 
    [3] = LAYOUT(
        _______, KC_1,    KC_2,    KC_3,    KC_4,    KC_5,    KC_6,    KC_7,    KC_8,    KC_9,    KC_0,    _______,
        _______, KC_T,    KC_Q,    KC_W,    KC_E,    KC_R,    _______, _______, _______, _______, _______, _______, 
        _______, KC_G,    KC_A,    KC_S,    KC_D,    KC_F,    _______, _______, _______, _______, _______, _______,
        _______, KC_B,    KC_Z,    KC_X,    KC_C,    KC_V,    _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______, _______, _______, _______
    ),

    //  LAYER 4 - QMK 
    [4] = LAYOUT(
        _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, QK_BOOT, _______,
        _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, QK_CLEAR_EEPROM, _______,
        _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______, _______, _______, _______
    ),
};

// - ENCODER 
static char encoder_status[16] = "               ";

bool encoder_update_user(uint8_t index, bool clockwise) {
    switch (get_highest_layer(layer_state)) {
        case 0:                               
            if (clockwise) {
                tap_code(KC_VOLU);
                snprintf(encoder_status, sizeof(encoder_status), "VOL+");
            } else {
                tap_code(KC_VOLD);
                snprintf(encoder_status, sizeof(encoder_status), "VOL-");
            }
            break;
        case 1:
            if (clockwise) {
                charybdis_cycle_pointer_default_dpi(true);
                snprintf(encoder_status, sizeof(encoder_status), "DPI+\n%4d", charybdis_get_pointer_default_dpi());
            } else {
                charybdis_cycle_pointer_default_dpi(false);
                snprintf(encoder_status, sizeof(encoder_status), "DPI-\n%4d", charybdis_get_pointer_default_dpi());
            }
            break;
        case 2:
            if (clockwise) {
                tap_code(KC_MNXT);
                snprintf(encoder_status, sizeof(encoder_status), "NEXT");
            } else {
                tap_code(KC_MPRV);
                snprintf(encoder_status, sizeof(encoder_status), "PREV");
            }
            break;
    }
    return false;
}

// - OLED
#ifdef OLED_ENABLE

oled_rotation_t oled_init_user(oled_rotation_t rotation) {
    return OLED_ROTATION_270;
}

bool oled_task_user(void) {
    if (is_keyboard_left()) {
        return false;
    }

    oled_write_P(PSTR("LYR: "), false);
    switch (get_highest_layer(layer_state)) {
        case 0: oled_write_P(PSTR("BASE "), false); break;
        case 1: oled_write_P(PSTR("NAV  "), false); break;
        case 2: oled_write_P(PSTR("MEDIA"), false); break;
        case 3: oled_write_P(PSTR("GAME "), false); break;
        case 4: oled_write_P(PSTR("QMK  "), false); break;
        default: oled_write_P(PSTR("???  "), false); break;
    }

    led_t led_state = host_keyboard_led_state();
    oled_write_P(led_state.caps_lock ? PSTR("\nCAP  ") : PSTR("     "), false);

    oled_advance_page(true);
    oled_advance_page(true);
    oled_advance_page(true);
    oled_advance_page(true);
    oled_advance_page(true);

    oled_write_P(PSTR("ENC:\n"), false);
    oled_write(encoder_status, false);

    return false;
}

#endif
