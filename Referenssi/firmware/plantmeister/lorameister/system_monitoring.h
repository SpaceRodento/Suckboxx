/*=====================================================================
  system_monitoring.h - Unified System & Connection Monitoring

  Combines health monitoring, performance tracking, and detailed telemetry
  into a single comprehensive module for ESP32 system oversight.

  FEATURES:

  1. CONNECTION HEALTH MONITORING
     - Connection state machine (UNKNOWN → CONNECTED → WEAK → LOST)
     - RSSI statistics (min/max/average)
     - Packet loss detection with sequence numbers
     - Automatic recovery attempts
     - Configure: HEALTH_* settings in config.h (lines 120-145)

  2. PERFORMANCE MONITORING
     - CPU usage estimation (loop frequency)
     - Memory tracking (free heap, minimum heap, leak detection)
     - System uptime
     - Performance warnings
     - Configure: ENABLE_PERFORMANCE_MONITOR in config.h (line 98)

  3. DETAILED TELEMETRY (Optional)
     - SNR tracking (Signal-to-Noise Ratio)
     - Packet timing analysis (interval, jitter)
     - Loss streaks detection
     - Internal temperature sensor
     - Transmission statistics (ACK rate)
     - Configure: ENABLE_PACKET_STATS, ENABLE_EXTENDED_TELEMETRY in config.h

  CONFIGURATION (config.h):
  ═══════════════════════════════════════════════════════════════════

  // Performance monitoring (line 98)
  #define ENABLE_PERFORMANCE_MONITOR true
  #define PERF_REPORT_INTERVAL 60000  // 60 seconds

  // Connection health thresholds (lines 120-145)
  #define HEALTH_WEAK_TIMEOUT_MS 3000
  #define HEALTH_LOST_TIMEOUT_MS 8000
  #define HEALTH_WEAK_RSSI_DBM -100
  #define HEALTH_CRITICAL_RSSI_DBM -110

  // Detailed telemetry (lines 100-105)
  #define ENABLE_PACKET_STATS true
  #define ENABLE_EXTENDED_TELEMETRY true
  #define PACKET_STATS_INTERVAL 60000  // 60 seconds

  USAGE:
  ═══════════════════════════════════════════════════════════════════

  HealthMonitor health;
  DeviceState remoteDevice;

  void setup() {
    initSystemMonitoring(health);  // Initialize all monitoring
  }

  void loop() {
    // Update monitoring every loop
    updatePerformanceMetrics();

    // When packet received (receiver side)
    updateConnectionState(health, remoteDevice);
    recordPacketReceived(remoteDevice.rssi, remoteDevice.snr, seq);

    // Print periodic reports
    printPerformanceReport();
    printDetailedReport(health);
  }

  VERSION HISTORY:
  ═══════════════════════════════════════════════════════════════════

  2026-01-05: Unified health_monitor.h, performance_monitor.h,
              and detailed_telemetry.h into single module
              - Clearer structure with labeled sections
              - English comments throughout
              - Eliminated duplication between modules
              - Added config.h line references

=======================================================================*/

#ifndef SYSTEM_MONITORING_H
#define SYSTEM_MONITORING_H

#include <WiFi.h>
#include "config.h"
#include "structs.h"

#ifdef __cplusplus
extern "C" {
#endif

// ESP32 internal temperature sensor (if available)
uint8_t temprature_sens_read();

#ifdef __cplusplus
}
#endif

// ═══════════════════════════════════════════════════════════════════
// SECTION 1: CONNECTION HEALTH MONITORING
// ═══════════════════════════════════════════════════════════════════

// Watchdog configuration (from config.h lines 120-145)
#ifndef HEALTH_WEAK_TIMEOUT_MS
  #define HEALTH_WEAK_TIMEOUT_MS 3000
#endif
#ifndef HEALTH_LOST_TIMEOUT_MS
  #define HEALTH_LOST_TIMEOUT_MS 8000
#endif
#ifndef HEALTH_WEAK_RSSI_DBM
  #define HEALTH_WEAK_RSSI_DBM -100
#endif
#ifndef HEALTH_CRITICAL_RSSI_DBM
  #define HEALTH_CRITICAL_RSSI_DBM -110
#endif
#ifndef HEALTH_RECOVERY_INTERVAL_MS
  #define HEALTH_RECOVERY_INTERVAL_MS 15000
#endif
#ifndef HEALTH_MAX_RECOVERY_ATTEMPTS
  #define HEALTH_MAX_RECOVERY_ATTEMPTS 3
#endif

WatchdogConfig watchdogCfg = {
  .weakTimeout = HEALTH_WEAK_TIMEOUT_MS,
  .lostTimeout = HEALTH_LOST_TIMEOUT_MS,
  .weakRssiThreshold = HEALTH_WEAK_RSSI_DBM,
  .criticalRssiThreshold = HEALTH_CRITICAL_RSSI_DBM,
  .recoveryInterval = HEALTH_RECOVERY_INTERVAL_MS,
  .maxRecoveryAttempts = HEALTH_MAX_RECOVERY_ATTEMPTS
};

/**
 * @brief Initialize health monitor
 * @param health HealthMonitor structure to initialize
 */
inline void initHealthMonitor(HealthMonitor& health) {
  health.state = CONN_UNKNOWN;
  health.stateChangeTime = millis();
  health.connectedSince = 0;

  health.rssiMin = -30;   // Start with unrealistically good value (will be updated on first packet)
  health.rssiMax = -120;  // Start with unrealistically bad value
  health.rssiSum = 0;
  health.rssiSamples = 0;

  health.firstSeq = -1;        // Not yet received any packets
  health.lastSeq = -1;         // Not yet received any packets
  health.expectedSeq = 0;
  health.packetsReceived = 0;
  health.packetsLost = 0;
  health.packetsDuplicate = 0;

  health.recoveryAttempts = 0;
  health.lastRecoveryAttempt = 0;
  health.maxAttemptsReachedNotified = false;

  health.startTime = millis();

  // Initialize per-device packet tracking (v2.7.1)
  health.totalDevicesTracked = 0;
  for (int i = 0; i < MAX_DEVICE_STATS; i++) {
    health.perDeviceStats[i].sourceId = 0;
    health.perDeviceStats[i].active = false;
    health.perDeviceStats[i].firstSeq = -1;
    health.perDeviceStats[i].lastSeq = -1;
    health.perDeviceStats[i].expectedSeq = 0;
    health.perDeviceStats[i].packetsReceived = 0;
    health.perDeviceStats[i].packetsLost = 0;
    health.perDeviceStats[i].packetsDuplicate = 0;
    health.perDeviceStats[i].lastSeen = 0;
  }

  Serial.println("✓ Health Monitor initialized (with per-device tracking)");
}

/**
 * @brief Update RSSI statistics
 * @param health HealthMonitor structure
 * @param rssi RSSI value in dBm
 */
