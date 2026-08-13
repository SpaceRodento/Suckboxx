/*=====================================================================
  lcd_display.h - LCD Display Manager (16x2 I2C)

  LoraMeister - LCD display manager with multiple layout configurations.

  FEATURES:
  - Multiple display layouts (Sender, Receiver, Light-sensor focused)
  - Easy layout switching
  - Auto-detection of LCD presence
  - Clean separation from TFT display code

  LAYOUTS:
  1. Minimalist Signal  - Simple RSSI display
  2. LoRa Range Testing - RSSI, SNR, packet loss
  3. Light Sensor       - LM393 flash count + alarm
  4. Developer Debug    - Heap, temp, sequence
  5. Mesh Receiver Test - Devices, dBm, hops, source, light, loss
  6. LM393 Detailed     - Brightness, flash count, alarm, sequence
  7. Mesh/RELAY Stats   - Relay forwarding statistics

  HARDWARE:
  - LCD1602 I2C display at address 0x27
  - Connections: SDA=GPIO21, SCL=GPIO22, VCC=5V, GND

  USAGE:
    // In setup():
    initLCDDisplay();
    setLCDLayout(LAYOUT_SENDER_LIGHT);

    // In loop():
    updateLCDDisplay(local, remote, health);

=======================================================================*/

#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#include <Arduino.h>
#include "config.h"
#include "structs.h"

// ESP32 internal temperature sensor (for Layout 4)
#if ENABLE_EXTENDED_TELEMETRY
  extern "C" uint8_t temprature_sens_read();
#endif

#if ENABLE_LCD
  #include "i2c_manager.h"
  #include <LiquidCrystal_I2C.h>

  // LCD instance (address 0x27, 16 columns, 2 rows)
  LiquidCrystal_I2C lcd(0x27, 16, 2);
#endif

// ═══════════════════════════════════════════════════════════════════
// DISPLAY LAYOUT TYPES
// ═══════════════════════════════════════════════════════════════════

enum LCDLayout {
  LAYOUT_1,    // Minimalist Signal - Simple, stylish RSSI display
  LAYOUT_2,    // LoRa Range Testing - RSSI, SNR, packet loss, statistics
  LAYOUT_3,    // Light Sensor Monitor - LM393 ADC value, alarm status, sequence
  LAYOUT_4,    // Developer Debug - All states, heap, temperature, sequence
  LAYOUT_5,    // Mesh Receiver Testing - Devices, dBm, hops, source, light, loss
  LAYOUT_6,    // LM393 Detailed Monitor - Brightness + Flash count + Alarm
  LAYOUT_7,    // Mesh Network / RELAY Statistics
  LAYOUT_8     // Microphone Level (MAX4466)
};

// Legacy layout names (for backwards compatibility)
#define LAYOUT_SENDER_DEFAULT LAYOUT_1
#define LAYOUT_SENDER_LIGHT LAYOUT_3
#define LAYOUT_RECEIVER LAYOUT_2

// ═══════════════════════════════════════════════════════════════════
// GLOBAL STATE
// ═══════════════════════════════════════════════════════════════════

#if ENABLE_LCD
  static bool lcdPresent = false;
  static LCDLayout currentLayout = LAYOUT_1;  // Default to Layout 1 (Minimalist)
  static unsigned long lastLCDUpdate = 0;
  static const unsigned long LCD_UPDATE_INTERVAL = 500;  // Update every 500ms
#endif

// ═══════════════════════════════════════════════════════════════════
// INITIALIZATION
// ═══════════════════════════════════════════════════════════════════

/**
 * Initialize LCD display with auto-detection
 */
