/*=====================================================================
  display_hal.h - Display Hardware Abstraction Layer

  LoraMeister - Unified display abstraction.
  Provides a single API that works across different display types
  with automatic fallback.

  SUPPORTED DISPLAYS:
  1. LCD 16x2 I2C (primary, address 0x27)
  2. E-ink SPI (ESP32 WROOM path)
  3. Serial Console (fallback when no display connected)

  USAGE:
    #include "display_hal.h"

    void setup() {
      platform_init();
      display_init();

      display_clear();
      display_print(0, "Hello World");
      display_print(1, "Line 2");
      display_printf(0, "RSSI: %d dBm", rssi);
    }
=======================================================================*/

#ifndef DISPLAY_HAL_H
#define DISPLAY_HAL_H

#include <Arduino.h>
#include <Wire.h>
#include "platform_hal.h"

#if ENABLE_EINK_DISPLAY && !USE_XIAO_SX1262
  #include <SPI.h>
  #include <GxEPD2_BW.h>
  #include <Fonts/FreeMonoBold9pt7b.h>
#endif

// ═══════════════════════════════════════════════════════════════════
// DISPLAY TYPES
// ═══════════════════════════════════════════════════════════════════

typedef enum {
  DISPLAY_NONE = 0,
  DISPLAY_LCD_I2C,       // LCD 16x2 I2C
  DISPLAY_EINK_SPI,      // E-ink paper display via SPI
  DISPLAY_SERIAL         // Serial console fallback
} DisplayType;

// ═══════════════════════════════════════════════════════════════════
// DISPLAY CONFIGURATION
// ═══════════════════════════════════════════════════════════════════

#define DISPLAY_LCD_ADDR       0x27   // Common I2C address for LCD
#define DISPLAY_LCD_COLS       16     // LCD columns
#define DISPLAY_LCD_ROWS       2      // LCD rows
#define DISPLAY_LINE_MAX_LEN   20     // Max characters per line

// ═══════════════════════════════════════════════════════════════════
// DISPLAY STATE
// ═══════════════════════════════════════════════════════════════════

static DisplayType g_displayType = DISPLAY_NONE;
static bool g_displayInitialized = false;

// Line buffers for comparison (avoid redundant updates)
static char g_displayLine0[DISPLAY_LINE_MAX_LEN + 1] = "";
static char g_displayLine1[DISPLAY_LINE_MAX_LEN + 1] = "";

// E-ink status cache
static unsigned long g_einkLastRefreshMs = 0;
static bool g_einkForceFullRefresh = true;

#if ENABLE_EINK_DISPLAY && !USE_XIAO_SX1262
static GxEPD2_BW<GxEPD2_213_B74, GxEPD2_213_B74::HEIGHT> g_eink(
  GxEPD2_213_B74(EINK_CS_PIN, EINK_DC_PIN, EINK_RST_PIN, EINK_BUSY_PIN)
);
#endif

// ═══════════════════════════════════════════════════════════════════
// LCD DRIVER (Minimal I2C implementation)
// ═══════════════════════════════════════════════════════════════════

#define LCD_BACKLIGHT   0x08
#define LCD_ENABLE      0x04
#define LCD_RS          0x01

#define LCD_CLEAR       0x01
#define LCD_HOME        0x02
#define LCD_ENTRY_MODE  0x06
#define LCD_DISPLAY_ON  0x0C
#define LCD_FUNCTION    0x28  // 4-bit, 2 lines, 5x8

static uint8_t g_lcdBacklight = LCD_BACKLIGHT;

static void lcd_write4bits(uint8_t data) {
  Wire.beginTransmission(DISPLAY_LCD_ADDR);
  Wire.write(data | g_lcdBacklight);
  Wire.endTransmission();
  delayMicroseconds(1);

  Wire.beginTransmission(DISPLAY_LCD_ADDR);
  Wire.write(data | g_lcdBacklight | LCD_ENABLE);
  Wire.endTransmission();
  delayMicroseconds(1);

  Wire.beginTransmission(DISPLAY_LCD_ADDR);
  Wire.write((data | g_lcdBacklight) & ~LCD_ENABLE);
  Wire.endTransmission();
  delayMicroseconds(50);
}