inline void updateRSSI(HealthMonitor& health, int rssi) {
  // Update min/max
  if (rssi < health.rssiMin || health.rssiSamples == 0) {
    health.rssiMin = rssi;
  }
  if (rssi > health.rssiMax || health.rssiSamples == 0) {
    health.rssiMax = rssi;
  }

  // Update sum for average (use sliding window to prevent overflow)
  health.rssiSum += rssi;
  health.rssiSamples++;

  // Reset statistics every 100 samples to prevent overflow
  if (health.rssiSamples >= 100) {
    health.rssiSum = health.rssiSum / health.rssiSamples;  // Keep average
    health.rssiSamples = 1;
  }
}

/**
 * @brief Get RSSI average
 * @param health HealthMonitor structure
 * @return Average RSSI in dBm
 */
inline int getRSSIAverage(HealthMonitor& health) {
  if (health.rssiSamples == 0) return 0;
  return health.rssiSum / health.rssiSamples;
}

/**
 * @brief Track received packet and update statistics (PER-DEVICE TRACKING v2.7.1)
 *
 * Now tracks each device independently to avoid false packet loss when multiple
 * senders are active. Each device has its own sequence tracking.
 *
 * WRAP-AROUND HANDLING:
 * MeshMessage.sequenceNumber is uint16_t (0-65535) and wraps around.
 * This function detects wrap-around and handles it correctly per device.
 *
 * @param health HealthMonitor structure
 * @param receivedSeq Received sequence number
 * @param sourceId Device ID (0 = unknown source, uses legacy tracking)
 */
inline void trackPacket(HealthMonitor& health, int receivedSeq, uint8_t sourceId = 0) {
  // If sourceId not provided, fall back to legacy single-device tracking
  if (sourceId == 0) {
    // Legacy tracking for backward compatibility
    health.packetsReceived++;
    health.lastSourceId = 0;
    return;
  }

  // Find or create slot for this device
  int deviceSlot = -1;
  for (int i = 0; i < MAX_DEVICE_STATS; i++) {
    if (health.perDeviceStats[i].active && health.perDeviceStats[i].sourceId == sourceId) {
      deviceSlot = i;  // Found existing device
      break;
    }
  }

  // If device not found, allocate new slot
  if (deviceSlot == -1) {
    for (int i = 0; i < MAX_DEVICE_STATS; i++) {
      if (!health.perDeviceStats[i].active) {
        deviceSlot = i;
        health.perDeviceStats[i].active = true;
        health.perDeviceStats[i].sourceId = sourceId;
        health.perDeviceStats[i].firstSeq = receivedSeq;
        health.perDeviceStats[i].lastSeq = receivedSeq;
        health.perDeviceStats[i].expectedSeq = receivedSeq + 1;
        health.perDeviceStats[i].packetsReceived = 1;
        health.perDeviceStats[i].packetsLost = 0;
        health.perDeviceStats[i].packetsDuplicate = 0;
        health.perDeviceStats[i].lastSeen = millis();
        health.totalDevicesTracked++;

        Serial.print("📊 Started tracking device ID:");
        Serial.print(sourceId);
        Serial.print(" at SEQ:");
        Serial.println(receivedSeq);

        // Update legacy tracking
        health.lastSourceId = sourceId;
        health.packetsReceived++;
        return;
      }
    }

    // No free slots available - use legacy tracking
    Serial.print("⚠ No free slots for device ID:");
    Serial.print(sourceId);
    Serial.println(" (using legacy tracking)");
    health.packetsReceived++;
    health.lastSourceId = sourceId;
    return;
  }

  // Get device stats
  PerDevicePacketStats& dev = health.perDeviceStats[deviceSlot];
  dev.lastSeen = millis();

  // Update legacy lastSourceId for display
  health.lastSourceId = sourceId;

  // ═══════════════════════════════════════════════════════════════════
  // WRAP-AROUND DETECTION (uint16_t sequences wrap at 65536)
  // ═══════════════════════════════════════════════════════════════════
  const int WRAP_THRESHOLD_HIGH = 60000;
  const int WRAP_THRESHOLD_LOW = 5536;

  if (dev.expectedSeq > WRAP_THRESHOLD_HIGH && receivedSeq < WRAP_THRESHOLD_LOW) {
    int gapBeforeWrap = 65536 - dev.expectedSeq;
    int gapAfterWrap = receivedSeq;
    int lost = gapBeforeWrap + gapAfterWrap - 1;

    if (lost > 0) {
      dev.packetsLost += lost;
      Serial.print("🔄 ID:");
      Serial.print(sourceId);
      Serial.print(" SEQ wrap: ");
      Serial.print(dev.expectedSeq);
      Serial.print(" → ");
      Serial.print(receivedSeq);
      Serial.print(" (lost:");
      Serial.print(lost);
      Serial.println(")");
    }

    dev.packetsReceived++;
    dev.expectedSeq = receivedSeq + 1;
    dev.lastSeq = receivedSeq;
    health.packetsReceived++;  // Legacy counter
    return;
  }

  // Check for lost packets (gap in sequence)
  if (receivedSeq > dev.expectedSeq) {
    int lost = receivedSeq - dev.expectedSeq;
    dev.packetsLost += lost;

    Serial.print("⚠ ID:");
    Serial.print(sourceId);
    Serial.print(" lost:");
    Serial.print(lost);
    Serial.print(" pkts (SEQ ");
    Serial.print(dev.expectedSeq);
    Serial.print("-");
    Serial.print(receivedSeq - 1);
    Serial.println(")");
  }
  // Check for duplicate
  else if (receivedSeq < dev.expectedSeq) {
    dev.packetsDuplicate++;
    Serial.print("⚠ ID:");
    Serial.print(sourceId);
    Serial.print(" duplicate SEQ:");
    Serial.print(receivedSeq);
    Serial.print(" (expected:");
    Serial.print(dev.expectedSeq);
    Serial.println(")");
    return;  // Don't increment counters for duplicates
  }

  // Update counters
  dev.packetsReceived++;
  dev.expectedSeq = receivedSeq + 1;
  health.packetsReceived++;  // Legacy counter

  // Track highest SEQ
  if (receivedSeq > dev.lastSeq) {
    dev.lastSeq = receivedSeq;
  }
}

/**
 * @brief Get packet loss percentage (based on sequence gaps only)
 *
 * This calculates packet loss ONLY from detected gaps in sequence numbers,
 * starting from the first packet received. Does NOT count packets sent
 * before receiver started.
 *
 * Formula: lost / (received + lost) * 100%
 *
 * Example: First packet SEQ=195, then 197, 199, 201...
 *          Lost: 1 (SEQ 196), 1 (SEQ 198), 1 (SEQ 200) = 3 total
 *          Received: 10
 *          Packet loss: 3 / (10 + 3) = 23.1%
 *
 * @param health HealthMonitor structure
 * @return Packet loss percentage (0-100)
 */
