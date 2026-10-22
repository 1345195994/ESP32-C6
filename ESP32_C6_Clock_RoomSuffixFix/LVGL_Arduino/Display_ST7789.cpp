#include "Display_ST7789.h"

static void command(uint8_t value) {
  digitalWrite(EXAMPLE_PIN_NUM_LCD_DC, LOW);
  SPI.transfer(value);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_DC, HIGH);
}
static void beginTransfer() {
  SPI.beginTransaction(SPISettings(80000000, MSBFIRST, SPI_MODE0));
  digitalWrite(EXAMPLE_PIN_NUM_LCD_CS, LOW);
}
static void endTransfer() {
  digitalWrite(EXAMPLE_PIN_NUM_LCD_CS, HIGH);
  SPI.endTransaction();
}
static void writeCommand(uint8_t cmd, const uint8_t *data = nullptr, uint8_t count = 0) {
  beginTransfer();
  command(cmd);
  if (count) SPI.transferBytes((uint8_t *)data, nullptr, count);
  endTransfer();
}
void Backlight_Init() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(EXAMPLE_PIN_NUM_BK_LIGHT, 1000, 10);
#else
  ledcSetup(0, 1000, 10);
  ledcAttachPin(EXAMPLE_PIN_NUM_BK_LIGHT, 0);
#endif
}
void Set_Backlight(uint8_t percent) {
  uint32_t duty = (percent > 100 ? 100 : percent) * 1023UL / 100UL;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(EXAMPLE_PIN_NUM_BK_LIGHT, duty);
#else
  ledcWrite(0, duty);
#endif
}
void LCD_Init() {
  pinMode(EXAMPLE_PIN_NUM_LCD_CS, OUTPUT);
  pinMode(EXAMPLE_PIN_NUM_LCD_DC, OUTPUT);
  pinMode(EXAMPLE_PIN_NUM_LCD_RST, OUTPUT);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_CS, HIGH);
  Backlight_Init(); Set_Backlight(0);
  SPI.begin(EXAMPLE_PIN_NUM_SCLK, EXAMPLE_PIN_NUM_MISO, EXAMPLE_PIN_NUM_MOSI);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_RST, HIGH); delay(50);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_RST, LOW); delay(50);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_RST, HIGH); delay(50);
  writeCommand(0x11); delay(120);
  struct Init { uint8_t cmd, count, data[14]; };
  static const Init sequence[] = {
    // MADCTL bit 3: this panel uses BGR. RGB mode displayed #FF0000 as blue.
    {0x36,1,{0x08}}, {0x3A,1,{0x05}}, {0xB0,2,{0x00,0xE8}},
    {0xB2,5,{0x0C,0x0C,0x00,0x33,0x33}}, {0xB7,1,{0x35}}, {0xBB,1,{0x35}},
    {0xC0,1,{0x2C}}, {0xC2,1,{0x01}}, {0xC3,1,{0x13}}, {0xC4,1,{0x20}},
    {0xC6,1,{0x0F}}, {0xD0,2,{0xA4,0xA1}}, {0xD6,1,{0xA1}},
    {0xE0,14,{0xF0,0x00,0x04,0x04,0x04,0x05,0x29,0x33,0x3E,0x38,0x12,0x12,0x28,0x30}},
    {0xE1,14,{0xF0,0x07,0x0A,0x0D,0x0B,0x07,0x28,0x33,0x3E,0x36,0x14,0x14,0x29,0x32}},
    {0x21,0,{0}}
  };
  for (const auto &entry : sequence) writeCommand(entry.cmd, entry.data, entry.count);
  writeCommand(0x11); delay(120); writeCommand(0x29);
}
// Coordinates use the panel's native portrait space; LVGL handles rotation.
static void cursor(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
  x1 += 34; x2 += 34;
  command(0x2A); SPI.transfer16(x1); SPI.transfer16(x2);
  command(0x2B); SPI.transfer16(y1); SPI.transfer16(y2);
  command(0x2C);
}
void LCD_SetCursor(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
  beginTransfer(); cursor(x1, y1, x2, y2); endTransfer();
}
void LCD_addWindow(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t *pixels) {
  if (x1 > x2 || y1 > y2 || x2 >= LCD_WIDTH || y2 >= LCD_HEIGHT) return;
  uint32_t bytes = (uint32_t)(x2 - x1 + 1) * (y2 - y1 + 1) * 2;
  beginTransfer(); cursor(x1, y1, x2, y2);
  // Write only: the old driver allocated a receive array as large as the refresh.
  SPI.transferBytes((uint8_t *)pixels, nullptr, bytes);
  endTransfer();
}