static void lcd_command(uint8_t cmd) {
  lcd_write4bits(cmd & 0xF0);
  lcd_write4bits((cmd << 4) & 0xF0);
  if (cmd <= 3) delay(2);
}

static void lcd_data(uint8_t data) {
  lcd_write4bits((data & 0xF0) | LCD_RS);
  lcd_write4bits(((data << 4) & 0xF0) | LCD_RS);
}

static bool lcd_init() {
  const PlatformPins* pins = platform_getPins();

  Wire.begin(pins->i2cSda, pins->i2cScl);
  delay(50);

  Wire.beginTransmission(DISPLAY_LCD_ADDR);
  if (Wire.endTransmission() != 0) {
    return false;
  }

  delay(15);
  lcd_write4bits(0x30);
  delay(5);
  lcd_write4bits(0x30);
  delayMicroseconds(150);
  lcd_write4bits(0x30);
  lcd_write4bits(0x20);

  lcd_command(LCD_FUNCTION);
  lcd_command(LCD_DISPLAY_ON);
  lcd_command(LCD_CLEAR);
  lcd_command(LCD_ENTRY_MODE);

  return true;
}

static void lcd_clear() {
  lcd_command(LCD_CLEAR);
}

static void lcd_setCursor(uint8_t col, uint8_t row) {
  uint8_t offset = row == 0 ? 0x00 : 0x40;
  lcd_command(0x80 | (col + offset));
}

static void lcd_print(const char* str) {
  while (*str) {
    lcd_data(*str++);
  }
}

static void lcd_setBacklight(bool on) {
  g_lcdBacklight = on ? LCD_BACKLIGHT : 0;
  Wire.beginTransmission(DISPLAY_LCD_ADDR);
  Wire.write(g_lcdBacklight);
  Wire.endTransmission();
}

// ═══════════════════════════════════════════════════════════════════
// SERIAL FALLBACK DRIVER
// ═══════════════════════════════════════════════════════════════════

static void serial_print(int line, const char* text) {
  Serial.print(F("[DISPLAY L"));
  Serial.print(line);
  Serial.print(F("] "));
  Serial.println(text);
}

// Forward declarations for public API used below.
bool display_init();
const char* display_getTypeName();

// ═══════════════════════════════════════════════════════════════════
// E-INK DRIVER
// ═══════════════════════════════════════════════════════════════════

#if ENABLE_EINK_DISPLAY && !USE_XIAO_SX1262
static bool eink_init() {
  SPI.begin(EINK_SCLK_PIN, -1, EINK_MOSI_PIN, EINK_CS_PIN);

  // Keep init conservative for broad panel compatibility.
  g_eink.init(115200, true, 2, false);
  g_eink.setRotation(1);
  g_eink.setTextColor(GxEPD_BLACK);
  g_eink.setFont(&FreeMonoBold9pt7b);
  g_eink.setFullWindow();

  g_eink.firstPage();
  do {
    g_eink.fillScreen(GxEPD_WHITE);
    g_eink.setCursor(6, 24);
    g_eink.print("LoraMeister");
    g_eink.setCursor(6, 50);
    g_eink.print("E-ink init OK");
  } while (g_eink.nextPage());

  g_einkLastRefreshMs = millis();
  g_einkForceFullRefresh = false;
  return true;
}

static void eink_renderStatus(const char* role, unsigned long lastTxMs, int rssi, bool alarm, bool forceRefresh) {
  unsigned long now = millis();
  if (!forceRefresh && (now - g_einkLastRefreshMs < EINK_REFRESH_INTERVAL_MS)) {
    return;
  }

  g_eink.setFullWindow();
  g_eink.firstPage();
  do {
    char line[32];
    g_eink.fillScreen(GxEPD_WHITE);

    g_eink.setCursor(6, 24);
    g_eink.print("Zignalmeister 2000");

    g_eink.setCursor(6, 52);
    snprintf(line, sizeof(line), "ROLE: %s", role ? role : "UNKNOWN");
    g_eink.print(line);

    g_eink.setCursor(6, 80);
    unsigned long lastTxSec = (lastTxMs > 0) ? (lastTxMs / 1000UL) : 0;
    snprintf(line, sizeof(line), "LAST TX: %lus", lastTxSec);
    g_eink.print(line);

    g_eink.setCursor(6, 108);
    snprintf(line, sizeof(line), "RSSI: %d dBm", rssi);
    g_eink.print(line);

    g_eink.setCursor(6, 136);
    g_eink.print(alarm ? "STATUS: ALARM" : "STATUS: OK");
  } while (g_eink.nextPage());

  g_einkLastRefreshMs = now;
}
#endif