inline float getPacketLoss(HealthMonitor& health) {
  // Aggregate from per-device stats (health.packetsLost is never updated by trackPacket)
  int totalReceived = 0;
  int totalLost = 0;
  for (int i = 0; i < MAX_DEVICE_STATS; i++) {
    if (health.perDeviceStats[i].active) {
      totalReceived += health.perDeviceStats[i].packetsReceived;
      totalLost += health.perDeviceStats[i].packetsLost;
    }
  }
  // Fall back to legacy counters if no per-device tracking active
  if (totalReceived == 0 && health.packetsReceived > 0) {
    return 0.0;  // Legacy path: no loss tracking available
  }
  int totalExpected = totalReceived + totalLost;
  if (totalExpected == 0) return 0.0;
  return (float)totalLost / totalExpected * 100.0;
}

/**
 * @brief Get alternative packet loss based on SEQ range
 *
 * This calculates packet loss based on the full sequence number range seen,
 * which can be useful for debugging but may be misleading if receiver
 * started after sender.
 *
 * Formula: (lastSeq - firstSeq + 1 - received) / (lastSeq - firstSeq + 1) * 100%
 *
 * Example: First SEQ=195, Last SEQ=210, Received=10
 *          Expected in range: 210-195+1 = 16
 *          Lost: 16-10 = 6
 *          Range loss: 6/16 = 37.5%
 *
 * @param health HealthMonitor structure
 * @return Range-based packet loss percentage (0-100)
 */
inline float getPacketLossRange(HealthMonitor& health) {
  if (health.firstSeq < 0 || health.lastSeq < 0) return 0.0;

  int seqRange = health.lastSeq - health.firstSeq + 1;
  if (seqRange <= 0) return 0.0;

  int lostInRange = seqRange - health.packetsReceived;
  return (float)lostInRange / seqRange * 100.0;
}

/**
 * @brief Get AVERAGE packet loss across all tracked devices (v2.7.1)
 *
 * Calculates the average packet loss percentage across all active devices.
 * Useful for getting an overall network quality metric.
 *
 * @param health HealthMonitor structure
 * @return Average packet loss percentage (0-100), or 0.0 if no devices tracked
 */
inline float getAveragePacketLoss(HealthMonitor& health) {
  float totalLoss = 0.0;
  int activeDevices = 0;

  for (int i = 0; i < MAX_DEVICE_STATS; i++) {
    if (health.perDeviceStats[i].active) {
      PerDevicePacketStats& dev = health.perDeviceStats[i];
      int totalExpected = dev.packetsReceived + dev.packetsLost;
      if (totalExpected > 0) {
        float deviceLoss = (float)dev.packetsLost / totalExpected * 100.0;
        totalLoss += deviceLoss;
        activeDevices++;
      }
    }
  }

  if (activeDevices == 0) return 0.0;
  return totalLoss / activeDevices;
}

/**
 * @brief Get WORST packet loss from all tracked devices (v2.7.1)
 *
 * Returns the highest packet loss percentage among all active devices.
 * Useful for identifying problematic devices.
 *
 * @param health HealthMonitor structure
 * @param worstDeviceId Output parameter - ID of device with worst loss (optional)
 * @return Worst packet loss percentage (0-100), or 0.0 if no devices tracked
 */
inline float getWorstPacketLoss(HealthMonitor& health, uint8_t* worstDeviceId = nullptr) {
  float worstLoss = 0.0;
  uint8_t worstId = 0;

  for (int i = 0; i < MAX_DEVICE_STATS; i++) {
    if (health.perDeviceStats[i].active) {
      PerDevicePacketStats& dev = health.perDeviceStats[i];
      int totalExpected = dev.packetsReceived + dev.packetsLost;
      if (totalExpected > 0) {
        float deviceLoss = (float)dev.packetsLost / totalExpected * 100.0;
        if (deviceLoss > worstLoss) {
          worstLoss = deviceLoss;
          worstId = dev.sourceId;
        }
      }
    }
  }

  if (worstDeviceId != nullptr) {
    *worstDeviceId = worstId;
  }

  return worstLoss;
}

/**
 * @brief Get per-device packet loss (v2.7.1)
 *
 * Returns packet loss for a specific device.
 *
 * @param health HealthMonitor structure
 * @param sourceId Device ID to query
 * @return Packet loss percentage for that device, or -1.0 if device not found
 */
inline float getDevicePacketLoss(HealthMonitor& health, uint8_t sourceId) {
  for (int i = 0; i < MAX_DEVICE_STATS; i++) {
    if (health.perDeviceStats[i].active && health.perDeviceStats[i].sourceId == sourceId) {
      PerDevicePacketStats& dev = health.perDeviceStats[i];
      int totalExpected = dev.packetsReceived + dev.packetsLost;
      if (totalExpected == 0) return 0.0;
      return (float)dev.packetsLost / totalExpected * 100.0;
    }
  }
  return -1.0;  // Device not found
}

/**
 * @brief Get connection state as string
 * @param state Connection state
 * @return String representation
 */
inline const char* getConnectionStateString(ConnectionState state) {
  switch (state) {
    case CONN_UNKNOWN:    return "UNKNOWN";
    case CONN_CONNECTING: return "CONNECT";
    case CONN_CONNECTED:  return "OK";
    case CONN_WEAK:       return "WEAK";
    case CONN_LOST:       return "LOST";
    default:              return "ERROR";
  }
}

/**
 * @brief Get connection state icon
 * @param state Connection state
 * @return ASCII character icon
 */
inline char getConnectionIcon(ConnectionState state) {
  switch (state) {
    case CONN_UNKNOWN:    return '?';
    case CONN_CONNECTING: return '~';
    case CONN_CONNECTED:  return '*';  // Good
    case CONN_WEAK:       return '!';  // Warning
    case CONN_LOST:       return 'X';  // Error
    default:              return '#';
  }
}

/**
 * @brief Update connection state machine
 *
 * Call this regularly in receiver loop to monitor connection health
 *
 * @param health HealthMonitor structure
 * @param remote Remote device state
 */
inline void updateConnectionState(HealthMonitor& health, DeviceState& remote) {
  unsigned long now = millis();
  unsigned long timeSinceLastMsg = now - remote.lastMessageTime;
  ConnectionState oldState = health.state;
  ConnectionState newState = health.state;

  // Determine new state based on time and RSSI
  if (timeSinceLastMsg > watchdogCfg.lostTimeout) {
    // No messages for > 8 seconds
    newState = CONN_LOST;
  }
  else if (timeSinceLastMsg > watchdogCfg.weakTimeout ||
           remote.rssi < watchdogCfg.weakRssiThreshold) {
    // No messages for 3-8 seconds OR weak signal
    newState = CONN_WEAK;
  }
  else if (timeSinceLastMsg < watchdogCfg.weakTimeout &&
           remote.rssi >= watchdogCfg.weakRssiThreshold) {
    // Messages recent and signal good
    newState = CONN_CONNECTED;

    // Track connected time
    if (oldState != CONN_CONNECTED) {
      health.connectedSince = now;
    }
  }

  // State changed?
  if (newState != oldState) {
    health.state = newState;
    health.stateChangeTime = now;

    // Log state change
    Serial.print("\n╔════════ LoRa CONNECTION STATE ════════╗\n");
    Serial.print("║ ");
    Serial.print(getConnectionStateString(oldState));
    Serial.print(" -> ");
    Serial.print(getConnectionStateString(newState));
    Serial.println();
    Serial.print("║ Time since last LoRa msg: ");
    Serial.print(timeSinceLastMsg / 1000.0, 1);
    Serial.println(" s");
    Serial.print("║ RSSI: ");
    Serial.print(remote.rssi);
    Serial.println(" dBm");
    Serial.println("╚═══════════════════════════════════════╝\n");
  }
}

