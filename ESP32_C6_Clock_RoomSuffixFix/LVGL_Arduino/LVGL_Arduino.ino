#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <esp_sntp.h>
#include <time.h>
#include <new>
#include "Display_ST7789.h"
#include "LVGL_Driver.h"
#include "ScaledFont.h"
#include "NativeFonts.h"
#include "GlyphFont.h"
#include "WebUI.h"
#include "UiMessages.h"

// Fill in your 2.4 GHz Wi-Fi credentials.
const char *WIFI_SSID = "GL-MT3600BE-369";
const char *WIFI_PASSWORD = "zhhebzybe7xggehs";

enum { BG, CLOCK, DATE, NET, IP, P2TIME, P2DATE, BODY, P3TIME, P3TEXT, FOOTER, COLOR_COUNT };
struct Settings {
  uint32_t marker = 0x434C4B32;
  uint8_t landscape = 1, brightness = 40, autoCarousel = 0, carouselSeconds = 5;
  uint8_t page2Enabled = 1, page3Enabled = 1, largeTimeSize = 32, smallTimeSize = 18;
  uint8_t scheduleTextSize = 20, thirdTimeSize = 24, thirdTextSize = 18;
  uint8_t thirdTimeFormat = 0, marqueeMode = 0, rainbowText = 0;
  uint32_t color[COLOR_COUNT] = {0x08111F,0xF3F7FC,0xA9BDD3,0x39D0B0,0xA9BDD3,
    0xF3F7FC,0xA9BDD3,0xF3F7FC,0xF3F7FC,0xF3F7FC,0x7188A3};
};
struct NumberField {
  const char *name;
  uint8_t Settings::*member;
  uint8_t minimum, maximum;
  const char *legacy;
};
const NumberField numberFields[] = {
  {"landscape",&Settings::landscape,0,1,"landscape"},
  {"brightness",&Settings::brightness,10,50,"bright"},
  {"autoCarousel",&Settings::autoCarousel,0,1,"carousel"},
  {"carouselSeconds",&Settings::carouselSeconds,2,60,"carSec"},
  {"page2Enabled",&Settings::page2Enabled,0,1,"p2enabled"},
  {"page3Enabled",&Settings::page3Enabled,0,1,"p3enabled"},
  {"largeTimeSize",&Settings::largeTimeSize,16,48,"largeSize"},
  {"smallTimeSize",&Settings::smallTimeSize,12,30,"smallSize"},
  {"scheduleTextSize",&Settings::scheduleTextSize,10,24,"schedSize"},
  {"thirdTimeSize",&Settings::thirdTimeSize,12,42,"thirdSize"},
  {"thirdTextSize",&Settings::thirdTextSize,10,30,"textSize"},
  {"thirdTimeFormat",&Settings::thirdTimeFormat,0,2,"timeFmt"},
  {"marqueeMode",&Settings::marqueeMode,0,7,"scrollMode"},
  {"rainbowText",&Settings::rainbowText,0,1,"rainbow"}
};
const char *colorNames[] = {"bg","clock","date","network","ipColor","page2TimeColor",
  "page2DateColor","messageColor","page3TimeColor","page3MessageColor","footerColor"};
const char *colorKeys[] = {"bg","clock","date","net","ip","p2time","p2date","body","p3time","p3msg","foot"};

static const uint8_t BUTTON_PIN = 9;
static const size_t CSV_LIMIT = 3500;
static const time_t VALID_TIME = 1700000000;
Settings cfg;
Preferences preferences;
WebServer server(80);
String thirdMessage = "欢迎使用 ESP32-C6 时钟", scheduleCsv;
ScaledFont bodyFont;
const lv_font_t *activeClockFont = nullptr;
lv_obj_t *timeLabel, *dateLabel, *bodyLabel, *wifiLabel, *ipLabel, *footerLabel;
lv_obj_t *viewport, *thirdLabel;
lv_obj_t *bodyViewport;
uint32_t bodyPageAt = 0;
uint16_t bodyStride = 0, bodyPage = 0;
struct CourseRow { String cell[3]; int minute; };
CourseRow dayCourses[42];
uint8_t dayCourseCount = 0, tableRow = 0, tableNextRow = 0;
uint16_t tableLine = 0, tableNextLine = 0;
lv_obj_t *scheduleTable;
lv_obj_t *weatherLabel;
uint8_t *uploadData = nullptr;
size_t uploadSize = 0, uploadReceived = 0;
uint32_t uploadKey = 0, uploadAt = 0;
uint32_t textSegmentAt = 0;
uint16_t textSegment = 0, textSegmentStride = 0;
uint8_t currentPage = 0;
uint32_t lastSwitch = 0, lastWifiAttempt = 0;
bool ntpStarted = false, timeSynced = false;
bool fontCached = false;
bool scheduleStatus = true;
uint32_t ntpStartedAt = 0;
volatile bool syncObserved = false;
#include "WeatherService.h"

void renderPage();
void updateContent(bool force);
void handleState();
static bool hasCredentials();