void display_einkFullRefresh() {
#if ENABLE_EINK_DISPLAY && !USE_XIAO_SX1262
  g_einkForceFullRefresh = true;
#endif
}

void display_einkPartialUpdate() {
#if ENABLE_EINK_DISPLAY && !USE_XIAO_SX1262
  // Initial implementation: partial refresh falls back to safe full refresh.
  g_einkForceFullRefresh = true;
#endif
}

void display_showStatus(const char* role, unsigned long lastTxMs, int rssi, bool alarm) {
  if (!g_displayInitialized) {
    display_init();
  }

  switch (g_displayType) {
    case DISPLAY_EINK_SPI:
#if ENABLE_EINK_DISPLAY && !USE_XIAO_SX1262
      eink_renderStatus(role, lastTxMs, rssi, alarm, g_einkForceFullRefresh);
      g_einkForceFullRefresh = false;
#endif
      break;
    case DISPLAY_LCD_I2C:
    case DISPLAY_SERIAL:
    case DISPLAY_NONE:
    default:
      // Existing LCD/Serial paths are handled by their own display layers.
      break;
  }
}

// ═══════════════════════════════════════════════════════════════════
// DISPLAY HAL PUBLIC API
// ═══════════════════════════════════════════════════════════════════

bool display_init() {
  if (g_displayInitialized) {
    return true;
  }

  Serial.println();
  Serial.println(F("======================================================="));
  Serial.println(F("         DISPLAY HAL - Auto Detection                 "));
  Serial.println(F("======================================================="));

  // Try LCD (highest priority)
  if (platform_hasCapability(CAP_LCD_I2C)) {
    Serial.print(F("Scanning for LCD at 0x"));
    Serial.print(DISPLAY_LCD_ADDR, HEX);
    Serial.print(F("... "));

    if (lcd_init()) {
      Serial.println(F("FOUND"));
      g_displayType = DISPLAY_LCD_I2C;
      g_displayInitialized = true;

      lcd_clear();
      lcd_setCursor(2, 0);
      lcd_print("LoraMeister");
      lcd_setCursor(4, 1);
      char ver[10];
      snprintf(ver, sizeof(ver), "v%s", PROJECT_VERSION);
      lcd_print(ver);

      Serial.println(F("LCD I2C 16x2 initialized"));
      Serial.println(F("=======================================================\n"));
      return true;
    }
    Serial.println(F("not found"));
  }

  // Try E-ink (second priority)
#if ENABLE_EINK_DISPLAY && !USE_XIAO_SX1262
  Serial.println(F("Trying E-ink SPI..."));
  if (eink_init()) {
    Serial.println(F("[DISPLAY] E-ink init OK"));
    g_displayType = DISPLAY_EINK_SPI;
    g_displayInitialized = true;
    Serial.println(F("=======================================================\n"));
    return true;
  }
  Serial.println(F("[DISPLAY] E-ink init FAILED"));
#endif

  // Fallback to Serial console
  Serial.println(F("No hardware display found - Serial fallback"));
  g_displayType = DISPLAY_SERIAL;
  g_displayInitialized = true;

  Serial.println(F("=======================================================\n"));
  return true;
}

DisplayType display_getType() {
  return g_displayType;
}

const char* display_getTypeName() {
  switch (g_displayType) {
    case DISPLAY_LCD_I2C:  return "LCD I2C";
    case DISPLAY_EINK_SPI: return "E-ink SPI";
    case DISPLAY_SERIAL:   return "Serial Console";
    default:               return "None";
  }
}