/**
 * @brief Attempt to recover lost connection
 *
 * Try to recover lost connection by re-initializing LoRa module
 *
 * @param health HealthMonitor structure
 * @param myAddress LoRa address for this device
 * @param networkID LoRa network ID
 * @return true if recovery succeeded, false otherwise
 */
inline bool attemptRecovery(HealthMonitor& health, uint8_t myAddress, uint8_t networkID) {
  unsigned long now = millis();

  // Check if we should attempt recovery
  if (health.state != CONN_LOST) {
    return false;  // Only recover from LOST state
  }

  // Check recovery cooldown
  if (now - health.lastRecoveryAttempt < watchdogCfg.recoveryInterval) {
    return false;  // Too soon
  }

  // Check max attempts
  if (health.recoveryAttempts >= watchdogCfg.maxRecoveryAttempts) {
    // Notify only once when max attempts first reached
    if (!health.maxAttemptsReachedNotified) {
      Serial.println("\n⚠️  LoRa connection lost - max recovery attempts reached");
      Serial.println("    Will continue trying every 60s in background...");
      Serial.println("    (This is normal if LoRa transmitter is not active)");
      health.maxAttemptsReachedNotified = true;
    }

    // Continue recovery attempts but with longer interval (60s instead of 15s)
    if (now - health.lastRecoveryAttempt < 60000) {  // 60 seconds
      return false;  // Too soon for background retry
    }

    // Reset counter to allow continued background attempts
    health.recoveryAttempts = 0;
  }

  // Attempt recovery
  health.recoveryAttempts++;
  health.lastRecoveryAttempt = now;

  // Only show detailed recovery messages for first few attempts
  if (health.recoveryAttempts <= watchdogCfg.maxRecoveryAttempts) {
    Serial.println("\n╔════════════════════════════════════╗");
    Serial.print("║ LoRa RECOVERY ATTEMPT #");
    Serial.println(health.recoveryAttempts);
    Serial.println("║ Re-initializing LoRa module...");
    Serial.println("╚════════════════════════════════════╝");
  }

  // Re-initialize LoRa (from lora_handler.h)
  bool success = initLoRa(myAddress, networkID);

  if (success) {
    Serial.println("✓ Recovery successful!");
    health.state = CONN_CONNECTING;
    health.stateChangeTime = now;
    health.recoveryAttempts = 0;  // Reset counter on success
    return true;
  } else {
    Serial.println("❌ Recovery failed");
    return false;
  }
}

/**
 * @brief Get uptime as formatted string
 * @param health HealthMonitor structure
 * @return Formatted uptime string (e.g., "2d 14:32:05")
 */
inline String getUptimeString(HealthMonitor& health) {
  unsigned long totalSeconds = (millis() - health.startTime) / 1000;

  // Calculate days, hours, minutes, seconds
  unsigned long days = totalSeconds / 86400;
  unsigned long hours = (totalSeconds % 86400) / 3600;
  unsigned long minutes = (totalSeconds % 3600) / 60;
  unsigned long seconds = totalSeconds % 60;

  // Format: "Xd HH:MM:SS"
  String result = String(days) + "d ";

  // Add hours (two digits)
  if (hours < 10) result += "0";
  result += String(hours) + ":";

  // Add minutes (two digits)
  if (minutes < 10) result += "0";
  result += String(minutes) + ":";

  // Add seconds (two digits)
  if (seconds < 10) result += "0";
  result += String(seconds);

  return result;
}

/**
 * @brief Print health monitor report
 * @param health HealthMonitor structure
 * @param remote Remote device state
 */
inline void printHealthReport(HealthMonitor& health, DeviceState& remote) {
  Serial.println("\n╔═══════════════════════════════════════╗");
  Serial.println("║        HEALTH MONITOR REPORT         ║");
  Serial.println("╠═══════════════════════════════════════╣");

  // Connection status
  Serial.print("║ Status:     ");
  Serial.print(getConnectionStateString(health.state));
  Serial.print(" ");
  Serial.println(getConnectionIcon(health.state));

  unsigned long uptime = (millis() - health.startTime) / 1000;
  Serial.print("║ Uptime:     ");
  Serial.print(uptime);
  Serial.println(" s");

  if (health.state == CONN_CONNECTED) {
    unsigned long connTime = (millis() - health.connectedSince) / 1000;
    Serial.print("║ Connected:  ");
    Serial.print(connTime);
    Serial.println(" s");
  }

  // RSSI statistics
  Serial.println("╠═══════════════════════════════════════╣");
  Serial.print("║ RSSI Avg:   ");
  Serial.print(getRSSIAverage(health));
  Serial.println(" dBm");
  Serial.print("║ RSSI Min:   ");
  Serial.print(health.rssiMin);
  Serial.println(" dBm");
  Serial.print("║ RSSI Max:   ");
  Serial.print(health.rssiMax);
  Serial.println(" dBm");
  Serial.print("║ Samples:    ");
  Serial.println(health.rssiSamples);

  // Packet statistics (PER-DEVICE TRACKING v2.7.1)
  Serial.println("╠═══════════════════════════════════════╣");
  Serial.print("║ Packets RX: ");
  Serial.println(health.packetsReceived);

  // Show per-device stats if available
  if (health.totalDevicesTracked > 0) {
    Serial.print("║ Devices:    ");
    Serial.println(health.totalDevicesTracked);

    // Average and worst packet loss
    float avgLoss = getAveragePacketLoss(health);
    uint8_t worstId = 0;
    float worstLoss = getWorstPacketLoss(health, &worstId);

    Serial.print("║ Avg Loss:   ");
    Serial.print(avgLoss, 1);
    Serial.println("%");
    Serial.print("║ Worst Loss: ");
    Serial.print(worstLoss, 1);
    Serial.print("% (ID:");
    Serial.print(worstId);
    Serial.println(")");

    // Per-device breakdown
    Serial.println("║ --- Per-Device Stats ---");
    for (int i = 0; i < MAX_DEVICE_STATS; i++) {
      if (health.perDeviceStats[i].active) {
        PerDevicePacketStats& dev = health.perDeviceStats[i];
        float devLoss = getDevicePacketLoss(health, dev.sourceId);
        Serial.print("║ ID ");
        Serial.print(dev.sourceId);
        Serial.print(": ");
        Serial.print(dev.packetsReceived);
        Serial.print(" pkts, ");
        Serial.print(devLoss, 1);
        Serial.println("% loss");
      }
    }
  } else {
    // Legacy single-device display
    Serial.print("║ SEQ range:  ");
    if (health.firstSeq >= 0) {
      Serial.print(health.firstSeq);
      Serial.print(" - ");
      Serial.println(health.lastSeq);
    } else {
      Serial.println("(no packets yet)");
    }
    Serial.print("║ Lost:       ");
    Serial.print(health.packetsLost);
    Serial.print(" (");
    Serial.print(getPacketLoss(health), 1);
    Serial.println("% gap-based)");
    Serial.print("║ Duplicate:  ");
    Serial.println(health.packetsDuplicate);
  }

  Serial.println("╚═══════════════════════════════════════╝\n");
}

