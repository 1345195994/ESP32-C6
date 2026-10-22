#pragma once
#include <Arduino.h>
#include <SPI.h>
#define LCD_WIDTH 172
#define LCD_HEIGHT 320
#define EXAMPLE_PIN_NUM_MISO 5
#define EXAMPLE_PIN_NUM_MOSI 6
#define EXAMPLE_PIN_NUM_SCLK 7
#define EXAMPLE_PIN_NUM_LCD_CS 14
#define EXAMPLE_PIN_NUM_LCD_DC 15
#define EXAMPLE_PIN_NUM_LCD_RST 21
#define EXAMPLE_PIN_NUM_BK_LIGHT 22
void LCD_Init();
void LCD_SetCursor(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
void LCD_addWindow(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t *pixels);
void Backlight_Init();
void Set_Backlight(uint8_t percent);