inline void initLCDDisplay() {
  #if ENABLE_LCD
    ensureI2CInitialized();  // Initialize I2C bus first!

    // Auto-detect LCD to prevent hanging if not connected
    if (!isI2CDevicePresent(I2C_LCD_ADDRESS)) {
      Serial.println("⚠️  LCD not found at 0x27 - LCD disabled");
      Serial.print("   (Check wiring: SDA=GPIO");
      Serial.print(I2C_SDA_PIN);
      Serial.print(", SCL=GPIO");
      Serial.print(I2C_SCL_PIN);
      Serial.println(", VCC, GND)");
      lcdPresent = false;
      return;
    }

    lcdPresent = true;
    Serial.println("DEBUG: LCD found at 0x27, initializing...");

    // Re-initialize I2C with correct pins before lcd.init()
    // Some LiquidCrystal_I2C versions call Wire.begin() internally without pins
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Serial.println("DEBUG: Wire.begin() called with correct pins");

    lcd.init();
    Serial.println("DEBUG: lcd.init() done");

    lcd.clear();
    lcd.backlight();
    Serial.println("DEBUG: lcd.backlight() done");

    // Display loading screen
    lcd.setCursor(2, 0);
    lcd.print("LoraMeister");
    lcd.setCursor(5, 1);
    lcd.print("v0.1.0");
    Serial.println("DEBUG: Text written to LCD");

    Serial.println("✓ LCD Display initialized");
    Serial.print("  Address: 0x27 (I2C), SDA=GPIO");
    Serial.print(I2C_SDA_PIN);
    Serial.print(", SCL=GPIO");
    Serial.println(I2C_SCL_PIN);
    Serial.println("  Size: 16x2 characters");
  #else
    Serial.println("⚠️  LCD disabled in config.h");
  #endif
}

/**
 * Check if LCD is present and working
 */
inline bool isLCDPresent() {
  #if ENABLE_LCD
    return lcdPresent;
  #else
    return false;
  #endif
}

// ═══════════════════════════════════════════════════════════════════
// LAYOUT SELECTION
// ═══════════════════════════════════════════════════════════════════

/**
 * Set LCD display layout
 */
inline void setLCDLayout(LCDLayout layout) {
  #if ENABLE_LCD
    currentLayout = layout;

    // Clear display when changing layout
    if (lcdPresent) {
      lcd.clear();
    }

    // Log layout change
    const char* layoutName = "";
    switch (layout) {
      case LAYOUT_1: layoutName = "LAYOUT_1 (Minimalist Signal)"; break;
      case LAYOUT_2: layoutName = "LAYOUT_2 (LoRa Range Test)"; break;
      case LAYOUT_3: layoutName = "LAYOUT_3 (Light Sensor)"; break;
      case LAYOUT_4: layoutName = "LAYOUT_4 (Developer Debug)"; break;
      case LAYOUT_5: layoutName = "LAYOUT_5 (Mesh Receiver Test)"; break;
      case LAYOUT_6: layoutName = "LAYOUT_6 (LM393 Detailed Monitor)"; break;
      case LAYOUT_7: layoutName = "LAYOUT_7 (Mesh/RELAY Stats)"; break;
      case LAYOUT_8: layoutName = "LAYOUT_8 (Microphone Level)"; break;
    }
    Serial.print("📺 LCD layout changed to: ");
    Serial.println(layoutName);
  #endif
}

/**
 * Get current layout
 */
inline LCDLayout getLCDLayout() {
  #if ENABLE_LCD
    return currentLayout;
  #else
    return LAYOUT_1;
  #endif
}

// ═══════════════════════════════════════════════════════════════════
// HELPER FUNCTIONS
// ═══════════════════════════════════════════════════════════════════

#if ENABLE_LCD
/**
 * Print right-aligned text on LCD
 */
inline void lcdPrintRight(int row, const char* text, int maxWidth = 16) {
  int len = strlen(text);
  if (len > maxWidth) len = maxWidth;
  int col = maxWidth - len;
  lcd.setCursor(col, row);
  lcd.print(text);
}

/**
 * Clear a specific row on LCD
 */
inline void lcdClearRow(int row) {
  lcd.setCursor(0, row);
  lcd.print("                ");  // 16 spaces
}
#endif

// ═══════════════════════════════════════════════════════════════════
// LAYOUT RENDERERS
// ═══════════════════════════════════════════════════════════════════

#if ENABLE_LCD