// ═══════════════════════════════════════════════════════════════════
// SECTION 2: PERFORMANCE MONITORING
// ═══════════════════════════════════════════════════════════════════

// Performance metrics
struct PerformanceMetrics {
  // Time
  unsigned long uptimeSeconds;
  unsigned long startTime;

  // Memory
  int freeHeapKB;
  int minFreeHeapKB;
  int initialHeapKB;

  // CPU
  int loopFrequency;          // Loops per second
  unsigned long loopCount;    // Total loop iterations
  unsigned long lastLoopTime; // Last frequency calculation
  unsigned long loopCountSnapshot; // For frequency calculation
  float cpuUsage;             // Estimated CPU usage % (placeholder)
  int taskStackFree;          // Free task stack bytes (placeholder)

  // Reporting
  unsigned long lastReport;
  int reportCount;

  // Warnings
  bool lowMemoryWarning;
  bool memoryLeakWarning;
};

PerformanceMetrics perf = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0.0, 0, 0, 0, false, false};

// Memory warning threshold (KB)
#define MEMORY_WARNING_THRESHOLD 50

/**
 * @brief Initialize performance monitoring
 */
void initPerformanceMonitor() {
  #if ENABLE_PERFORMANCE_MONITOR
    perf.startTime = millis();
    perf.initialHeapKB = ESP.getFreeHeap() / 1024;
    perf.freeHeapKB = perf.initialHeapKB;
    perf.minFreeHeapKB = perf.initialHeapKB;
    perf.lastLoopTime = millis();
    perf.lastReport = millis();

    Serial.println("✓ Performance monitor initialized");
    Serial.print("  Initial free heap: ");
    Serial.print(perf.initialHeapKB);
    Serial.println(" KB");
    Serial.print("  Report interval: ");
    Serial.print(PERF_REPORT_INTERVAL / 1000);
    Serial.println(" seconds");
  #endif
}

/**
 * @brief Update performance metrics
 *
 * Call this every loop to track performance metrics
 */
void updatePerformanceMetrics() {
  #if ENABLE_PERFORMANCE_MONITOR
    unsigned long now = millis();

    // Update loop counter
    perf.loopCount++;

    // Update uptime
    perf.uptimeSeconds = (now - perf.startTime) / 1000;

    // Update memory stats
    int currentHeap = ESP.getFreeHeap();
    perf.freeHeapKB = currentHeap / 1024;
    int minHeap = ESP.getMinFreeHeap();
    perf.minFreeHeapKB = minHeap / 1024;

    // Estimate CPU usage based on loop frequency
    // (Higher frequency = lower CPU usage, assuming no blocking delays)
    // This is a rough estimate: 100 Hz = 0%, 1000+ Hz = low usage
    if (perf.loopFrequency > 0) {
      perf.cpuUsage = constrain(100.0 - (perf.loopFrequency / 10.0), 0.0, 100.0);
    } else {
      perf.cpuUsage = 0.0;
    }

    // Task stack free (ESP32 specific, requires FreeRTOS)
    // For now, use a placeholder based on free heap
    perf.taskStackFree = currentHeap;

    // Check memory warnings
    if (perf.freeHeapKB < MEMORY_WARNING_THRESHOLD) {
      if (!perf.lowMemoryWarning) {
        Serial.println("⚠️ LOW MEMORY WARNING!");
        Serial.print("   Free heap: ");
        Serial.print(perf.freeHeapKB);
        Serial.println(" KB");
        perf.lowMemoryWarning = true;
      }
    } else {
      perf.lowMemoryWarning = false;
    }

    // Check for memory leak (min heap decreasing)
    static int previousMinHeap = perf.minFreeHeapKB;
    if (perf.minFreeHeapKB < previousMinHeap - 5) {  // 5 KB drop
      if (!perf.memoryLeakWarning) {
        Serial.println("⚠️ POSSIBLE MEMORY LEAK DETECTED!");
        Serial.print("   Min heap dropped from ");
        Serial.print(previousMinHeap);
        Serial.print(" KB to ");
        Serial.print(perf.minFreeHeapKB);
        Serial.println(" KB");
        perf.memoryLeakWarning = true;
      }
      previousMinHeap = perf.minFreeHeapKB;
    }

    // Calculate loop frequency (every second)
    if (now - perf.lastLoopTime >= 1000) {
      perf.loopFrequency = perf.loopCount - perf.loopCountSnapshot;
      perf.loopCountSnapshot = perf.loopCount;
      perf.lastLoopTime = now;
    }
  #endif
}

/**
 * @brief Print performance report
 */
void printPerformanceReport() {
  #if ENABLE_PERFORMANCE_MONITOR
    unsigned long now = millis();

    // Check if it's time to report
    if (now - perf.lastReport < PERF_REPORT_INTERVAL) {
      return;
    }

    perf.lastReport = now;
    perf.reportCount++;

    // Print report
    Serial.println("\n╔═══════════════ PERFORMANCE REPORT ═══════════════╗");
    Serial.print("║ Report #");
    Serial.println(perf.reportCount);

    // Uptime
    Serial.print("║ Uptime:        ");
    if (perf.uptimeSeconds < 60) {
      Serial.print(perf.uptimeSeconds);
      Serial.println(" seconds");
    } else if (perf.uptimeSeconds < 3600) {
      Serial.print(perf.uptimeSeconds / 60);
      Serial.print(" min ");
      Serial.print(perf.uptimeSeconds % 60);
      Serial.println(" sec");
    } else {
      Serial.print(perf.uptimeSeconds / 3600);
      Serial.print(" hours ");
      Serial.print((perf.uptimeSeconds % 3600) / 60);
      Serial.println(" min");
    }

    // Loop stats
    Serial.print("║ Loop freq:     ");
    Serial.print(perf.loopFrequency);
    Serial.print(" Hz");
    if (perf.loopFrequency < 10) {
      Serial.print(" ⚠️ SLOW!");
    } else if (perf.loopFrequency > 1000) {
      Serial.print(" ✓ Excellent");
    } else if (perf.loopFrequency > 100) {
      Serial.print(" ✓ Good");
    }
    Serial.println();

    Serial.print("║ Total loops:   ");
    Serial.println(perf.loopCount);

    // Memory stats
    Serial.print("║ Free heap:     ");
    Serial.print(perf.freeHeapKB);
    Serial.print(" KB");
    if (perf.freeHeapKB < 50) {
      Serial.print(" ⚠️ LOW!");
    } else {
      Serial.print(" ✓");
    }
    Serial.println();

    Serial.print("║ Min heap:      ");
    Serial.print(perf.minFreeHeapKB);
    Serial.println(" KB");

    Serial.print("║ Initial heap:  ");
    Serial.print(perf.initialHeapKB);
    Serial.println(" KB");

    // Memory usage
    int usedHeap = perf.initialHeapKB - perf.freeHeapKB;
    Serial.print("║ Memory used:   ");
    Serial.print(usedHeap);
    Serial.print(" KB (");
    Serial.print((usedHeap * 100) / perf.initialHeapKB);
    Serial.println("%)");

    // Warnings
    if (perf.lowMemoryWarning) {
      Serial.println("║ ⚠️ WARNING: Low memory!");
    }
    if (perf.memoryLeakWarning) {
      Serial.println("║ ⚠️ WARNING: Possible memory leak!");
    }

    Serial.println("╚══════════════════════════════════════════════════╝\n");
  #endif
}

