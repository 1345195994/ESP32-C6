#pragma once
#include <lvgl.h>
// Target-size glyphs in flash; no interpolation or full-label transform layer.
const lv_font_t *clockNativeFont(uint8_t requested);
const lv_font_t *networkNativeFont();
const lv_font_t *dateNativeFont(bool landscape);
const lv_font_t *bodyNativeFont(uint8_t requested);
