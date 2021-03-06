/*
 * Copyright (c) 2020 Nick Winans <nick@winans.codes>
 *
 * SPDX-License-Identifier: MIT
 */

#include <device.h>
#include <init.h>
#include <kernel.h>

#include <math.h>
#include <stdlib.h>

#include <logging/log.h>

#include <drivers/led_strip.h>
#include <device.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define STRIP_LABEL		    DT_LABEL(DT_CHOSEN(zmk_underglow))
#define STRIP_NUM_PIXELS	DT_PROP(DT_CHOSEN(zmk_underglow), chain_length)

enum rgb_underglow_effect {
    UNDERGLOW_EFFECT_SOLID,
    UNDERGLOW_EFFECT_BREATHE,
    UNDERGLOW_EFFECT_SPECTRUM,
    UNDERGLOW_EFFECT_SWIRL,
    UNDERGLOW_EFFECT_NUMBER // Used to track number of underglow effects
};

struct led_hsb {
	u16_t h;
	u8_t  s;
	u8_t  b;
};

struct rgb_underglow_state {
    u16_t hue;
    u8_t  saturation;
    u8_t  brightness;
    u8_t  animation_speed;
    u8_t  current_effect;
    u16_t animation_step;
    bool  on;
};

struct rgb_underglow_state state;

struct device *led_strip;

struct led_rgb pixels[STRIP_NUM_PIXELS];

static struct led_rgb hsb_to_rgb(struct led_hsb hsb)
{
    double r, g, b;

    u8_t i = hsb.h / 60;
    double v = hsb.b / 100.0;
    double s = hsb.s / 100.0;
    double f = hsb.h / 360.0 * 6 - i;
    double p = v * (1 - s);
    double q = v * (1 - f * s);
    double t = v * (1 - (1 - f) * s);

    switch (i % 6)
    {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        case 5: r = v; g = p; b = q; break;
    }

    struct led_rgb rgb = { r: r*255, g: g*255, b: b*255 };

    return rgb;
}

static void zmk_rgb_underglow_effect_solid()
{
    for (int i=0; i<STRIP_NUM_PIXELS; i++)
    {
        int hue = state.hue;
        int sat = state.saturation;
        int brt = state.brightness;

        struct led_hsb hsb = { hue, sat, brt };

        pixels[i] = hsb_to_rgb(hsb);
    }
}

static void zmk_rgb_underglow_effect_breathe()
{
    for (int i=0; i<STRIP_NUM_PIXELS; i++)
    {
        int hue = state.hue;
        int sat = state.saturation;
        int brt = abs(state.animation_step - 1200) / 12;

        struct led_hsb hsb = { hue, sat, brt };

        pixels[i] = hsb_to_rgb(hsb);
    }

    state.animation_step += state.animation_speed * 10;
    
    if (state.animation_step > 2400) {
        state.animation_step = 0;
    }
}

static void zmk_rgb_underglow_effect_spectrum()
{
    for (int i=0; i<STRIP_NUM_PIXELS; i++)
    {
        int hue = state.animation_step;
        int sat = state.saturation;
        int brt = state.brightness;

        struct led_hsb hsb = { hue, sat, brt };

        pixels[i] = hsb_to_rgb(hsb);
    }

    state.animation_step += state.animation_speed;
    state.animation_step = state.animation_step % 360;
}

static void zmk_rgb_underglow_effect_swirl()
{
    for (int i=0; i<STRIP_NUM_PIXELS; i++)
    {
        int hue = (360 / STRIP_NUM_PIXELS * i + state.animation_step) % 360;
        int sat = state.saturation;
        int brt = state.brightness;

        struct led_hsb hsb = { hue, sat, brt };

        pixels[i] = hsb_to_rgb(hsb);
    }

    state.animation_step += state.animation_speed * 2;
    state.animation_step = state.animation_step % 360;
}