/**
 * @brief Get performance status as CSV string
 * @return CSV string with loop frequency and free heap
 */
String getPerformanceStatus() {
  #if ENABLE_PERFORMANCE_MONITOR
    return String(perf.loopFrequency) + "," + String(perf.freeHeapKB);
  #else
    return "0,0";
  #endif
}

/**
 * @brief Check if performance is degraded
 * @return true if performance is degraded, false otherwise
 */
bool isPerformanceDegraded() {
  #if ENABLE_PERFORMANCE_MONITOR
    return (perf.loopFrequency < 10 || perf.freeHeapKB < MEMORY_WARNING_THRESHOLD);
  #else
    return false;
  #endif
}

/**
 * @brief Get loop frequency
 * @return Loop frequency in Hz
 */
int getLoopFrequency() {
  #if ENABLE_PERFORMANCE_MONITOR
    return perf.loopFrequency;
  #else
    return 0;
  #endif
}

/**
 * @brief Get free heap in KB
 * @return Free heap in KB
 */
int getFreeHeapKB() {
  #if ENABLE_PERFORMANCE_MONITOR
    return perf.freeHeapKB;
  #else
    return 0;
  #endif
}

// ═══════════════════════════════════════════════════════════════════
// SECTION 3: DETAILED TELEMETRY (Optional Advanced Features)
// ═══════════════════════════════════════════════════════════════════

// Packet statistics (SNR, timing, loss streaks - unique data not in health_monitor)
struct PacketStatistics {
  // SNR statistics
  int snrMin;
  int snrMax;
  long snrSum;
  int snrCount;
  float snrAvg;

  // Timing statistics
  unsigned long lastPacketTime;
  unsigned long minInterval;
  unsigned long maxInterval;
  unsigned long totalInterval;
  int intervalCount;
  float avgInterval;
  float jitter;  // Standard deviation of interval

  // Loss streaks
  int currentLossStreak;
  int maxLossStreak;
  int totalStreaks;

  // Duplicates and out-of-order
  unsigned long duplicates;
  unsigned long outOfOrder;

  // Transmission stats
  unsigned long packetsSent;
  unsigned long transmissionAttempts;
  unsigned long ackReceived;
  unsigned long ackTimeout;

  // Reporting
  unsigned long lastReport;
  int reportCount;
};

// System telemetry
struct SystemTelemetry {
  unsigned long uptime;          // System uptime (seconds)
  int freeHeapKB;                // Free heap memory (KB)
  int minFreeHeapKB;             // Minimum free heap (KB)
  float temperature;             // Internal temperature (°C)
  int loopFrequency;             // Loop frequency (Hz)
  int wifiRSSI;                  // WiFi RSSI (if connected)
  unsigned long lastUpdate;      // Last telemetry update
  int updateCount;               // Number of updates
};

// Global instances
PacketStatistics pktStats = {
  999, -999, 0, 0, 0,            // SNR (init min=999, max=-999)
  0, 999999, 0, 0, 0, 0, 0,      // Timing
  0, 0, 0,                       // Loss streaks
  0, 0,                          // Duplicates, out-of-order
  0, 0, 0, 0,                    // Transmission
  0, 0                           // Reporting
};

SystemTelemetry sysTelem = {0, 0, 0, 0.0, 0, 0, 0, 0};

/**
 * @brief Initialize detailed telemetry
 */
void initDetailedTelemetry() {
  #if ENABLE_PACKET_STATS || ENABLE_EXTENDED_TELEMETRY
    Serial.println("╔════════════════════════════════════════╗");
    Serial.println("║  DETAILED TELEMETRY INIT               ║");
    Serial.println("╚════════════════════════════════════════╝");
  #endif

  #if ENABLE_PACKET_STATS
    pktStats.lastPacketTime = millis();
    pktStats.lastReport = millis();

    Serial.println("  📈 Packet statistics enabled");
    Serial.print("    Report interval: ");
    Serial.print(PACKET_STATS_INTERVAL / 1000);
    Serial.println(" seconds");
    Serial.println("    Tracking:");
    Serial.println("      - SNR min/max/avg");
    Serial.println("      - Packet timing and jitter");
    Serial.println("      - Loss streaks");
    Serial.println("      - Duplicates and out-of-order");
    Serial.println("    RSSI/Packet loss → from health monitor");
  #endif

  #if ENABLE_EXTENDED_TELEMETRY
    sysTelem.lastUpdate = millis();
    sysTelem.freeHeapKB = ESP.getFreeHeap() / 1024;
    sysTelem.minFreeHeapKB = ESP.getMinFreeHeap() / 1024;

    Serial.println("  📊 System telemetry enabled");
    Serial.println("    Monitoring:");
    Serial.println("      - System uptime");
    Serial.println("      - Free heap memory");
    Serial.println("      - Internal temperature");
    Serial.println("      - Loop frequency");
    Serial.println("    ⚠️  Payload size +35 bytes");
  #endif

  #if ENABLE_PACKET_STATS || ENABLE_EXTENDED_TELEMETRY
    Serial.println();
    Serial.println("Detailed telemetry ready.");
    Serial.println();
  #endif
}

/**
 * @brief Record received packet (SNR + timing)
 * @param rssi RSSI value (passed to health monitor)
 * @param snr SNR value
 * @param sequence Sequence number
 */
