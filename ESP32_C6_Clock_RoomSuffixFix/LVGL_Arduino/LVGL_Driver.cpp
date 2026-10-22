#include "LVGL_Driver.h"
#include <esp_timer.h>

// One 12-line buffer: 4,128 bytes, instead of two 5,504-byte buffers.
static lv_color_t pixels[LCD_WIDTH * 12];
static lv_disp_draw_buf_t drawBuffer;
static lv_disp_drv_t display;
static esp_timer_handle_t tickTimer;
static void flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *data) {
#if LV_COLOR_DEPTH != 16
#error "This panel driver requires LV_COLOR_DEPTH 16"
#endif
#if !LV_COLOR_16_SWAP
  // ST7789 expects the high byte first. The draw buffer can be reused after flush.
  size_t count = (size_t)(area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1);
  for (size_t i = 0; i < count; ++i) data[i].full = __builtin_bswap16(data[i].full);
#endif
  LCD_addWindow(area->x1, area->y1, area->x2, area->y2, (uint16_t *)data);
#if !LV_COLOR_16_SWAP
  // Keep LVGL's buffer in its original format for any subsequent partial redraw.
  for (size_t i = 0; i < count; ++i) data[i].full = __builtin_bswap16(data[i].full);
#endif
  lv_disp_flush_ready(driver);
}
static void tick(void *) { lv_tick_inc(5); }
void Lvgl_Init() {
  lv_init();
  lv_disp_draw_buf_init(&drawBuffer, pixels, nullptr, sizeof(pixels) / sizeof(pixels[0]));
  lv_disp_drv_init(&display);
  display.hor_res = LCD_WIDTH; display.ver_res = LCD_HEIGHT;
  display.flush_cb = flush; display.draw_buf = &drawBuffer;
  display.full_refresh = 0;
  lv_disp_drv_register(&display);
  esp_timer_create_args_t args = {};
  args.callback = tick; args.name = "lvgl_tick";
  esp_timer_create(&args, &tickTimer);
  esp_timer_start_periodic(tickTimer, 5000);
}
void Timer_Loop() { lv_timer_handler(); }