static void zmk_rgb_underglow_tick(struct k_work *work)
{
    switch (state.current_effect)
    {
        case UNDERGLOW_EFFECT_SOLID:
            zmk_rgb_underglow_effect_solid();
            break;
        case UNDERGLOW_EFFECT_BREATHE:
            zmk_rgb_underglow_effect_breathe();
            break;
        case UNDERGLOW_EFFECT_SPECTRUM:
            zmk_rgb_underglow_effect_spectrum();
            break;
        case UNDERGLOW_EFFECT_SWIRL:
            zmk_rgb_underglow_effect_swirl();
            break;
    }

    led_strip_update_rgb(led_strip, pixels, STRIP_NUM_PIXELS);
}

K_WORK_DEFINE(underglow_work, zmk_rgb_underglow_tick);

static void zmk_rgb_underglow_tick_handler(struct k_timer *timer)
{
    k_work_submit(&underglow_work);
}

K_TIMER_DEFINE(underglow_tick, zmk_rgb_underglow_tick_handler, NULL);

static int zmk_rgb_underglow_init(struct device *_arg)
{
	led_strip = device_get_binding(STRIP_LABEL);
	if (led_strip) {
		LOG_INF("Found LED strip device %s", STRIP_LABEL);
	} else {
		LOG_ERR("LED strip device %s not found", STRIP_LABEL);
		return -EINVAL;
	}

    state = (struct rgb_underglow_state){
        hue: 0,
        saturation: 100,
        brightness: 100,
        animation_speed: 3,
        current_effect: 0,
        animation_step: 0,
        on: true
    };

    k_timer_start(&underglow_tick, K_NO_WAIT, K_MSEC(50));

    return 0;
}

int zmk_rgb_underglow_cycle_effect(int direction)
{
    if (!led_strip) return -ENODEV;

    if (state.current_effect == 0 && direction < 0) {
        state.current_effect = UNDERGLOW_EFFECT_NUMBER - 1;
        return 0;
    }

    state.current_effect += direction;

    if (state.current_effect >= UNDERGLOW_EFFECT_NUMBER) {
        state.current_effect = 0;
    }
    
    state.animation_step = 0;

    return 0;
}

int zmk_rgb_underglow_toggle()
{
    if (!led_strip) return -ENODEV;

    state.on = !state.on;

    if (state.on) {
        state.animation_step = 0;
        k_timer_start(&underglow_tick, K_NO_WAIT, K_MSEC(50));
    } else {

        for (int i=0; i<STRIP_NUM_PIXELS; i++)
        {
            pixels[i] = (struct led_rgb){ r: 0, g: 0, b: 0};
        }

        led_strip_update_rgb(led_strip, pixels, STRIP_NUM_PIXELS);

        k_timer_stop(&underglow_tick);
    }

    return 0;
}

int zmk_rgb_underglow_change_hue(int direction)
{
    if (!led_strip) return -ENODEV;

    if (state.hue == 0 && direction < 0) {
        state.hue = 350;
        return 0;
    }
    
    state.hue += direction * CONFIG_ZMK_RGB_UNDERGLOW_HUE_STEP;

    if (state.hue > 350) {
        state.hue = 0;
    }

    return 0;
}

int zmk_rgb_underglow_change_sat(int direction)
{
    if (!led_strip) return -ENODEV;

    if (state.saturation == 0 && direction < 0) {
        return 0;
    }

    state.saturation += direction * CONFIG_ZMK_RGB_UNDERGLOW_SAT_STEP;

    if (state.saturation > 100) {
        state.saturation = 100;
    }

    return 0;
}

int zmk_rgb_underglow_change_brt(int direction)
{
    if (!led_strip) return -ENODEV;

    if (state.brightness == 0 && direction < 0) {
        return 0;
    }

    state.brightness += direction * CONFIG_ZMK_RGB_UNDERGLOW_BRT_STEP;

    if (state.brightness > 100) {
        state.brightness = 100;
    }

    return 0;
}

int zmk_rgb_underglow_change_spd(int direction)
{
    if (!led_strip) return -ENODEV;

    if (state.animation_speed == 1 && direction < 0) {
        return 0;
    }

    state.animation_speed += direction;

    if (state.animation_speed > 5) {
        state.animation_speed = 5;
    }

    return 0;
}

SYS_INIT(zmk_rgb_underglow_init,
        APPLICATION,
        CONFIG_APPLICATION_INIT_PRIORITY);