/**
 * LAYOUT_1: Minimalist Signal Display
 * Line 1: "  LoraMeister   "
 * Line 2: " RSSI: -85 dBm  "
 *
 * Simple, clean display focusing on signal strength (dBm reading)
 */
inline void renderLayout1(DeviceState& local, DeviceState& remote, HealthMonitor& health) {
  // Line 1: Centered project name
  lcd.setCursor(0, 0);
  lcd.print("  LoraMeister   ");

  // Line 2: RSSI in dBm (centered with spacing for style)
  lcd.setCursor(0, 1);
  char line2[17];
  if (remote.rssi != 0) {
    snprintf(line2, 17, " RSSI:%4d dBm  ", remote.rssi);
  } else {
    snprintf(line2, 17, " RSSI: --- dBm  ");
  }
  lcd.print(line2);
}

/**
 * LAYOUT_2: LoRa Range Testing
 * Line 1: "RSSI:-85 SNR:10 "
 * Line 2: "Loss:5%  Pkt:456"
 *
 * Optimized for LoRa range/coverage testing
 * Shows signal quality (RSSI, SNR), packet loss, and total packet count
 */
inline void renderLayout2(DeviceState& local, DeviceState& remote, HealthMonitor& health) {
  // Line 1: RSSI and SNR (signal quality indicators)
  lcd.setCursor(0, 0);
  char line1[17];
  if (remote.rssi != 0) {
    snprintf(line1, 17, "RSSI:%4d SNR:%2d ", remote.rssi, remote.snr);
  } else {
    snprintf(line1, 17, "RSSI:---  SNR:--");
  }
  lcd.print(line1);

  // Line 2: Packet loss percentage and total packet count
  lcd.setCursor(0, 1);
  char line2[17];
  float lossPercent = getPacketLoss(health);
  snprintf(line2, 17, "Loss:%2d%% Pkt:%04d",
    (int)lossPercent,
    remote.messageCount % 10000
  );
  lcd.print(line2);
}

/**
 * LAYOUT_3: Light + Signal Monitor
 * Line 1: "L:HI -75dB H:1 "  (Light state, RSSI, Hops)
 * Line 2: "R:1234 Fl:03 A:N"  (ADC raw, flash count, alarm)
 *
 * Yhdistää valosensorin tilan, signaalivoimakkuuden ja hyppyluvun.
 * Sopii kenttätestaukseen ja kantamatestaukseen valosensorin kanssa.
 */
inline void renderLayout3(DeviceState& local, DeviceState& remote, HealthMonitor& health) {
  #if ENABLE_LIGHT_DETECTION
    LM393State lm393 = getLM393StateForDisplay();
  #endif

  // Line 1: Light state + RSSI + Hops
  lcd.setCursor(0, 0);
  char line1[17];

  #if ENABLE_LIGHT_DETECTION
    const char* lightStr = "N/A";
    if (lm393.sensorAvailable) {
      lightStr = lm393.digitalState ? "HI" : "LO";
    }
  #else
    const char* lightStr = "--";
  #endif

  if (remote.rssi != 0) {
    snprintf(line1, 17, "L:%-2s %4ddB H:%d",
      lightStr, remote.rssi, remote.hopCount);
  } else {
    snprintf(line1, 17, "L:%-2s  ---dB H:-", lightStr);
  }
  lcd.print(line1);

  // Line 2: Flash count and alarm status
  lcd.setCursor(0, 1);

  #if ENABLE_LIGHT_DETECTION
    char line2[17];
    snprintf(line2, 17, "R:%4d Fl:%02d A:%c",
      lm393.adcValue,
      lm393.flashCount % 100,
      lm393.alarmActive ? 'Y' : 'N'
    );
    lcd.print(line2);
  #else
    lcd.print("Alarm: ---      ");
  #endif
}