void recordPacketReceived(int rssi, int snr, int sequence) {
  #if ENABLE_PACKET_STATS
    unsigned long now = millis();

    // Update SNR stats
    if (snr < pktStats.snrMin) pktStats.snrMin = snr;
    if (snr > pktStats.snrMax) pktStats.snrMax = snr;
    pktStats.snrSum += snr;
    pktStats.snrCount++;
    pktStats.snrAvg = (float)pktStats.snrSum / (float)pktStats.snrCount;

    // Update timing stats
    if (pktStats.lastPacketTime > 0) {
      unsigned long interval = now - pktStats.lastPacketTime;

      if (interval < pktStats.minInterval) pktStats.minInterval = interval;
      if (interval > pktStats.maxInterval) pktStats.maxInterval = interval;

      pktStats.totalInterval += interval;
      pktStats.intervalCount++;
      pktStats.avgInterval = (float)pktStats.totalInterval / (float)pktStats.intervalCount;

      // Simple jitter calculation (difference from average)
      float deviation = abs((float)interval - pktStats.avgInterval);
      pktStats.jitter = (pktStats.jitter * 0.9) + (deviation * 0.1);  // Moving average
    }

    pktStats.lastPacketTime = now;

    // Reset loss streak (packet received successfully)
    if (pktStats.currentLossStreak > 0) {
      if (pktStats.currentLossStreak > pktStats.maxLossStreak) {
        pktStats.maxLossStreak = pktStats.currentLossStreak;
      }
      pktStats.totalStreaks++;
      pktStats.currentLossStreak = 0;
    }
  #endif
}

/**
 * @brief Record lost packet (for loss streak tracking)
 */
void recordPacketLost() {
  #if ENABLE_PACKET_STATS
    pktStats.currentLossStreak++;
  #endif
}

/**
 * @brief Record duplicate packet
 * @param sequence Sequence number
 */
void recordDuplicate(int sequence) {
  #if ENABLE_PACKET_STATS
    pktStats.duplicates++;
    Serial.print("📋 Duplicate packet: SEQ:");
    Serial.println(sequence);
  #endif
}

/**
 * @brief Record out-of-order packet
 * @param expected Expected sequence number
 * @param received Received sequence number
 */
void recordOutOfOrder(int expected, int received) {
  #if ENABLE_PACKET_STATS
    pktStats.outOfOrder++;
    Serial.print("🔀 Out-of-order packet: Expected SEQ:");
    Serial.print(expected);
    Serial.print(", Got:");
    Serial.println(received);
  #endif
}

/**
 * @brief Record transmitted packet
 */
void recordPacketSent() {
  #if ENABLE_PACKET_STATS
    pktStats.packetsSent++;
    pktStats.transmissionAttempts++;
  #endif
}

/**
 * @brief Record ACK received
 */
void recordAckReceived() {
  #if ENABLE_PACKET_STATS
    pktStats.ackReceived++;
  #endif
}

/**
 * @brief Record ACK timeout
 */
void recordAckTimeout() {
  #if ENABLE_PACKET_STATS
    pktStats.ackTimeout++;
  #endif
}

/**
 * @brief Calculate ACK success rate
 * @return ACK success rate as percentage
 */
float calculateAckRate() {
  #if ENABLE_PACKET_STATS
    unsigned long total = pktStats.ackReceived + pktStats.ackTimeout;
    if (total == 0) return 0.0;
    return ((float)pktStats.ackReceived / (float)total) * 100.0;
  #else
    return 0.0;
  #endif
}

/**
 * @brief Read internal temperature sensor
 * @return Temperature in Celsius (±5°C accuracy, for trends only)
 */
float readInternalTemperature() {
  #if ENABLE_EXTENDED_TELEMETRY
    // ESP32 has internal temperature sensor
    // Note: Accuracy is ±5°C, use for trends only
    uint8_t raw = temprature_sens_read();

    // Convert to Celsius (approximate formula)
    // Formula varies by ESP32 revision
    float tempC = (raw - 32) / 1.8;

    // Clamp to reasonable range
    if (tempC < -40.0) tempC = -40.0;
    if (tempC > 125.0) tempC = 125.0;

    return tempC;
  #else
    return 0.0;
  #endif
}

/**
 * @brief Update telemetry data
 */
void updateTelemetry() {
  #if ENABLE_EXTENDED_TELEMETRY
    unsigned long now = millis();

    // Uptime in seconds
    sysTelem.uptime = now / 1000;

    // Memory stats
    sysTelem.freeHeapKB = ESP.getFreeHeap() / 1024;
    sysTelem.minFreeHeapKB = ESP.getMinFreeHeap() / 1024;

    // Temperature
    sysTelem.temperature = readInternalTemperature();

    // Loop frequency (from performance monitor)
    #if ENABLE_PERFORMANCE_MONITOR
      sysTelem.loopFrequency = perf.loopFrequency;
    #else
      sysTelem.loopFrequency = 0;
    #endif

    // WiFi RSSI (if WiFi enabled)
    #if ENABLE_WIFI_AP
      if (WiFi.status() == WL_CONNECTED) {
        sysTelem.wifiRSSI = WiFi.RSSI();
      } else {
        sysTelem.wifiRSSI = 0;
      }
    #else
      sysTelem.wifiRSSI = 0;
    #endif

    sysTelem.lastUpdate = now;
    sysTelem.updateCount++;
  #endif
}

/**
 * @brief Check if system health is good
 * @return true if system is healthy, false otherwise
 */
bool isSystemHealthy() {
  #if ENABLE_EXTENDED_TELEMETRY
    updateTelemetry();

    // Check memory
    if (sysTelem.freeHeapKB < 50) {
      return false;  // Low memory
    }

    // Check temperature
    if (sysTelem.temperature > 85.0) {
      return false;  // Too hot
    }

    // Check loop frequency
    if (sysTelem.loopFrequency > 0 && sysTelem.loopFrequency < 10) {
      return false;  // Loop too slow
    }

    return true;  // All checks passed
  #else
    return true;  // Feature disabled, assume healthy
  #endif
}

/**
 * @brief Get health status string
 * @return Health status description
 */
String getHealthStatus() {
  #if ENABLE_EXTENDED_TELEMETRY
    if (isSystemHealthy()) {
      return "HEALTHY";
    } else {
      String issues = "ISSUES:";
      if (sysTelem.freeHeapKB < 50) issues += " LOW_MEM";
      if (sysTelem.temperature > 85.0) issues += " HIGH_TEMP";
      if (sysTelem.loopFrequency < 10 && sysTelem.loopFrequency > 0) issues += " SLOW_LOOP";
      return issues;
    }
  #else
    return "DISABLED";
  #endif
}

/**
 * @brief Print detailed telemetry report
 * @param health HealthMonitor structure (for RSSI/packet loss data)
 */
