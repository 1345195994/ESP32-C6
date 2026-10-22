#pragma once
#include <lvgl.h>
#include <string.h>

// Scale one glyph at a time. Never allocate an offscreen layer for a whole label.
struct ScaledFont {
  lv_font_t font = {};
  const lv_font_t *base = nullptr;
  uint8_t pixels = 16;
};
static uint8_t scaledGlyph[48 * 64 / 2];

static int scaleMetric(int value, const ScaledFont *s) {
  const int height = s->base->line_height;
  return value < 0 ? -((-value * s->pixels + height / 2) / height)
                   : (value * s->pixels + height / 2) / height;
}

static bool sourceGlyph(const ScaledFont *s, uint32_t &code,
                        lv_font_glyph_dsc_t &d, uint32_t next = 0) {
  if (lv_font_get_glyph_dsc(s->base, &d, code, next) && !d.is_placeholder) return true;
  code = '?';
  return lv_font_get_glyph_dsc(s->base, &d, code, 0) && !d.is_placeholder;
}

static bool scaledDescription(const lv_font_t *font, lv_font_glyph_dsc_t *out,
                              uint32_t code, uint32_t next) {
  const ScaledFont *s = static_cast<const ScaledFont *>(font->dsc);
  lv_font_glyph_dsc_t d = {};
  if (!sourceGlyph(s, code, d, next)) return false;
  *out = d;
  out->adv_w = scaleMetric(d.adv_w, s);
  out->box_w = d.box_w ? LV_MAX(1, scaleMetric(d.box_w, s)) : 0;
  out->box_h = d.box_h ? LV_MAX(1, scaleMetric(d.box_h, s)) : 0;
  out->ofs_x = scaleMetric(d.ofs_x, s);
  out->ofs_y = scaleMetric(d.ofs_y, s);
  out->bpp = 4;
  out->is_placeholder = false;
  return true;
}

static const uint8_t *scaledBitmap(const lv_font_t *font, uint32_t code) {
  const ScaledFont *s = static_cast<const ScaledFont *>(font->dsc);
  lv_font_glyph_dsc_t d = {}, dst = {};
  if (!sourceGlyph(s, code, d) || !d.box_w || !d.box_h) return nullptr;
  const uint8_t *source = lv_font_get_glyph_bitmap(d.resolved_font, code);
  if (!source || !(d.bpp == 1 || d.bpp == 2 || d.bpp == 4 || d.bpp == 8)) return nullptr;
  scaledDescription(font, &dst, code, 0);
  size_t bytes = ((size_t)dst.box_w * dst.box_h + 1) / 2;
  if (bytes > sizeof(scaledGlyph)) return nullptr;
  memset(scaledGlyph, 0, bytes);
  const uint16_t mask = (1U << d.bpp) - 1;
  for (uint16_t y = 0; y < dst.box_h; ++y) {
    const uint16_t sy = (uint32_t)y * d.box_h / dst.box_h;
    for (uint16_t x = 0; x < dst.box_w; ++x) {
      const uint16_t sx = (uint32_t)x * d.box_w / dst.box_w;
      size_t bit = ((size_t)sy * d.box_w + sx) * d.bpp;
      uint8_t alpha = ((source[bit / 8] >> (8 - d.bpp - bit % 8)) & mask) * 15 / mask;
      size_t pixel = (size_t)y * dst.box_w + x;
      scaledGlyph[pixel / 2] |= alpha << ((pixel & 1) ? 0 : 4);
    }
  }
  return scaledGlyph;
}

static void configureFont(ScaledFont &s, const lv_font_t *base, uint8_t pixels) {
  s.base = base;
  s.pixels = pixels;
  s.font.get_glyph_dsc = scaledDescription;
  s.font.get_glyph_bitmap = scaledBitmap;
  s.font.line_height = pixels;
  s.font.base_line = scaleMetric(base->base_line, &s);
  s.font.underline_position = scaleMetric(base->underline_position, &s);
  s.font.underline_thickness = LV_MAX(1, scaleMetric(base->underline_thickness, &s));
  s.font.dsc = &s;
}
