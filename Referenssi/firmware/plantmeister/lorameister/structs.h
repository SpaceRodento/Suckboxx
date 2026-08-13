/*=====================================================================
  structs.h - Shared Data Structures

  LoraMeister - Central repository for all data structures.

  STRUCTURES:
  - DeviceState      : LED, touch, communication, LoRa signal, sensor data
  - TimingData       : Timestamp tracking for periodic operations
  - SpinnerData      : LCD animation symbols
  - ConnectionState  : Connection status enum
  - HealthMonitor    : RSSI stats, packet loss, recovery
  - WatchdogConfig   : Connection watchdog thresholds
  - MeshMessage      : Mesh network message with hop tracking
  - RelayStats       : Relay node statistics
  - SeenMessage      : Duplicate message detection
  - DeviceTracker    : Multi-device tracking for receiver
  - BatteryStatus    : Battery voltage and charge level

  USAGE:
    #include "structs.h"

    DeviceState myDevice;
    myDevice.ledState = true;
    myDevice.rssi = -75;
=======================================================================*/

#ifndef STRUCTS_H
#define STRUCTS_H

// =============== DEVICE STATE STRUCTURE ================================
struct DeviceState {
  // LED status
  bool ledState;
  int ledCount;

  // Touch sensor
  bool touchState;
  unsigned long touchValue;

  // Communication
  int messageCount;
  unsigned long lastMessageTime;
  int sequenceNumber;  // For packet tracking

  // Spinner animation
  int spinnerIndex;

  // LoRa signal quality
  int rssi;  // Received Signal Strength Indicator (dBm)
  int snr;   // Signal-to-Noise Ratio (dB)

  // Multi-sender support (mesh network)
  uint8_t sourceId;   // Original sender ID (from mesh message)
  uint8_t hopCount;   // How many relays the message passed through

  // Remote light sensor state (received via LoRa LIT: field)
  // 0=N/A, 1=DARK, 2=AMBIENT, 3=LIGHT, 4=ALARM
  uint8_t lightState;

  // Microphone (MAX4466) data (received via LoRa MIC/MICDB fields)
  uint16_t micPeakToPeak;  // Raw ADC peak-to-peak (0-4095)
  uint8_t micDB;           // Estimated dB level

  // Generic sensor values (populated by sensor_registry parsers on receiver side)
  // Sensors use named indices: e.g., SENSOR_IDX_TEMPERATURE = 0
  float sensorValues[8];
};

// =============== TIMING DATA STRUCTURE ================================
struct TimingData {
  unsigned long lastLED;
  unsigned long lastLCD;
  unsigned long lastSensor;
  unsigned long lastCheck;
  unsigned long lastSend;
  unsigned long lastSpinner;
  unsigned long lastHealthReport;
  unsigned long lastDataOutput;  // For CSV/JSON logging
};

// =============== SPINNER DATA STRUCTURE ================================
struct SpinnerData {
  char symbols[4];
  int index;
  unsigned long lastUpdate;
};

// =============== CONNECTION STATE ENUM ================================
enum ConnectionState {
  CONN_UNKNOWN = 0,
  CONN_CONNECTING = 1,
  CONN_CONNECTED = 2,
  CONN_WEAK = 3,
  CONN_LOST = 4
};

// =============== PER-DEVICE PACKET TRACKING (v2.7.1) ================================
// Tracks packet loss independently for each device
// Solves multi-device confusion where different senders with overlapping SEQ numbers
// would cause false packet loss reports
#define MAX_DEVICE_STATS 10             // Track up to 10 devices simultaneously

struct PerDevicePacketStats {
  uint8_t sourceId;                     // Device ID (0 = unused slot)
  int firstSeq;                         // First SEQ received from this device
  int lastSeq;                          // Last SEQ received
  int expectedSeq;                      // Next expected SEQ
  int packetsReceived;                  // Total packets from this device
  int packetsLost;                      // Packets lost (gaps detected)
  int packetsDuplicate;                 // Duplicate packets
  unsigned long lastSeen;               // Last packet timestamp (millis)
  bool active;                          // Is this slot in use?
};

// =============== HEALTH MONITORING STRUCTURE ================================
struct HealthMonitor {
  // Connection state
  ConnectionState state;
  unsigned long stateChangeTime;
  unsigned long connectedSince;

  // RSSI statistics (sliding window)
  int rssiMin;
  int rssiMax;
  long rssiSum;      // For average calculation
  int rssiSamples;

  // Aggregate packet tracking (used by health reports, display, NVS save)
  int firstSeq;           // First received sequence number (baseline)
  int lastSeq;            // Last received sequence number
  int expectedSeq;        // Next expected sequence number
  int packetsReceived;    // Total packets received (all devices)
  int packetsLost;
  int packetsDuplicate;

  // Multi-device support (v2.6.1)
  uint8_t lastSourceId;   // Last device that sent a packet (for per-device tracking)

  // Per-device packet tracking (v2.7.1) - detailed per-device stats
  PerDevicePacketStats perDeviceStats[MAX_DEVICE_STATS];
  int totalDevicesTracked;              // Number of active device slots

  // Recovery attempts
  int recoveryAttempts;
  unsigned long lastRecoveryAttempt;
  bool maxAttemptsReachedNotified;  // Track if we've already notified about max attempts

  // Uptime
  unsigned long startTime;
};

