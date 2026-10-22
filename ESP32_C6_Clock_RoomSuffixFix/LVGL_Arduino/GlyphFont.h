#pragma once
#include <lvgl.h>
#include <stdint.h>
#include <stddef.h>

// Browser-generated glyph subsets: 32-byte header, 16 bytes per glyph, 1-bit ink.
// Glyphs are stored at their final pixel size. The packet is never a whole label image.
static uint8_t *glyphPacket = nullptr;
static uint32_t glyphPacketKey = 0;
static const size_t GLYPH_LIMIT = 32768;
struct GlyphContext { lv_font_t font = {}; uint8_t group = 0; };
static GlyphContext glyphContexts[3];
static constexpr uint16_t glyphU16(const uint8_t *p) { return p[0] | (uint16_t)p[1] << 8; }
static constexpr uint32_t glyphU32(const uint8_t *p) { return glyphU16(p) | (uint32_t)glyphU16(p + 2) << 16; }
static const uint8_t *findGlyph(uint8_t group, uint32_t code) {
  if (!glyphPacket) return nullptr;
  int lo = 0, hi = glyphU16(glyphPacket + 4) - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    const uint8_t *g = glyphPacket + 32 + mid * 16;
    uint32_t current = glyphU32(g);
    if (g[14] == group && current == code) return g;
    if (g[14] < group || (g[14] == group && current < code)) lo = mid + 1;
    else hi = mid - 1;
  }
  return nullptr;
}
static bool glyphDescription(const lv_font_t *font, lv_font_glyph_dsc_t *out, uint32_t code, uint32_t) {
  uint8_t group = static_cast<const GlyphContext *>(font->dsc)->group;
  const uint8_t *g = findGlyph(group, code);
  if (!g) return false;
  *out = {};
  out->adv_w = glyphU16(g + 8); out->box_w = g[10]; out->box_h = g[11];
  out->ofs_x = (int8_t)g[12]; out->ofs_y = (int8_t)g[13]; out->bpp = 1;
  return true;
}
static const uint8_t *glyphBitmap(const lv_font_t *font, uint32_t code) {
  const uint8_t *g = findGlyph(static_cast<const GlyphContext *>(font->dsc)->group, code);
  return g ? glyphPacket + glyphU32(g + 4) : nullptr;
}
static constexpr bool validateGlyphPacket(const uint8_t *data, size_t bytes) {
  if (bytes < 32 || bytes > GLYPH_LIMIT || glyphU32(data) != 0x32504647 || data[6] != 3) return false;
  uint16_t count = glyphU16(data + 4);
  if (count > 768 || 32U + (size_t)count * 16 > bytes) return false;
  for (uint8_t i = 0; i < 3; ++i) {
    uint16_t height = glyphU16(data + 8 + i * 4), baseline = glyphU16(data + 10 + i * 4);
    if (height < 8 || height > 64 || baseline > height) return false;
  }
  uint8_t lastGroup = 0; uint32_t lastCode = 0;
  for (uint16_t i = 0; i < count; ++i) {
    const uint8_t *g = data + 32 + i * 16;
    uint32_t code = glyphU32(g), offset = glyphU32(g + 4);
    size_t length = ((size_t)g[10] * g[11] + 7) / 8;
    if (g[14] > 2 || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF) ||
        g[10] > 64 || g[11] > 64 || glyphU16(g + 8) > 96 ||
        offset < 32U + (size_t)count * 16 || offset > bytes || length > bytes - offset) return false;
    if (i && (g[14] < lastGroup || (g[14] == lastGroup && code <= lastCode))) return false;
    int baselineY = glyphU16(data + 8 + g[14] * 4) - glyphU16(data + 10 + g[14] * 4);
    int y = baselineY - g[11] - (int8_t)g[13];
    if (y < 0 || y + g[11] > glyphU16(data + 8 + g[14] * 4)) return false;
    lastGroup = g[14]; lastCode = code;
  }
  return true;
}
static const lv_font_t *uploadedFont(uint8_t group, const lv_font_t *fallback) {
  if (!glyphPacket) return fallback;
  GlyphContext &ctx = glyphContexts[group]; ctx.group = group;
  ctx.font.get_glyph_dsc = glyphDescription; ctx.font.get_glyph_bitmap = glyphBitmap;
  int height = glyphU16(glyphPacket + 8 + group * 4);
  int baseline = glyphU16(glyphPacket + 10 + group * 4);
  int ascent = LV_MAX(height - baseline, fallback->line_height - fallback->base_line);
  ctx.font.base_line = LV_MAX(baseline, fallback->base_line);
  ctx.font.line_height = ascent + ctx.font.base_line;
  ctx.font.dsc = &ctx; ctx.font.fallback = fallback;
  return &ctx.font;
}