static const lv_font_t *textFont() {
#if LV_FONT_SIMSUN_16_CJK
  return &lv_font_simsun_16_cjk;
#else
  return LV_FONT_DEFAULT;
#endif
}
static const lv_font_t *digitFont() {
#if LV_FONT_MONTSERRAT_16
  return &lv_font_montserrat_16;
#else
  return LV_FONT_DEFAULT;
#endif
}
static int screenWidth() { return cfg.landscape ? LCD_HEIGHT : LCD_WIDTH; }
static int screenHeight() { return cfg.landscape ? LCD_WIDTH : LCD_HEIGHT; }
static int viewportTop() { return cfg.thirdTimeSize + 18; }
static int viewportHeight() { return screenHeight() - viewportTop() - 22; }
static bool horizontalText() { return cfg.marqueeMode <= 2; }
static bool pageEnabled(int page) {
  return page == 0 || page == 3 || (page == 1 && cfg.page2Enabled) || (page == 2 && cfg.page3Enabled);
}
static uint8_t nextPage() {
  for (uint8_t step = 1; step <= 4; ++step) {
    uint8_t page = (currentPage + step) % 4;
    if (pageEnabled(page)) return page;
  }
  return 0;
}
static uint32_t renderKey() {
  uint32_t hash = 2166136261UL;
  const String *texts[] = {&thirdMessage, &scheduleCsv};
  for (const String *text : texts) {
    for (size_t i = 0; i < text->length(); ++i) hash = (hash ^ (uint8_t)(*text)[i]) * 16777619UL;
    hash = (hash ^ 0xFF) * 16777619UL;
  }
  for (const char *p = WIFI_SSID; *p; ++p) hash = (hash ^ (uint8_t)*p) * 16777619UL;
  hash = (hash ^ cfg.scheduleTextSize) * 16777619UL;
  hash = (hash ^ cfg.thirdTextSize) * 16777619UL;
  return hash ? hash : 1;
}
static bool fontReady() { return glyphPacket && glyphPacketKey == renderKey(); }
static void setText(lv_obj_t *label, const char *text) {
  if (strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}
static void hide(lv_obj_t *obj, bool hidden) {
  if (hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
}
static void position(lv_obj_t *obj, int y, int width, int height) {
  lv_obj_set_size(obj, width, height);
  lv_obj_align(obj, LV_ALIGN_TOP_MID, 0, y);
}
static void useFont(lv_obj_t *obj, ScaledFont &font, const lv_font_t *base, uint8_t pixels) {
  configureFont(font, base, pixels);
  lv_obj_set_style_text_font(obj, uploadedFont(obj == thirdLabel ? 1 : 0, &font.font), 0);
  lv_obj_refresh_self_size(obj);
}
static void color(lv_obj_t *obj, uint32_t rgb) {
  lv_obj_set_style_text_color(obj, lv_color_hex(rgb), 0);
}
static void abortUpload() {
  free(uploadData);
  uploadData = nullptr;
  uploadSize = uploadReceived = 0;
}
static void animateAxis(lv_obj_t *obj, bool x, int start, int end, uint32_t duration, bool bounce) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, obj);
  lv_anim_set_exec_cb(&a, x ? [](void *p, int32_t v) { lv_obj_set_x((lv_obj_t *)p, v); }
                          : [](void *p, int32_t v) { lv_obj_set_y((lv_obj_t *)p, v); });
  lv_anim_set_values(&a, start, end);
  lv_anim_set_time(&a, duration);
  if (bounce) lv_anim_set_playback_time(&a, duration);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&a);
}
static void startTextAnimation() {
  lv_anim_del(thirdLabel, nullptr);
  textSegment = 0; textSegmentStride = 0; textSegmentAt = millis();
  if (currentPage != 2) return;
  lv_obj_t *obj = thirdLabel;
  lv_obj_update_layout(obj);
  int w = lv_obj_get_width(obj), h = lv_obj_get_height(obj);
  int vw = screenWidth() - 16, vh = viewportHeight();
  lv_obj_set_pos(obj, max(0, (vw - w) / 2), max(0, (vh - h) / 2));
  if (cfg.marqueeMode == 5) {
    const lv_font_t *font = lv_obj_get_style_text_font(thirdLabel, 0);
    int pitch = font->line_height + 2;
    int lines = max(1, vh / pitch);
    textSegmentStride = lines * pitch;
    lv_obj_set_height(viewport, min(vh, (int)textSegmentStride - 2));
    lv_obj_set_y(obj, 0);
    return;
  }
  if (cfg.marqueeMode == 0) {
    if (w > vw) animateAxis(obj, true, 0, vw - w, max(2000, (w - vw) * 1000 / 45), true);
  } else if (cfg.marqueeMode == 1 || cfg.marqueeMode == 2) {
    bool right = cfg.marqueeMode == 2;
    animateAxis(obj, true, right ? -w : vw, right ? vw : -w, (vw + w) * 1000UL / 45, false);
  } else if (cfg.marqueeMode <= 4) {
    bool up = cfg.marqueeMode == 3;
    animateAxis(obj, false, up ? vh : -h, up ? -h : vh, (vh + h) * 1000UL / 35, false);
  } else {
    bool upRight = cfg.marqueeMode == 6;
    uint32_t duration = (vw + w) * 1000UL / 45;
    animateAxis(obj, true, upRight ? -w : vw, upRight ? vw : -w, duration, false);
    animateAxis(obj, false, upRight ? vh : -h, upRight ? -h : vh, duration, false);
  }
}
static void showThirdText() {
  String text;
  if (cfg.rainbowText) {
    text.reserve(thirdMessage.length() * 5);
    for (size_t i = 0; i < thirdMessage.length();) {
      uint8_t first = (uint8_t)thirdMessage[i];
      size_t length = first < 0x80 ? 1 : ((first & 0xE0) == 0xC0 ? 2 : ((first & 0xF0) == 0xE0 ? 3 : 4));
      if (first == '#') text += "##";
      else if (first == '\n') text += '\n';
      else {
        lv_color32_t c; c.full = lv_color_to32(lv_color_hsv_to_rgb((i * 359UL) / max((size_t)1,thirdMessage.length()),85,100));
        char prefix[10]; snprintf(prefix,sizeof(prefix),"#%02X%02X%02X ",c.ch.red,c.ch.green,c.ch.blue);
        text += prefix; text += thirdMessage.substring(i, i + length); text += '#';
      }
      i += length;
    }
  } else text = thirdMessage;
  lv_label_set_recolor(thirdLabel, cfg.rainbowText != 0);
  setText(thirdLabel, text.c_str());
  lv_label_set_long_mode(thirdLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_line_space(thirdLabel, 2, 0);
  lv_obj_set_size(thirdLabel, horizontalText() ? LV_SIZE_CONTENT : screenWidth() - 16, LV_SIZE_CONTENT);
  startTextAnimation();
}

static int clockMinutes(const String &text) {
  int colon = text.indexOf(':');
  if ((colon != 1 && colon != 2) || text.length() != (size_t)colon + 3) return -1;
  for (size_t i = 0; i < text.length(); ++i)
    if ((int)i != colon && (text[i] < '0' || text[i] > '9')) return -1;
  int h = text.substring(0, colon).toInt(), m = text.substring(colon + 1).toInt();
  return h < 24 && m < 60 ? h * 60 + m : -1;
}
static bool csvRow(const String &row, String fields[5]) {
  int from = 0;
  for (int i = 0; i < 4; ++i) {
    int comma = row.indexOf(',', from);
    if (comma < 0) return false;
    fields[i] = row.substring(from, comma);
    fields[i].trim();
    from = comma + 1;
  }
  fields[4] = row.substring(from); fields[4].trim();
  return fields[4].indexOf(',') < 0 && fields[0].length() == 1 &&
    fields[0][0] >= '1' && fields[0][0] <= '7' && fields[3].length() &&
    clockMinutes(fields[1]) >= 0 && clockMinutes(fields[2]) > clockMinutes(fields[1]);
}
static String validateCsv(const String &csv) {
  int start = 0, line = 0, count = 0;
  while (start < (int)csv.length()) {
    ++line;
    int end = csv.indexOf('\n', start); if (end < 0) end = csv.length();
    String row = csv.substring(start, end); row.trim(); start = end + 1;
    if (!row.length()) continue;
    String fields[5];
    if (!csvRow(row, fields)) return String("课表第 ") + line + " 行格式错误，请按示例填写";
    if (++count > 42) return "课表最多 42 节";
  }
  return "";
}
static String roomDisplayName(String room) {
  // Normalize common Unicode separators before removing the room suffix.
  const char *separators[] = {"－", "–", "—", "−", "﹣", "‑", "‐"};
  for (const char *separator : separators) room.replace(separator, "-");
  room.replace("\xC2\xA0", " ");
  room.replace("　", " ");
  room.trim();
  int suffix = room.indexOf('-');
  if (suffix > 0) room.remove(suffix);
  room.trim();
  return room;
}
static String scheduleText(time_t now) {
  scheduleStatus = true;
  dayCourseCount = 0;
  if (!timeSynced) {
    if (!hasCredentials()) return MSG_WIFI_CONFIG;
    if (WiFi.status() != WL_CONNECTED) return MSG_WAIT_WIFI;
    return ntpStarted && millis() - ntpStartedAt >= 15000UL ? MSG_TIME_HELP : MSG_WAIT_TIME;
  }
  if (now < VALID_TIME) return MSG_WAIT_TIME;
  if (!scheduleCsv.length()) return MSG_EMPTY_CSV;
  struct tm tm; localtime_r(&now, &tm);
  int weekday = tm.tm_wday ? tm.tm_wday : 7, minute = tm.tm_hour * 60 + tm.tm_min;
  String today;
  today.reserve(1100);
  int cursor = 0;
  while (cursor < (int)scheduleCsv.length()) {
    int end = scheduleCsv.indexOf('\n', cursor); if (end < 0) end = scheduleCsv.length();
    String row = scheduleCsv.substring(cursor, end); row.trim(); cursor = end + 1;
    String fields[5];
    if (!csvRow(row, fields) || fields[0].toInt() != weekday) continue;
    int start = clockMinutes(fields[1]);
    if (dayCourseCount >= 42) break;
    fields[4] = roomDisplayName(fields[4]);
    CourseRow &course = dayCourses[dayCourseCount++];
    course.cell[0] = fields[1]; course.cell[1] = fields[3]; course.cell[2] = fields[4];
    course.minute = start;
    String item = fields[1] + " " + fields[3];
    if (fields[4].length()) item += " " + fields[4];
    item += '\n';
    today += item;
  }
  if (!today.length()) return MSG_NO_CLASS;
  (void)minute;
  for (uint8_t i = 1; i < dayCourseCount; ++i) {
    uint8_t j = i;
    while (j && dayCourses[j].minute < dayCourses[j-1].minute) {
      CourseRow swap = dayCourses[j]; dayCourses[j] = dayCourses[j-1]; dayCourses[j-1] = swap; --j;
    }
  }
  String result = String(MSG_TODAY) + today;
  const lv_font_t *font = uploadedFont(0, bodyNativeFont(cfg.scheduleTextSize));
  const char *p = result.c_str();
  while (*p) {
    uint32_t code = (uint8_t)*p++;
    if (code >= 0xC0) {
      int n = code < 0xE0 ? 1 : (code < 0xF0 ? 2 : 3);
      code &= (1U << (6 - n)) - 1;
      while (n-- && *p) code = (code << 6) | ((uint8_t)*p++ & 63);
    }
    if (code < 32) continue;
    lv_font_glyph_dsc_t glyph = {};
    if (!lv_font_get_glyph_dsc(font, &glyph, code, 0) || glyph.is_placeholder) return MSG_FONT_HELP;
  }
  scheduleStatus = false;
  return result;
}
#include "ScheduleTable.h"
static void fitClock(uint8_t size) {
  char sample[20];
  lv_point_t extent;
  do {
    activeClockFont = clockNativeFont(size);
    char widest = '0'; uint16_t advance = 0;
    for (char c = '0'; c <= '9'; ++c) {
      lv_font_glyph_dsc_t g = {};
      if (lv_font_get_glyph_dsc(activeClockFont, &g, c, 0) && g.adv_w > advance) { advance = g.adv_w; widest = c; }
    }
    snprintf(sample, sizeof(sample), "%c%c:%c%c:%c%c", widest,widest,widest,widest,widest,widest);
    if (currentPage == 2 && cfg.thirdTimeFormat == 1) strcat(sample, " PM");
    if (currentPage == 2 && cfg.thirdTimeFormat == 2) sample[5] = 0;
    lv_txt_get_size(&extent, sample, activeClockFont, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  } while (extent.x > screenWidth() - 18 && size-- > 12);
  lv_obj_set_style_text_font(timeLabel, activeClockFont, 0);
  lv_obj_refresh_self_size(timeLabel);
}
void renderPage() {
  if (!pageEnabled(currentPage)) currentPage = 0;
  lv_anim_del(thirdLabel, nullptr);
  lv_label_set_long_mode(bodyLabel, LV_LABEL_LONG_WRAP);
  hide(timeLabel, currentPage == 3);
  hide(dateLabel, currentPage != 0);
  hide(weatherLabel, currentPage != 0 || !weatherSettings.enabled);
  hide(wifiLabel, currentPage != 0);
  hide(ipLabel, currentPage != 0);
  hide(bodyViewport, currentPage != 1 && currentPage != 3);
  hide(scheduleTable, true);
  hide(bodyLabel, false);
  bodyStride = bodyPage = 0;
  bodyPageAt = millis();
  lv_obj_set_y(bodyLabel, 0);
  hide(viewport, currentPage != 2);
  lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(cfg.color[BG]), 0);
  color(footerLabel, cfg.color[FOOTER]);
  char number[2] = {(char)('1' + currentPage), 0};
  setText(footerLabel, number);
  lv_obj_align(footerLabel, LV_ALIGN_BOTTOM_MID, 0, -2);
  const int w = screenWidth(), h = screenHeight();
  if (currentPage < 3) {
    uint8_t requested = currentPage == 0 ? cfg.largeTimeSize : (currentPage == 1 ? cfg.smallTimeSize : cfg.thirdTimeSize);
    fitClock(requested);
    if(currentPage==0 && cfg.landscape && weatherSettings.enabled) {
      while(activeClockFont->line_height>screenHeight()-130 && requested>12) fitClock(--requested);
    }
    int top = currentPage == 0 ? (cfg.landscape ? 2 : 35) : (currentPage == 1 ? 2 : 8);
    position(timeLabel, top, w - 12, LV_SIZE_CONTENT);
    color(timeLabel, cfg.color[currentPage == 0 ? CLOCK : (currentPage == 1 ? P2TIME : P3TIME)]);
    if (currentPage <= 1) {
      const lv_font_t *dateFont = dateNativeFont(currentPage == 0 && cfg.landscape);
      lv_obj_set_style_text_font(dateLabel, dateFont, 0);
      int dateY = top + activeClockFont->line_height + 6;
      if(currentPage==0 && weatherSettings.enabled) {
        int weatherY=top+activeClockFont->line_height+2;
        lv_obj_set_style_text_font(weatherLabel,bodyNativeFont(16),0);
        lv_label_set_long_mode(weatherLabel,LV_LABEL_LONG_SCROLL_CIRCULAR);
        position(weatherLabel,weatherY,w-12,LV_SIZE_CONTENT);
        color(weatherLabel,cfg.color[DATE]);
        dateY=weatherY+bodyNativeFont(16)->line_height+2;
      }
      position(dateLabel, dateY, w - 12, LV_SIZE_CONTENT);
      color(dateLabel, cfg.color[currentPage == 0 ? DATE : P2DATE]);
      if (currentPage == 0) {
        const lv_font_t *infoFont = cfg.landscape ? networkNativeFont() : digitFont();
        lv_obj_set_style_text_font(wifiLabel, infoFont, 0);
        lv_obj_set_style_text_font(ipLabel, infoFont, 0);
        const int infoY = dateY + dateFont->line_height + 2;
        lv_label_set_long_mode(wifiLabel, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_label_set_long_mode(ipLabel, LV_LABEL_LONG_SCROLL_CIRCULAR);
        position(wifiLabel, infoY, w - 12, LV_SIZE_CONTENT);
        position(ipLabel, infoY + infoFont->line_height + 1, w - 12, LV_SIZE_CONTENT);
        color(wifiLabel, cfg.color[NET]); color(ipLabel, cfg.color[IP]);
      } else {
        lv_obj_set_style_text_font(bodyLabel, bodyNativeFont(20), 0);
        const int bodyY = top + activeClockFont->line_height + 5;
        position(bodyViewport, bodyY, w - 16, h - bodyY - 22);
        lv_obj_set_size(bodyLabel, w - 16, LV_SIZE_CONTENT);
        color(bodyLabel, cfg.color[BODY]);
      }
    } else {
      useFont(thirdLabel, bodyFont, textFont(), cfg.thirdTextSize);
      color(thirdLabel, cfg.color[P3TEXT]);
      position(viewport, viewportTop(), w - 16, viewportHeight());
      showThirdText();
    }
  } else {
    static lv_font_t networkFont;
    position(bodyViewport, 0, w, h);
    networkFont = *networkNativeFont();
    const lv_font_t *extra = uploadedFont(2, textFont());
    int glyphNetworkBaseline = max(networkFont.line_height - networkFont.base_line, extra->line_height - extra->base_line);
    networkFont.base_line = max(networkFont.base_line, extra->base_line);
    networkFont.line_height = glyphNetworkBaseline + networkFont.base_line;
    networkFont.fallback = extra;
    lv_obj_set_style_text_font(bodyLabel, &networkFont, 0);
    lv_obj_set_style_text_line_space(bodyLabel, 1, 0);
    position(bodyLabel, 6, w - 12, h - 27);
    lv_label_set_long_mode(bodyLabel, LV_LABEL_LONG_SCROLL);
    color(bodyLabel, cfg.color[DATE]);
  }
  updateContent(true);
}
static void switchPage(uint8_t page) {
  currentPage = page;
  lastSwitch = millis();
  renderPage();
}
void updateContent(bool force) {
  static time_t lastScheduleMinute = -1;
  static time_t lastScheduleDay = -1;
  time_t now = time(nullptr);
  char clock[20] = "--:--:--", date[48] = "等待校时";
  if (now >= VALID_TIME) {
    struct tm tm; localtime_r(&now, &tm);
    const char *format = currentPage == 2 && cfg.thirdTimeFormat == 1 ? "%I:%M:%S %p" :
      (currentPage == 2 && cfg.thirdTimeFormat == 2 ? "%H:%M" : "%H:%M:%S");
    strftime(clock, sizeof(clock), format, &tm);
    const char *days[] = {"周日","周一","周二","周三","周四","周五","周六"};
    snprintf(date, sizeof(date), "%04d-%02d-%02d %s", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, days[tm.tm_wday]);
  }
  if (currentPage < 3) setText(timeLabel, clock);
  if (currentPage <= 1) setText(dateLabel, date);
  const bool online = WiFi.status() == WL_CONNECTED;
  if (currentPage == 0) {
    String weather = weatherText(); setText(weatherLabel, weather.c_str());
    setText(wifiLabel, online ? "Wi-Fi: online" : "Wi-Fi: connecting");
    String ip = "IP: " + (online ? WiFi.localIP().toString() : String("--"));
    setText(ipLabel, ip.c_str());
  } else if (currentPage == 1 && (force || !timeSynced || now / 60 != lastScheduleMinute)) {
    lastScheduleMinute = now / 60;
    String text = scheduleText(now);
    time_t day=(now+8L*3600L)/86400L;
    bool resetTable=force || day!=lastScheduleDay || tableRow>=dayCourseCount;
    lastScheduleDay=day;
    if(resetTable) { tableRow=tableNextRow=0; tableLine=tableNextLine=0; bodyPageAt=millis(); }
    hide(scheduleTable, scheduleStatus); hide(bodyLabel, !scheduleStatus);
    if (!scheduleStatus) {
      lv_obj_set_size(scheduleTable, screenWidth() - 16, screenHeight() - lv_obj_get_y(bodyViewport) - 22);
      lv_obj_set_height(bodyViewport, lv_obj_get_height(scheduleTable));
      bodyStride = 0;
      lv_obj_invalidate(scheduleTable);
      return;
    }
    lv_obj_set_style_text_font(bodyLabel, scheduleStatus ? bodyNativeFont(20) : uploadedFont(0, bodyNativeFont(cfg.scheduleTextSize)), 0);
    lv_label_set_long_mode(bodyLabel, LV_LABEL_LONG_WRAP);
    setText(bodyLabel, text.c_str());
    lv_obj_update_layout(bodyLabel);
    int pitch = lv_obj_get_style_text_font(bodyLabel, 0)->line_height;
    int available = screenHeight() - lv_obj_get_y(bodyViewport) - 22;
    bodyStride = max(1, available / pitch) * pitch;
    lv_obj_set_height(bodyViewport, bodyStride);
    bodyPage = 0; bodyPageAt = millis(); lv_obj_set_y(bodyLabel, 0);
  } else if (currentPage == 3) {
    String text;
    text.reserve(300);
    text = online ? "Wi-Fi  " + String(WiFi.RSSI()) + " dBm\nSSID  " + WiFi.SSID() + "\nIP  " + WiFi.localIP().toString()
      : String("Wi-Fi  offline\nIP  --");
    text += timeSynced ? "\nNTP  synced once" : "\nNTP  waiting";
    uint32_t minutes = millis() / 60000UL;
    text += "\nUP  " + String(minutes / 60) + "h " + String(minutes % 60) + "m";
    setText(bodyLabel, text.c_str());
  }
}
static lv_obj_t *makeLabel(lv_obj_t *parent, bool chinese) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_pad_all(label, 0, 0);
  lv_obj_set_style_text_letter_space(label, 0, 0);
  lv_obj_set_style_text_line_space(label, 0, 0);
  lv_obj_set_style_text_font(label, chinese ? textFont() : digitFont(), 0);
  lv_label_set_text(label, "");
  return label;
}
static void createUI() {
  lv_obj_clean(lv_scr_act()); // Boot only. Settings never destroy the widget tree.
  lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, 0);
  lv_obj_clear_flag(lv_scr_act(), LV_OBJ_FLAG_SCROLLABLE);
  timeLabel = makeLabel(lv_scr_act(), false);
  dateLabel = makeLabel(lv_scr_act(), true);
  weatherLabel = makeLabel(lv_scr_act(),true);
  bodyViewport = lv_obj_create(lv_scr_act());
  lv_obj_remove_style_all(bodyViewport);
  lv_obj_clear_flag(bodyViewport, LV_OBJ_FLAG_SCROLLABLE);
  bodyLabel = makeLabel(bodyViewport, true);
  scheduleTable = lv_obj_create(bodyViewport);
  lv_obj_remove_style_all(scheduleTable);
  lv_obj_clear_flag(scheduleTable, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(scheduleTable, drawScheduleTable, LV_EVENT_DRAW_MAIN, nullptr);
  wifiLabel = makeLabel(lv_scr_act(), false);
  ipLabel = makeLabel(lv_scr_act(), false);
  footerLabel = makeLabel(lv_scr_act(), false);
  viewport = lv_obj_create(lv_scr_act());
  lv_obj_remove_style_all(viewport);
  lv_obj_clear_flag(viewport, LV_OBJ_FLAG_SCROLLABLE);
  thirdLabel = makeLabel(viewport, true);
}
static void applyRotation() {
  lv_disp_t *display = lv_disp_get_default();
  display->driver->full_refresh = 0;
  display->driver->sw_rotate = cfg.landscape;
  lv_disp_set_rotation(display, cfg.landscape ? LV_DISP_ROT_90 : LV_DISP_ROT_NONE);
}
static void loadSettings() {
  Settings saved;
  bool valid = preferences.getBytesLength("config2") == sizeof(saved) &&
    preferences.getBytes("config2", &saved, sizeof(saved)) == sizeof(saved) && saved.marker == cfg.marker;
  if (valid) cfg = saved;
  else {
    for (const auto &f : numberFields) {
      // Old firmware stored these three flags as NVS booleans.
      bool wasBool = f.member == &Settings::page2Enabled || f.member == &Settings::page3Enabled || f.member == &Settings::rainbowText;
      cfg.*(f.member) = wasBool ? preferences.getBool(f.legacy, cfg.*(f.member)) : preferences.getUChar(f.legacy, cfg.*(f.member));
    }
    for (int i = 0; i < COLOR_COUNT; ++i) cfg.color[i] = preferences.getUInt(colorKeys[i], cfg.color[i]);
  }
  for (const auto &f : numberFields) cfg.*(f.member) = constrain(cfg.*(f.member), f.minimum, f.maximum);
  thirdMessage = preferences.getString("scroll", thirdMessage);
  scheduleCsv = preferences.getString("schedule", "");
  WeatherSettings savedWeather;
  if(preferences.getBytesLength("weather1")==sizeof(savedWeather) &&
      preferences.getBytes("weather1",&savedWeather,sizeof(savedWeather))==sizeof(savedWeather) &&
      savedWeather.city[96]==0 && savedWeather.enabled<=1 && isfinite(savedWeather.latitude) &&
      isfinite(savedWeather.longitude) && fabsf(savedWeather.latitude)<=90 && fabsf(savedWeather.longitude)<=180)
    weatherSettings=savedWeather;
  if (thirdMessage.length() > 360) thirdMessage = "ESP32-C6";
  if (scheduleCsv.length() > CSV_LIMIT || validateCsv(scheduleCsv).length()) scheduleCsv = "";
}
static void appendQuoted(String &out, const String &text) {
  out += '"';
  for (size_t i = 0; i < text.length(); ++i) {
    char c = text[i];
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else if ((uint8_t)c >= 32) out += c;
  }
  out += '"';
}
static void loadGlyphCache() {
  size_t bytes = preferences.getBytesLength("glyphcache");
  if (bytes < 32 || bytes > 8192) return;
  uint8_t *data = (uint8_t *)malloc(bytes);
  if (!data) return;
  if (preferences.getBytes("glyphcache", data, bytes) != bytes || !validateGlyphPacket(data, bytes) ||
      glyphU32(data + 20) != renderKey()) { free(data); return; }
  glyphPacket = data; glyphPacketKey = glyphU32(data + 20); fontCached = true;
}
void handleState() {
  String json; json.reserve(1700 + scheduleCsv.length() + thirdMessage.length());
  json = "{\"fontKey\":\"" + String(renderKey()) + "\",\"fontReady\":" + String(fontReady() ? "true" : "false");
  json += ",\"fontCached\":" + String(fontCached && fontReady() ? "true" : "false");
  json += ",\"currentPage\":" + String(currentPage) + ",\"timeSynced\":" + String(timeSynced ? "true" : "false");
  json += ",\"freeHeap\":" + String(ESP.getFreeHeap()) + ",\"maxHeap\":" + String(ESP.getMaxAllocHeap());
  json += ",\"uptime\":" + String(millis() / 1000UL) + ",\"rssi\":" + String(WiFi.RSSI());
  json += ",\"ip\":"; appendQuoted(json, WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String(""));
  json += ",\"ssid\":"; appendQuoted(json, WiFi.SSID());
  json += ",\"weatherCity\":"; appendQuoted(json,String(weatherSettings.city));
  json += ",\"weatherEnabled\":"+String(weatherSettings.enabled);
  json += ",\"weatherLat\":"+String(weatherSettings.latitude,5)+",\"weatherLon\":"+String(weatherSettings.longitude,5);
  json += ",\"weatherStatus\":"; appendQuoted(json,weatherText());
  json += ",\"thirdMessage\":"; appendQuoted(json, thirdMessage);
  json += ",\"scheduleCsv\":"; appendQuoted(json, scheduleCsv);
  for (const auto &f : numberFields) { json += ",\""; json += f.name; json += "\":"; json += String(cfg.*(f.member)); }
  for (int i = 0; i < COLOR_COUNT; ++i) {
    char value[8]; snprintf(value, sizeof(value), "#%06lX", (unsigned long)cfg.color[i]);
    json += ",\""; json += colorNames[i]; json += "\":"; appendQuoted(json, value);
  }
  json += '}';
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json; charset=utf-8", json);
}
static bool unsignedNumber(const String &text, uint32_t &value) {
  if (!text.length() || text.length() > 10) return false;
  uint64_t n = 0;
  for (size_t i = 0; i < text.length(); ++i) {
    if (text[i] < '0' || text[i] > '9') return false;
    n = n * 10 + text[i] - '0';
    if (n > UINT32_MAX) return false;
  }
  value = n; return true;
}
static void error(int status, const String &message) { server.send(status, "text/plain; charset=utf-8", message); }
static void ok() { server.send(200, "application/json", "{\"ok\":true}"); }
static void handleSave() {
  Settings next = cfg;
  WeatherSettings nextWeather = weatherSettings;
  if(server.hasArg("weatherEnabled")) {
    uint32_t enabled;
    if(!unsignedNumber(server.arg("weatherEnabled"),enabled)||enabled>1) { error(400,"天气开关错误"); return; }
    nextWeather.enabled=enabled;
  }
  if(server.hasArg("weatherCity")) {
    String city=server.arg("weatherCity"); city.trim();
    if(city.length()>96 || city.indexOf('\n')>=0) { error(400,"天气地址过长或格式错误"); return; }
    memset(nextWeather.city,0,sizeof(nextWeather.city)); city.toCharArray(nextWeather.city,sizeof(nextWeather.city));
  }
  const char *coordinateNames[]={"weatherLat","weatherLon"};
  float *coordinates[]={&nextWeather.latitude,&nextWeather.longitude};
  for(int i=0;i<2;++i) if(server.hasArg(coordinateNames[i])) {
    String raw=server.arg(coordinateNames[i]); raw.trim(); char *end;
    float value=strtof(raw.c_str(),&end);
    if(!raw.length() || *end || !isfinite(value) || fabsf(value)>(i?180:90)) { error(400,"天气经纬度错误"); return; }
    *coordinates[i]=value;
  }
  if(nextWeather.enabled && !nextWeather.city[0]) { error(400,"请填写天气城市，并选择地点或填写经纬度"); return; }
  for (const auto &f : numberFields) {
    if (!server.hasArg(f.name)) continue;
    uint32_t n;
    if (!unsignedNumber(server.arg(f.name), n) || n < f.minimum || n > f.maximum) {
      error(400, String("参数超出范围: ") + f.name); return;
    }
    next.*(f.member) = n;
  }
  for (int i = 0; i < COLOR_COUNT; ++i) {
    if (!server.hasArg(colorNames[i])) continue;
    String value = server.arg(colorNames[i]);
    if (value.length() != 7 || value[0] != '#') { error(400, "颜色格式错误"); return; }
    for (int j = 1; j < 7; ++j) if (!isxdigit((unsigned char)value[j])) { error(400, "颜色格式错误"); return; }
    next.color[i] = strtoul(value.c_str() + 1, nullptr, 16);
  }
  String text = server.hasArg("thirdMessage") ? server.arg("thirdMessage") : thirdMessage;
  String csv = server.hasArg("scheduleCsv") ? server.arg("scheduleCsv") : scheduleCsv;
  text.replace("\r", "");
  if (csv.startsWith("\xEF\xBB\xBF")) csv.remove(0, 3);
  csv.replace("\r", "");
  if (text.length() > 360 || csv.length() > CSV_LIMIT) { error(413, "文字或课表内容超过容量"); return; }
  if (csv != scheduleCsv) {
    String problem = validateCsv(csv);
    if (problem.length()) { error(400, problem); return; }
  }
  // Write only changed values. A color/brightness edit never rewrites the timetable.
  bool weatherChanged=memcmp(&nextWeather,&weatherSettings,sizeof(nextWeather))!=0;
  if(weatherChanged && preferences.putBytes("weather1",&nextWeather,sizeof(nextWeather))!=sizeof(nextWeather)) { error(507,"天气地址保存失败"); return; }
  if (text != thirdMessage && preferences.putString("scroll", text) != text.length()) { error(507, "文字保存失败"); return; }
  if (csv != scheduleCsv && preferences.putString("schedule", csv) != csv.length()) { error(507, "课表保存失败"); return; }
  if (memcmp(&next, &cfg, sizeof(cfg)) && preferences.putBytes("config2", &next, sizeof(next)) != sizeof(next)) { error(507, "设置保存失败"); return; }
  bool rotated = next.landscape != cfg.landscape;
  bool backlight = next.brightness != cfg.brightness;
  bool redraw = weatherChanged || text != thirdMessage || csv != scheduleCsv || memcmp(next.color, cfg.color, sizeof(cfg.color));
  for (const auto &f : numberFields) {
    if (f.member != &Settings::brightness && f.member != &Settings::autoCarousel && f.member != &Settings::carouselSeconds)
      redraw |= next.*(f.member) != cfg.*(f.member);
  }
  if (next.autoCarousel != cfg.autoCarousel || next.carouselSeconds != cfg.carouselSeconds) lastSwitch = millis();
  cfg = next; thirdMessage = text; scheduleCsv = csv;
  if(weatherChanged) { weatherSettings=nextWeather; ++weatherVersion; weatherValid=false; weatherAttempted=false; }
  if (uploadData && uploadKey != renderKey()) abortUpload();
  if (backlight) Set_Backlight(cfg.brightness);
  if (rotated) applyRotation();
  if (redraw) renderPage();
  lv_mem_monitor_t memory; lv_mem_monitor(&memory);
  Serial.printf("Saved. heap=%u max=%u LVGL-free=%u\n", (unsigned)ESP.getFreeHeap(),
    (unsigned)ESP.getMaxAllocHeap(), (unsigned)memory.free_size);
  handleState(); // 200 JSON. Font subsets change only with text or text size.
}
static void handleFontStart() {
  if (WiFi.status() != WL_CONNECTED || !timeSynced) { error(409, "请先联网并完成首次校时"); return; }
  if (weatherBusy) { error(503, "正在更新天气，请稍后再次保存以加载字模"); return; }
  uint32_t key, bytes;
  if (!unsignedNumber(server.arg("key"), key) || key != renderKey() ||
      !unsignedNumber(server.arg("size"), bytes) || bytes < 32 || bytes > GLYPH_LIMIT) {
    error(400, "字模容量或版本错误"); return;
  }
  abortUpload();
  if (ESP.getFreeHeap() < bytes + 16384UL) { error(503, "内存不足，保留原有显示"); return; }
  uploadData = (uint8_t *)malloc(bytes);
  if (!uploadData) { error(503, "字模内存不足，保留原有显示"); return; }
  uploadKey = key; uploadSize = bytes; uploadReceived = 0; uploadAt = millis();
  ok();
}
static int base64Digit(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  return c == '+' ? 62 : (c == '/' ? 63 : -1);
}
static void handleFontChunk() {
  uint32_t offset;
  if (!uploadData || uploadKey != renderKey() || !unsignedNumber(server.arg("offset"), offset) || offset != uploadReceived) {
    abortUpload(); error(409, "字模上传已过期"); return;
  }
  String data = server.arg("data");
  if (!data.length() || data.length() > 684 || data.length() % 4) { abortUpload(); error(400, "字模数据错误"); return; }
  uint32_t acc = 0; uint8_t bits = 0; size_t decoded = 0;
  for (size_t i = 0; i < data.length(); ++i) {
    if (data[i] == '=') break;
    int digit = base64Digit(data[i]);
    if (digit < 0) { abortUpload(); error(400, "字模编码错误"); return; }
    acc = (acc << 6) | digit; bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (++decoded > 512 || uploadReceived >= uploadSize) { abortUpload(); error(400, "字模数据过长"); return; }
      uploadData[uploadReceived++] = acc >> bits;
    }
  }
  if (!decoded) { abortUpload(); error(400, "空数据"); return; }
  uploadAt = millis(); ok();
}
static void handleFontEnd() {
  uint32_t key;
  if (!uploadData || !unsignedNumber(server.arg("key"), key) || key != uploadKey ||
      key != renderKey() || uploadReceived != uploadSize || !validateGlyphPacket(uploadData, uploadSize)) {
    abortUpload(); error(409, "字模不完整、格式错误或设置已变更"); return;
  }
  for (uint8_t i = 0; i < 4; ++i) uploadData[20 + i] = key >> (i * 8);
  fontCached = uploadSize <= 8192 && preferences.putBytes("glyphcache", uploadData, uploadSize) == uploadSize;
  uint8_t *old = glyphPacket;
  glyphPacket = uploadData; glyphPacketKey = key; uploadData = nullptr;
  uploadSize = uploadReceived = 0;
  renderPage();
  free(old);
  server.send(200, "application/json", fontCached ? "{\"ok\":true,\"cached\":true}" : "{\"ok\":true,\"cached\":false}");
}
static void startServer() {
  server.on("/", HTTP_GET, []() { server.sendHeader("Cache-Control", "no-store"); server.send_P(200, "text/html; charset=utf-8", INDEX_HTML); });
  server.on("/state", HTTP_GET, handleState);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/page", HTTP_POST, []() {
    uint32_t page;
    if (!unsignedNumber(server.arg("index"), page) || page > 3 || !pageEnabled(page)) { error(400, "该页面已关闭"); return; }
    switchPage(page); ok();
  });
  server.on("/font/start", HTTP_POST, handleFontStart);
  server.on("/font/chunk", HTTP_POST, handleFontChunk);
  server.on("/font/end", HTTP_POST, handleFontEnd);
  server.begin();
}
static bool hasCredentials() {
  return WIFI_SSID[0] && strcmp(WIFI_SSID, "YOUR_WIFI_SSID") != 0;
}
static void startWifi() {
  if (hasCredentials()) { WiFi.begin(WIFI_SSID, WIFI_PASSWORD); lastWifiAttempt = millis(); }
}
static void onTimeSync(struct timeval *) { syncObserved = true; }
static void networkTick() {
  if (syncObserved && !timeSynced) {
    if (time(nullptr) < VALID_TIME) return;
    timeSynced = true;
    esp_sntp_stop();
    loadGlyphCache();
    renderPage();
    Serial.println("NTP synchronized once; stopped until next boot.");
  }
  if (WiFi.status() != WL_CONNECTED) {
    if (hasCredentials() && millis() - lastWifiAttempt >= 15000UL) startWifi();
    return;
  }
  if (!ntpStarted && !timeSynced) {
    ntpStarted = true;
    ntpStartedAt = millis();
    sntp_set_time_sync_notification_cb(onTimeSync);
    configTime(8L * 3600L, 0, "ntp.aliyun.com", "pool.ntp.org");
    Serial.print("Web settings: http://"); Serial.println(WiFi.localIP());
  }
}
static void buttonTick() {
  static int last = HIGH, stable = HIGH;
  static uint32_t changed = 0;
  int reading = digitalRead(BUTTON_PIN);
  if (reading != last) { last = reading; changed = millis(); }
  if (reading != stable && millis() - changed >= 35) {
    stable = reading;
    if (stable == LOW) switchPage(nextPage());
  }
}
void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  preferences.begin("clock-ui", false);
  loadSettings();
  LCD_Init(); Set_Backlight(cfg.brightness);
  Lvgl_Init(); createUI(); applyRotation(); renderPage();
  WiFi.mode(WIFI_STA); WiFi.setSleep(false); WiFi.setAutoReconnect(true);
  startWifi(); startServer();
}
void loop() {
  static uint32_t lastDraw = 0, lastSecond = 0;
  server.handleClient();
  buttonTick();
  uint32_t now = millis();
  weatherTick();
  if (uploadData && now - uploadAt > 20000UL) abortUpload();
  if (now - lastDraw >= 5) { lastDraw = now; Timer_Loop(); }
  if (now - lastSecond >= 1000) { lastSecond = now; networkTick(); updateContent(false); }
  if (cfg.autoCarousel && now - lastSwitch >= cfg.carouselSeconds * 1000UL) switchPage(nextPage());
  if (currentPage == 1 && !scheduleStatus && now - bodyPageAt >= 5000UL) {
    bodyPageAt = now;
    tableRow = tableNextRow; tableLine = tableNextLine;
    lv_obj_invalidate(scheduleTable);
  }
  if (currentPage == 1 && bodyStride && now - bodyPageAt >= 5000UL) {
    bodyPageAt = now;
    unsigned pages = (lv_obj_get_height(bodyLabel) + bodyStride - 1) / bodyStride;
    bodyPage = (bodyPage + 1) % max(1U, pages);
    lv_obj_set_y(bodyLabel, -(int)bodyPage * bodyStride);
  }
  if (currentPage == 2 && cfg.marqueeMode == 5 && textSegmentStride && now - textSegmentAt >= 4000UL) {
    textSegmentAt = now;
    unsigned pages = (lv_obj_get_height(thirdLabel) + 2 + textSegmentStride - 1) / textSegmentStride;
    textSegment = (textSegment + 1) % max(1U, pages);
    lv_obj_set_y(thirdLabel, -(int)textSegment * textSegmentStride);
  }
  delay(1);
}