/**
 * LAYOUT_5: Mesh Receiver Testing
 * Line 1: "N:2  -75dBm H:1 "  (Devices, RSSI, Hops)
 * Line 2: "#3  L:DK  Ls: 2% "  (Source, Light, Loss)
 *
 * Optimoitu mesh-verkon testaukseen emolaitteelta (receiver).
 * Näyttää yhdistetyt laitteet, signaalivoimakkuuden, hyppyluvun
 * (0=suora, 1+=relayn kautta), lähteen tunnuksen, valosensorin
 * tilan ja pakettihäviön.
 *
 * Display format:
 *   N   = Active device count (mesh nodes online)
 *   dBm = Signal strength (RSSI)
 *   H   = Hop count (0=direct, 1+=via relay - KEY metric)
 *   #   = Source device ID
 *   L   = Light sensor state (DK/LT/AMB/ALM/N-A)
 *   Ls  = Packet loss percentage
 */
inline void renderLayout5(DeviceState& local, DeviceState& remote, HealthMonitor& health) {
  // Access device tracker for node count
  extern DeviceTracker deviceTracker;
  extern int getActiveDeviceCount(DeviceTracker& tracker);

  int activeDevices = getActiveDeviceCount(deviceTracker);

  // Line 1: Devices + RSSI + Hops
  // Format: "N:2  -75dBm H:1 " or "N:0 -120dBm H:0 "
  lcd.setCursor(0, 0);
  char line1[17];
  if (remote.rssi != 0) {
    snprintf(line1, 17, "N:%d %4ddBm H:%d ",
      activeDevices,
      remote.rssi,
      remote.hopCount
    );
  } else {
    snprintf(line1, 17, "N:%d  ---dBm H:- ",
      activeDevices
    );
  }
  lcd.print(line1);

  // Line 2: Source + Light + Loss
  // Format: "#3  L:DK  Ls: 2%" or "#0  L:N/A Ls: 0%"
  lcd.setCursor(0, 1);
  char line2[17];

  // Light sensor abbreviation (max 3 chars)
  const char* lightAbbr = "---";
  #if ENABLE_LIGHT_DETECTION
  {
    LM393State lm393 = getLM393StateForDisplay();
    if (lm393.sensorAvailable) {
      if (lm393.alarmActive)       lightAbbr = "ALM";
      else if (lm393.isLightDetected) lightAbbr = "LIT";
      else if (lm393.isDark)       lightAbbr = "DK";
      else                         lightAbbr = "AMB";
    } else {
      lightAbbr = "N/A";
    }
  }
  #endif

  float lossPercent = getPacketLoss(health);
  snprintf(line2, 17, "#%-2d L:%-3s Ls:%2d%%",
    remote.sourceId,
    lightAbbr,
    (int)lossPercent
  );
  lcd.print(line2);
}

/**
 * LAYOUT_6: LM393 Detailed Monitor
 * Line 1: "DARK  Bri:  0%  " (Light state + brightness percentage)
 * Line 2: "Fl:003 A:NO S:42" (Flash count + alarm + sequence)
 *
 * Detailed LM393 sensor view with brightness, flash count, alarm status
 * Requires: ENABLE_LIGHT_DETECTION
 *
 * Display format:
 *   Light state: DARK / LIGHT / AMB (based on analog thresholds)
 *   Bri = Brightness percentage (0-100%)
 *   Fl  = Flash count
 *   A   = Alarm active (YES/NO)
 *   S   = Sequence number
 */
inline void renderLayout6(DeviceState& local, DeviceState& remote, HealthMonitor& health) {
  #if ENABLE_LIGHT_DETECTION
    LM393State lm393 = getLM393StateForDisplay();

    // Line 1: Light state + Brightness
    lcd.setCursor(0, 0);
    char line1[17];
    const char* lightState;
    if (lm393.isLightDetected) {
      lightState = "LIGHT";
    } else if (lm393.isDark) {
      lightState = "DARK";
    } else {
      lightState = "AMB";
    }
    snprintf(line1, 17, "%-5s Bri:%3d%%  ",
      lightState,
      lm393.brightness
    );
    lcd.print(line1);

    // Line 2: Flash count + Alarm + Sequence
    lcd.setCursor(0, 1);
    char line2[17];
    snprintf(line2, 17, "Fl:%03d A:%-2s S:%02d",
      lm393.flashCount % 1000,
      lm393.alarmActive ? "Y" : "N",
      local.sequenceNumber % 100
    );
    lcd.print(line2);
  #else
    lcd.setCursor(0, 0);
    lcd.print("Light sensor    ");
    lcd.setCursor(0, 1);
    lcd.print("not enabled     ");
  #endif
}