void display_clear() {
  switch (g_displayType) {
    case DISPLAY_LCD_I2C:
      lcd_clear();
      break;
    case DISPLAY_EINK_SPI:
      display_einkFullRefresh();
      break;
    case DISPLAY_SERIAL:
      Serial.println(F("[DISPLAY] --- CLEAR ---"));
      break;
    default:
      break;
  }
  g_displayLine0[0] = '\0';
  g_displayLine1[0] = '\0';
}

void display_print(int line, const char* text) {
  if (!g_displayInitialized) {
    display_init();
  }

  line = (line < 0) ? 0 : (line > 1 ? 1 : line);

  char* lineBuffer = (line == 0) ? g_displayLine0 : g_displayLine1;
  if (strcmp(lineBuffer, text) == 0) {
    return;
  }

  strncpy(lineBuffer, text, DISPLAY_LINE_MAX_LEN);
  lineBuffer[DISPLAY_LINE_MAX_LEN] = '\0';

  switch (g_displayType) {
    case DISPLAY_LCD_I2C: {
      lcd_setCursor(0, line);
      char padded[DISPLAY_LCD_COLS + 1];
      snprintf(padded, sizeof(padded), "%-16s", text);
      lcd_print(padded);
      break;
    }
    case DISPLAY_EINK_SPI:
      // Show two-line compatibility text on e-ink when generic print API is used.
      display_showStatus((line == 0) ? text : g_displayLine0, millis(), 0, false);
      break;
    case DISPLAY_SERIAL:
      serial_print(line, text);
      break;
    default:
      break;
  }
}

void display_printf(int line, const char* format, ...) {
  char buffer[DISPLAY_LINE_MAX_LEN + 1];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  display_print(line, buffer);
}

void display_update() {
  // No-op for LCD (immediate writes)
}

void display_setBacklight(bool on) {
  if (g_displayType == DISPLAY_LCD_I2C) {
    lcd_setBacklight(on);
  }
}

bool display_isAvailable() {
  return g_displayInitialized && (g_displayType != DISPLAY_NONE);
}

bool display_isHardware() {
  return (g_displayType == DISPLAY_LCD_I2C) || (g_displayType == DISPLAY_EINK_SPI);
}

// ═══════════════════════════════════════════════════════════════════
// CONVENIENCE LAYOUTS
// ═══════════════════════════════════════════════════════════════════

void display_loraStatus(int rssi, int snr, int packets, int lossPercent) {
  display_printf(0, "RSSI:%d SNR:%d", rssi, snr);
  display_printf(1, "Pkts:%d Loss:%d%%", packets, lossPercent);
}

void display_deviceInfo(uint8_t addr, const char* role, unsigned long tx, unsigned long rx) {
  display_printf(0, "Addr:%d %s", addr, role);
  display_printf(1, "TX:%lu RX:%lu", tx, rx);
}

void display_startup(const char* version) {
  display_clear();
  display_print(0, "LoraMeister");
  display_printf(1, "v%s", version);
}

void display_error(const char* msg) {
  display_clear();
  display_print(0, "ERROR:");
  display_print(1, msg);
}

void display_printInfo() {
  Serial.println();
  Serial.println(F("======================================================="));
  Serial.println(F("         DISPLAY HAL INFORMATION                      "));
  Serial.println(F("======================================================="));
  Serial.print(F("Display Type:   ")); Serial.println(display_getTypeName());
  Serial.print(F("Initialized:    ")); Serial.println(g_displayInitialized ? "Yes" : "No");
  Serial.print(F("Is Hardware:    ")); Serial.println(display_isHardware() ? "Yes" : "No");

  if (g_displayType == DISPLAY_LCD_I2C) {
    Serial.print(F("I2C Address:    0x")); Serial.println(DISPLAY_LCD_ADDR, HEX);
    Serial.print(F("Size:           ")); Serial.print(DISPLAY_LCD_COLS);
    Serial.print("x"); Serial.println(DISPLAY_LCD_ROWS);
  }

  Serial.println(F("=======================================================\n"));
}

#endif // DISPLAY_HAL_H