void printDetailedReport(HealthMonitor& health) {
  #if ENABLE_PACKET_STATS || ENABLE_EXTENDED_TELEMETRY
    unsigned long now = millis();

    // Check if it's time to report
    #if ENABLE_PACKET_STATS
    if (now - pktStats.lastReport < PACKET_STATS_INTERVAL) {
      return;
    }
    pktStats.lastReport = now;
    pktStats.reportCount++;
    #endif

    Serial.println("\n╔═══════════════ DETAILED TELEMETRY REPORT ═══════════════╗");

    #if ENABLE_PACKET_STATS
    Serial.print("║ Report #");
    Serial.println(pktStats.reportCount);
    #endif

    // Packet reception (uses health_monitor data)
    #if ENABLE_PACKET_STATS
    Serial.println("║");
    Serial.println("║ PACKET RECEPTION (from health monitor):");
    Serial.print("║   SEQ range:           ");
    if (health.firstSeq >= 0) {
      Serial.print(health.firstSeq);
      Serial.print(" - ");
      Serial.println(health.lastSeq);
    } else {
      Serial.println("(none)");
    }
    Serial.print("║   Packets received:    ");
    Serial.println(health.packetsReceived);
    Serial.print("║   Packets lost:        ");
    Serial.print(health.packetsLost);
    Serial.print(" (");
    Serial.print(getPacketLoss(health), 2);
    Serial.println("% gap-based)");
    Serial.print("║   Range-based loss:    ");
    Serial.print(getPacketLossRange(health), 2);
    Serial.println("%");
    Serial.print("║   Duplicates:          ");
    Serial.println(pktStats.duplicates);
    Serial.print("║   Out-of-order:        ");
    Serial.println(pktStats.outOfOrder);
    #endif

    // Transmission stats
    #if ENABLE_PACKET_STATS
    if (pktStats.packetsSent > 0) {
      Serial.println("║");
      Serial.println("║ TRANSMISSION:");
      Serial.print("║   Packets sent:        ");
      Serial.println(pktStats.packetsSent);
      Serial.print("║   ACK received:        ");
      Serial.print(pktStats.ackReceived);
      Serial.print(" (");
      Serial.print(calculateAckRate(), 1);
      Serial.println("%)");
      Serial.print("║   ACK timeout:         ");
      Serial.println(pktStats.ackTimeout);
    }
    #endif

    // RSSI stats (uses health_monitor data)
    #if ENABLE_PACKET_STATS
    if (health.rssiSamples > 0) {
      Serial.println("║");
      Serial.println("║ RSSI (dBm) (from health monitor):");
      Serial.print("║   Average:             ");
      Serial.println(getRSSIAverage(health), 1);
      Serial.print("║   Min:                 ");
      Serial.println(health.rssiMin);
      Serial.print("║   Max:                 ");
      Serial.println(health.rssiMax);
      Serial.print("║   Range:               ");
      Serial.println(health.rssiMax - health.rssiMin);
    }
    #endif

    // SNR stats (unique to detailed_telemetry)
    #if ENABLE_PACKET_STATS
    if (pktStats.snrCount > 0) {
      Serial.println("║");
      Serial.println("║ SNR (dB):");
      Serial.print("║   Average:             ");
      Serial.println(pktStats.snrAvg, 1);
      Serial.print("║   Min:                 ");
      Serial.println(pktStats.snrMin);
      Serial.print("║   Max:                 ");
      Serial.println(pktStats.snrMax);
    }
    #endif

    // Timing stats (unique to detailed_telemetry)
    #if ENABLE_PACKET_STATS
    if (pktStats.intervalCount > 0) {
      Serial.println("║");
      Serial.println("║ TIMING:");
      Serial.print("║   Avg interval:        ");
      Serial.print(pktStats.avgInterval, 0);
      Serial.println(" ms");
      Serial.print("║   Min interval:        ");
      Serial.print(pktStats.minInterval);
      Serial.println(" ms");
      Serial.print("║   Max interval:        ");
      Serial.print(pktStats.maxInterval);
      Serial.println(" ms");
      Serial.print("║   Jitter:              ");
      Serial.print(pktStats.jitter, 1);
      Serial.println(" ms");
    }
    #endif

    // Loss streaks (unique to detailed_telemetry)
    #if ENABLE_PACKET_STATS
    Serial.println("║");
    Serial.println("║ LOSS STREAKS:");
    Serial.print("║   Current streak:      ");
    Serial.println(pktStats.currentLossStreak);
    Serial.print("║   Max streak:          ");
    Serial.println(pktStats.maxLossStreak);
    Serial.print("║   Total streaks:       ");
    Serial.println(pktStats.totalStreaks);
    #endif

    // System telemetry
    #if ENABLE_EXTENDED_TELEMETRY
    updateTelemetry();

    Serial.println("║");
    Serial.println("║ SYSTEM TELEMETRY:");

    Serial.print("║   Uptime:              ");
    if (sysTelem.uptime < 60) {
      Serial.print(sysTelem.uptime);
      Serial.println(" s");
    } else if (sysTelem.uptime < 3600) {
      Serial.print(sysTelem.uptime / 60);
      Serial.print(" min ");
      Serial.print(sysTelem.uptime % 60);
      Serial.println(" s");
    } else {
      Serial.print(sysTelem.uptime / 3600);
      Serial.print(" h ");
      Serial.print((sysTelem.uptime % 3600) / 60);
      Serial.println(" min");
    }

    Serial.print("║   Free heap:           ");
    Serial.print(sysTelem.freeHeapKB);
    Serial.print(" KB");
    if (sysTelem.freeHeapKB < 50) {
      Serial.print(" ⚠️  LOW!");
    }
    Serial.println();

    Serial.print("║   Min heap:            ");
    Serial.print(sysTelem.minFreeHeapKB);
    Serial.println(" KB");

    Serial.print("║   Temperature:         ");
    Serial.print(sysTelem.temperature, 1);
    Serial.print(" °C");
    if (sysTelem.temperature > 80.0) {
      Serial.print(" ⚠️  HIGH!");
    }
    Serial.println();

    if (sysTelem.loopFrequency > 0) {
      Serial.print("║   Loop frequency:      ");
      Serial.print(sysTelem.loopFrequency);
      Serial.print(" Hz");
      if (sysTelem.loopFrequency < 10) {
        Serial.print(" ⚠️  SLOW!");
      }
      Serial.println();
    }

    if (sysTelem.wifiRSSI != 0) {
      Serial.print("║   WiFi RSSI:           ");
      Serial.print(sysTelem.wifiRSSI);
      Serial.println(" dBm");
    }

    Serial.print("║   Health status:       ");
    Serial.println(getHealthStatus());
    #endif

    Serial.println("╚═════════════════════════════════════════════════════════╝\n");
  #endif
}

// ═══════════════════════════════════════════════════════════════════
// SECTION 4: UNIFIED INITIALIZATION
// ═══════════════════════════════════════════════════════════════════

/**
 * @brief Initialize complete system monitoring
 *
 * Call this once in setup() to initialize all monitoring subsystems.
 * This is the main entry point for system monitoring.
 *
 * @param health HealthMonitor structure to initialize
 */
void initSystemMonitoring(HealthMonitor& health) {
    Serial.println("\n╔════════════════════════════════════════╗");
    Serial.println("║  SYSTEM MONITORING INIT                ║");
    Serial.println("╚════════════════════════════════════════╝");
    Serial.println();

    // Initialize health monitor (connection watchdog)
    initHealthMonitor(health);
    Serial.println();

    // Initialize performance monitor
    #if ENABLE_PERFORMANCE_MONITOR
    initPerformanceMonitor();
    Serial.println();
    #endif

    // Initialize detailed telemetry (if enabled)
    #if ENABLE_PACKET_STATS || ENABLE_EXTENDED_TELEMETRY
    initDetailedTelemetry();
    #endif

    Serial.println("✓ System Monitoring ready\n");
}

#endif // SYSTEM_MONITORING_H