/**
 * LAYOUT_4: Developer Debug
 * Line 1: "S:123 L:ON T:Y  "
 * Line 2: "Heap:245 T:45C  "
 *
 * Comprehensive debug info for development and troubleshooting
 * Shows sequence, LED, touch, heap memory, and internal temperature
 */
inline void renderLayout4(DeviceState& local, DeviceState& remote, HealthMonitor& health) {
  // Line 1: Sequence, LED state, Touch state
  lcd.setCursor(0, 0);
  char line1[17];
  snprintf(line1, 17, "S:%03d L:%s T:%s  ",
    local.sequenceNumber % 1000,
    local.ledState ? "ON" : "OF",
    local.touchState ? "Y" : "N"
  );
  lcd.print(line1);

  // Line 2: Free heap memory and internal temperature
  lcd.setCursor(0, 1);
  char line2[17];

  #if ENABLE_EXTENDED_TELEMETRY
    // ESP32 internal temperature sensor (declared at file scope)
    uint8_t raw = temprature_sens_read();
    float tempC = (raw - 32) / 1.8;
    snprintf(line2, 17, "Heap:%3dK T:%2dC ",
      (int)(ESP.getFreeHeap() / 1024),
      (int)tempC
    );
  #else
    snprintf(line2, 17, "Heap:%3dK       ",
      (int)(ESP.getFreeHeap() / 1024)
    );
  #endif

  lcd.print(line2);
}

/**
 * LAYOUT_7: Mesh Network / RELAY Statistics
 * Line 1: "RX:123 FWD:98  "  (Messages received and forwarded)
 * Line 2: "DRP:5 HOP:2.3  "  (Dropped messages and average hop count)
 *
 * Optimized for mesh network and RELAY device testing
 * Shows relay statistics: received, forwarded, dropped, average hops
 */
inline void renderLayout7(DeviceState& local, DeviceState& remote, HealthMonitor& health) {
  #if ENABLE_MESH_NETWORK
    // Need access to relayStats - declare as extern
    extern RelayStats relayStats;

    // Line 1: Messages received and forwarded
    lcd.setCursor(0, 0);
    char line1[17];
    snprintf(line1, 17, "RX:%-4lu FWD:%-3lu ",
      relayStats.messagesReceived % 10000,
      relayStats.messagesRelayed % 1000
    );
    lcd.print(line1);

    // Line 2: Dropped messages and average hop count
    lcd.setCursor(0, 1);
    char line2[17];
    float avgHops = 0.0;
    if (relayStats.messagesReceived > 0) {
      avgHops = (float)relayStats.totalHops / (float)relayStats.messagesReceived;
    }
    snprintf(line2, 17, "DRP:%-2lu HOP:%1.1f  ",
      relayStats.messagesDropped % 100,
      avgHops
    );
    lcd.print(line2);
  #else
    // Mesh network not enabled
    lcd.setCursor(0, 0);
    lcd.print("Mesh network    ");
    lcd.setCursor(0, 1);
    lcd.print("not enabled     ");
  #endif
}

/**
 * LAYOUT_8: Microphone Level (MAX4466)
 * Line 1: "MIC: 65dB  -80dB"  (dB level + RSSI)
 * Line 2: "########        "  (bar graph of volume)
 *
 * Shows microphone volume level as dB and visual bar.
 * Uses data from microphone.h (g_micDB, g_micPeakToPeak)
 * For RECEIVER: shows remote mic data from LoRa payload
 */