// =============== WATCHDOG CONFIGURATION ================================
struct WatchdogConfig {
  unsigned long weakTimeout;      // Time to consider connection WEAK (ms)
  unsigned long lostTimeout;      // Time to consider connection LOST (ms)
  int weakRssiThreshold;          // RSSI below this = WEAK (-100 dBm)
  int criticalRssiThreshold;      // RSSI below this = CRITICAL (-110 dBm)
  unsigned long recoveryInterval; // Time between recovery attempts (ms)
  int maxRecoveryAttempts;        // Max recovery attempts before giving up
};

// =============== ERROR COUNTERS STRUCTURE ================================
struct ErrorCounters {
  int rxBufferOverflow;
  int parseErrors;
  int rxTimeouts;
  int loraATfails;
};

// =============== LORA CONFIG STRUCTURE ================================
struct LoRaConfig {
  uint16_t deviceAddress;
  uint8_t networkID;
  bool initialized;
  String firmwareVersion;
};

// =============== RELAY/MESH STATISTICS STRUCTURE ================================
// Tracks relay node performance and message forwarding statistics
struct RelayStats {
  // Message counters
  unsigned long messagesReceived;      // Total messages received
  unsigned long messagesRelayed;       // Successfully forwarded messages
  unsigned long messagesDropped;       // Dropped (max hops reached)
  unsigned long messagesDuplicate;     // Duplicate messages ignored

  // Hop statistics
  unsigned long totalHops;             // Sum of all hop counts (for average)
  uint8_t maxHopsSeenInMessage;        // Highest hop count encountered

  // Signal quality statistics
  int rssiMin;                         // Best RSSI seen
  int rssiMax;                         // Worst RSSI seen
  long rssiSum;                        // For average calculation
  int rssiSamples;

  // Timing
  unsigned long lastRelayTime;         // Last message relay timestamp
  unsigned long lastStatsReport;       // Last statistics print
  unsigned long uptimeStart;           // When relay mode started
};

// =============== MESH MESSAGE STRUCTURE ================================
// Encapsulates a mesh network message with hop tracking
struct MeshMessage {
  uint8_t hopCount;        // Current hop count (incremented by each relay)
  uint8_t maxHops;         // Maximum allowed hops (prevents loops)
  uint8_t sourceId;        // Original sender ID (for routing and deduplication)
  uint8_t destinationId;   // Target device ID (0 = broadcast to all)
  uint16_t sequenceNumber; // Original sequence number (for deduplication)
  bool isEncrypted;        // Flag: payload is encrypted
  bool valid;              // Flag: message parsed successfully (false = parse error)
  String payload;          // Original message payload (unchanged by relay)
};

// =============== SEEN MESSAGE STRUCTURE (for deduplication) ================================
// Tracks recently seen messages to prevent duplicate processing
#define SEEN_MESSAGES_BUFFER 20
#define SEEN_MESSAGE_TIMEOUT_MS 10000  // 10 seconds

struct SeenMessage {
  uint8_t sourceId;        // Original sender ID
  uint16_t sequenceNumber; // Sequence number
  unsigned long timestamp; // When message was seen (millis)
  bool valid;              // Is this entry valid?
};

// =============== SEEN COMMAND STRUCTURE (for command deduplication) ================================
// Tracks recently executed commands to prevent duplicate execution
// Example: If sender retries a command due to missed ACK, don't execute twice
#define SEEN_COMMANDS_BUFFER 50
#define SEEN_COMMAND_DEFAULT_TIMEOUT_MS 5000  // 5 seconds default

struct SeenCommand {
  String commandId;        // Unique command ID (from CMD:ACTION:ID)
  unsigned long timestamp; // When command was executed (millis)
  bool valid;              // Is this entry valid?
};

// =============== DEVICE TRACKER STRUCTURE ================================
// Tracks unique devices (senders) seen by receiver for automatic device counting
// Purpose: Know how many active devices are in the mesh network
// Use case: Discord bot can report "3 devices online", dashboard shows device count
//
// Example usage:
//   DeviceTracker tracker;
//   initDeviceTracker(tracker);
//   registerDevice(tracker, remote.sourceId);
//   int count = getActiveDeviceCount(tracker);
//
#define MAX_TRACKED_DEVICES 20          // Maximum unique devices to track
#define DEVICE_TIMEOUT_MS 120000        // Consider device offline after 2 minutes

struct TrackedDevice {
  uint8_t deviceId;              // Device address/ID
  unsigned long lastSeen;        // Timestamp of last message (millis)
  unsigned long messageCount;    // Total messages from this device
  int lastRssi;                  // Last RSSI value
  int lastSnr;                   // Last SNR value
  uint8_t lightState;            // Last light state (0=N/A, 1=DARK, 2=AMB, 3=LIGHT, 4=ALARM)
  bool active;                   // Is entry in use?
};

struct DeviceTracker {
  TrackedDevice devices[MAX_TRACKED_DEVICES];
  int totalDevicesEverSeen;      // Total unique devices seen (lifetime)
  int currentActiveCount;        // Currently active devices (cached)
  unsigned long lastCountUpdate; // When currentActiveCount was last calculated
};

// =============== BATTERY STATUS STRUCTURE ================================
// Tracks battery voltage and charge level for power management
// Used by: power_management.h (readBatteryVoltage, updateBatteryStatus)
typedef struct {
    float voltage;          // Current battery voltage (V)
    int percent;            // Battery percentage (0-100%)
    bool isLow;             // Below BATTERY_LOW_THRESHOLD
    bool isCritical;        // Below BATTERY_CRITICAL_THRESHOLD
    unsigned long lastRead; // Last ADC reading timestamp (millis)
} BatteryStatus;

#endif // STRUCTS_H