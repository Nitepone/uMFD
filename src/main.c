/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsp/board.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include "tusb.h"

#include "usb_descriptors.h"

struct gpio_state {
    bool last;
    uint32_t hist;
};

struct phy_btn_reg {
    bool enabled_state;
    uint8_t gpio_id;
    uint32_t gpio_debounce_mask;
    struct gpio_state gpio_state;
};

static uint32_t blink_interval_ms = 1500;
uint32_t global_button_state = 0;
uint8_t global_hat_state = 0;

void led_blinking_task(void);
void hid_task(void);

void reg_btn(struct phy_btn_reg* btn_reg, uint8_t gpio_id, bool enabled_state) {
    if (!btn_reg) {
        return;
    }
    memset(btn_reg, 0, sizeof(*btn_reg));
    btn_reg->enabled_state = enabled_state;
    btn_reg->gpio_id = gpio_id;
    btn_reg->gpio_debounce_mask = 0x0f;
}

void poll_registered_gpios(struct phy_btn_reg* btn_arr, uint16_t btn_arr_len) {
    struct phy_btn_reg* btn = NULL;
    uint32_t raw_gpio = gpio_get_all();
    uint32_t gpio_hist_window = 0;

    for (btn = btn_arr; btn < (btn_arr + btn_arr_len); btn++) {
        // check if button is supposed to change
        btn->gpio_state.hist = (btn->gpio_state.hist << 1)
                | ((raw_gpio >> btn->gpio_id) & 0x1);
        gpio_hist_window = (btn->gpio_state.hist) & btn->gpio_debounce_mask;
        if ( gpio_hist_window == 0 || gpio_hist_window == btn->gpio_debounce_mask ) {
            if ((gpio_hist_window & 1) == btn->enabled_state) {
                global_button_state |= (1 << btn->gpio_id);
                blink_interval_ms = 1;
            }
            else {
                global_button_state &= ~(1 << btn->gpio_id);
                blink_interval_ms = 100;
            }
        }
    }
}

/*------------- MAIN -------------*/
int main(void) {
    struct phy_btn_reg btns[32];
    int btn_cnt = 0;
    stdio_init_all(); // XXX Is this needed?

    // Setup for Pimironi Unicorn Buttons
    gpio_pull_up(12);
    reg_btn(btns + btn_cnt, 12, 0);
    btn_cnt++;
    gpio_pull_up(13);
    reg_btn(btns + btn_cnt, 13, 0);
    btn_cnt++;
    gpio_pull_up(14);
    reg_btn(btns + btn_cnt, 14, 0);
    btn_cnt++;
    gpio_pull_up(15);
    reg_btn(btns + btn_cnt, 15, 0);
    btn_cnt++;

    board_init();
    tusb_init();

    while (1) {
        tud_task(); // tinyusb device task
        led_blinking_task();
        poll_registered_gpios(btns, btn_cnt);
        hid_task();
    }

    return 0;
}

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void) {
    return;
}

// Invoked when device is unmounted
void tud_umount_cb(void) {
    return;
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en) {
    (void)remote_wakeup_en;
    return;
}

// Invoked when usb bus is resumed
void tud_resume_cb(void) { blink_interval_ms = 1500; }

//--------------------------------------------------------------------+
// USB HID
//--------------------------------------------------------------------+

static int send_hid_report() {
    // skip if hid is not ready yet
    if (!tud_hid_ready()) {
        return -1;
    }

    hid_gamepad_report_t report = {.x = 50,
                                   .y = 0,
                                   .z = 0,
                                   .rz = 0,
                                   .rx = 0,
                                   .ry = 0,
                                   .hat = global_hat_state,
                                   .buttons = global_button_state};
    tud_hid_report(REPORT_ID_GAMEPAD, &report, sizeof(report));
}

// Every 1ms, we will sent 1 report for each HID profile (keyboard, mouse etc
// ..) tud_hid_report_complete_cb() is used to send the next report after
// previous one is complete
void hid_task(void) {
    const uint32_t interval_ms = 1;
    static uint32_t start_ms = 0;

    if (board_millis() - start_ms < interval_ms)
        return; // not enough time
    start_ms += interval_ms;

    send_hid_report();
}

// Invoked when sent REPORT successfully to host
// Application can use this to send the next report
// Note: For composite reports, report[0] is report ID
void tud_hid_report_complete_cb(uint8_t instance, uint8_t const *report,
                                uint8_t len) {
    (void)instance;
    (void)len;

    uint8_t next_report_id = report[0] + 1;

    if (next_report_id < REPORT_ID_COUNT) {
        send_hid_report(next_report_id, board_button_read());
    }
}

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen) {
    // TODO not Implemented
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;

    return 0;
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type, uint8_t const *buffer,
                           uint16_t bufsize) {
    (void)instance;

    if (report_type == HID_REPORT_TYPE_OUTPUT) {
        // Set keyboard LED e.g Capslock, Numlock etc...
        if (report_id == REPORT_ID_KEYBOARD) {
            // bufsize should be (at least) 1
            if (bufsize < 1)
                return;

            uint8_t const kbd_leds = buffer[0];

            if (kbd_leds & KEYBOARD_LED_CAPSLOCK) {
                // Capslock On: disable blink, turn led on
                blink_interval_ms = 0;
                board_led_write(true);
            } else {
                // Caplocks Off: back to normal blink
                board_led_write(false);
                blink_interval_ms = 1500;
            }
        }
    }
}

//--------------------------------------------------------------------+
// BLINKING TASK
//--------------------------------------------------------------------+
void led_blinking_task(void) {
    static uint32_t start_ms = 0;
    static bool led_state = false;

    // blink is disabled
    if (!blink_interval_ms)
        return;

    // Blink every interval ms
    if (board_millis() - start_ms < blink_interval_ms)
        return; // not enough time
    start_ms += blink_interval_ms;

    board_led_write(led_state);
    led_state = 1 - led_state; // toggle
}