inline void renderLayout8(DeviceState& local, DeviceState& remote, HealthMonitor& health) {
  #if ENABLE_MICROPHONE
    extern uint8_t g_micDB;
    extern uint16_t g_micPeakToPeak;
  #endif

  // Line 1: dB reading + RSSI
  lcd.setCursor(0, 0);
  char line1[17];

  #if ENABLE_MICROPHONE
    uint8_t db = g_micDB;
    // On RECEIVER: use remote micDB if available
    if (remote.rssi != 0 && remote.micDB > 0) {
      db = remote.micDB;
    }
    if (remote.rssi != 0) {
      snprintf(line1, 17, "MIC:%3ddB %4ddB", db, remote.rssi);
    } else {
      snprintf(line1, 17, "MIC:%3ddB  ---dB", db);
    }
  #else
    snprintf(line1, 17, "MIC:--dB   ---dB");
  #endif
  lcd.print(line1);

  // Line 2: Bar graph (16 chars wide)
  lcd.setCursor(0, 1);
  char line2[17];
  memset(line2, ' ', 16);
  line2[16] = '\0';

  #if ENABLE_MICROPHONE
    uint8_t barDb = db;
    // Map dB to bar (0-16 chars)
    uint8_t bars = 0;
    if (barDb > MIC_DB_MIN) {
      bars = map(barDb, MIC_DB_MIN, MIC_DB_MAX, 0, 16);
      if (bars > 16) bars = 16;
    }
    for (uint8_t i = 0; i < bars; i++) {
      line2[i] = '#';
    }
  #endif
  lcd.print(line2);
}

#endif // ENABLE_LCD

// ═══════════════════════════════════════════════════════════════════
// MAIN UPDATE FUNCTION
// ═══════════════════════════════════════════════════════════════════

/**
 * Update LCD display based on current layout
 * Call this regularly from loop()
 *
 * @param local Local device state
 * @param remote Remote device state (for receiver)
 * @param health Health monitor data (for receiver)
 * @param forceUpdate Force immediate update (bypass interval check)
 */
inline void updateLCDDisplay(DeviceState& local, DeviceState& remote, HealthMonitor& health, bool forceUpdate = false) {
  #if ENABLE_LCD
    if (!lcdPresent) return;

    // Rate limiting (unless forced)
    unsigned long now = millis();
    if (!forceUpdate && (now - lastLCDUpdate < LCD_UPDATE_INTERVAL)) {
      return;
    }
    lastLCDUpdate = now;

    // Render appropriate layout
    switch (currentLayout) {
      case LAYOUT_1:
        renderLayout1(local, remote, health);
        break;

      case LAYOUT_2:
        renderLayout2(local, remote, health);
        break;

      case LAYOUT_3:
        renderLayout3(local, remote, health);
        break;

      case LAYOUT_4:
        renderLayout4(local, remote, health);
        break;

      case LAYOUT_5:
        renderLayout5(local, remote, health);
        break;

      case LAYOUT_6:
        renderLayout6(local, remote, health);
        break;

      case LAYOUT_7:
        renderLayout7(local, remote, health);
        break;

      case LAYOUT_8:
        renderLayout8(local, remote, health);
        break;
    }
  #endif
}

/**
 * Show a temporary message on LCD (clears after delay)
 * Useful for alerts, status messages
 */
inline void showLCDMessage(const char* line1, const char* line2 = nullptr, int displayTimeMs = 2000) {
  #if ENABLE_LCD
    if (!lcdPresent) return;

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(line1);

    if (line2 != nullptr) {
      lcd.setCursor(0, 1);
      lcd.print(line2);
    }

    delay(displayTimeMs);
    lcd.clear();
  #endif
}

/**
 * Display startup animation
 * Shows device role and ID
 */
inline void showLCDStartup(bool isReceiver, uint8_t deviceID) {
  #if ENABLE_LCD
    if (!lcdPresent) return;

    lcd.clear();

    // Line 1: Role
    lcd.setCursor(0, 0);
    if (isReceiver) {
      lcd.print("  RECEIVER  ");
    } else {
      lcd.print("   SENDER   ");
    }

    // Line 2: Device ID
    lcd.setCursor(0, 1);
    char idStr[17];
    snprintf(idStr, 17, "   ID: %3d", deviceID);
    lcd.print(idStr);

    delay(2000);
    lcd.clear();
  #endif
}

#endif // LCD_DISPLAY_H
