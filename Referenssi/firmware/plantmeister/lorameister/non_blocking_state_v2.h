/*=====================================================================
  non_blocking_state_v2.h - Non-Blocking State Machine V2 (Option B)

  Zignalmeister 2000 - ESP32 LoRa Communication System
  Roboter Gruppe 9

  VERSION: v2 (Option B - Structural Improvements)
  DATE: 2026-01-15
  CHANGES FROM V1:
  - Explicit state machine with enum states (not just boolean)
  - Memory safety: char[] instead of String (no heap fragmentation)
  - Relay queue: supports 4 pending messages (vs single message)
  - Overflow-safe comparisons throughout
  - Better debugging (state names visible)

  See STATE_MACHINE.md (project root) for complete documentation
=======================================================================*/

#ifndef NON_BLOCKING_STATE_V2_H
#define NON_BLOCKING_STATE_V2_H

#include <Arduino.h>

//=============================================================================
// CONFIGURATION
//=============================================================================

#define RELAY_QUEUE_SIZE 4          // Maximum pending relay messages
#define RELAY_MESSAGE_MAX_SIZE 350  // Maximum message length (256 payload + mesh headers)

//=============================================================================
// RELAY STATE MACHINE V2 - Explicit States + Memory Safe
//=============================================================================

/**
 * Explicit relay state machine states
 * Clear lifecycle: IDLE → SCHEDULED → SENDING → (COMPLETED or FAILED)
 */
enum RelayStateMachineState {
  RELAY_IDLE,           // No relay pending (not in queue)
  RELAY_SCHEDULED,      // Waiting for jitter timer to expire
  RELAY_SENDING,        // Currently transmitting
  RELAY_COMPLETED,      // Transmission successful (will be dequeued)
  RELAY_FAILED          // Max retries exceeded (will be dequeued)
};

/**
 * Get state name as string (for debugging)
 */
inline const char* getRelayStateName(RelayStateMachineState state) {
  switch (state) {
    case RELAY_IDLE:      return "IDLE";
    case RELAY_SCHEDULED: return "SCHEDULED";
    case RELAY_SENDING:   return "SENDING";
    case RELAY_COMPLETED: return "COMPLETED";
    case RELAY_FAILED:    return "FAILED";
    default:              return "UNKNOWN";
  }
}

/**
 * Individual relay state (memory-safe with fixed buffers)
 */
struct RelayStateV2 {
  RelayStateMachineState state;         // Explicit state
  char message[RELAY_MESSAGE_MAX_SIZE]; // Fixed buffer (no heap allocation)
  int targetAddr;                       // Target LoRa address
  unsigned long scheduleTime;           // When to send (millis())
  uint8_t retryCount;                   // Current retry attempt (0-255)
  uint8_t maxRetries;                   // Maximum retry attempts
};

/**
 * Relay queue for multiple pending messages
 * Circular buffer implementation
 */
struct RelayQueue {
  RelayStateV2 queue[RELAY_QUEUE_SIZE]; // Circular buffer
  uint8_t head;                         // Index of next message to send
  uint8_t tail;                         // Index of next free slot
  uint8_t count;                        // Number of messages in queue

  // Statistics
  uint16_t totalEnqueued;               // Total messages added
  uint16_t totalDropped;                // Messages dropped (queue full)
  uint8_t peakUsage;                    // Maximum queue usage seen
};

//=============================================================================
// QUEUE OPERATIONS
//=============================================================================

/**
 * Initialize relay queue
 */
inline void initRelayQueue(RelayQueue& q) {
  q.head = 0;
  q.tail = 0;
  q.count = 0;
  q.totalEnqueued = 0;
  q.totalDropped = 0;
  q.peakUsage = 0;

  // Initialize all queue slots to IDLE
  for (int i = 0; i < RELAY_QUEUE_SIZE; i++) {
    q.queue[i].state = RELAY_IDLE;
    q.queue[i].message[0] = '\0';
    q.queue[i].targetAddr = 0;
    q.queue[i].scheduleTime = 0;
    q.queue[i].retryCount = 0;
    q.queue[i].maxRetries = 2;
  }
}

/**
 * Check if queue is full
 */
inline bool isQueueFull(const RelayQueue& q) {
  return q.count >= RELAY_QUEUE_SIZE;
}

/**
 * Check if queue is empty
 */
inline bool isQueueEmpty(const RelayQueue& q) {
  return q.count == 0;
}

/**
 * Add message to relay queue
 *
 * @param q Relay queue
 * @param msg Message to relay (will be copied to fixed buffer)
 * @param addr Target address
 * @param delayMs Delay before sending
 * @param maxRetries Maximum retry attempts (default: 2)
 * @return true if enqueued, false if queue full
 */
inline bool enqueueRelay(RelayQueue& q, const char* msg, int addr, unsigned long delayMs, uint8_t maxRetries = 2) {
  // Check capacity
  if (isQueueFull(q)) {
    q.totalDropped++;

    #ifdef DEBUG_STATE_MACHINE
      Serial.printf("⚠️ Relay queue FULL (%d/%d), message DROPPED\n",
                    q.count, RELAY_QUEUE_SIZE);
    #endif

    return false;
  }

  // Get slot at tail
  RelayStateV2& slot = q.queue[q.tail];

  // Set state
  slot.state = RELAY_SCHEDULED;

  // Copy message with bounds checking
  strncpy(slot.message, msg, RELAY_MESSAGE_MAX_SIZE - 1);
  slot.message[RELAY_MESSAGE_MAX_SIZE - 1] = '\0';  // Ensure null termination

  // Set parameters
  slot.targetAddr = addr;
  slot.scheduleTime = millis() + delayMs;
  slot.retryCount = 0;
  slot.maxRetries = maxRetries;

  // Update queue pointers
  q.tail = (q.tail + 1) % RELAY_QUEUE_SIZE;
  q.count++;
  q.totalEnqueued++;

  // Track peak usage
  if (q.count > q.peakUsage) {
    q.peakUsage = q.count;
  }

  #ifdef DEBUG_STATE_MACHINE
    Serial.printf("✓ Relay enqueued [%d/%d]: delay=%lums, retries=%d\n",
                  q.count, RELAY_QUEUE_SIZE, delayMs, maxRetries);
  #endif

  return true;
}

/**
 * Overload: enqueue with String (converts to char*)
 */
inline bool enqueueRelay(RelayQueue& q, const String& msg, int addr, unsigned long delayMs, uint8_t maxRetries = 2) {
  return enqueueRelay(q, msg.c_str(), addr, delayMs, maxRetries);
}

/**
 * Get next ready relay from queue (without removing)
 *
 * @param q Relay queue
 * @return Pointer to ready relay, or NULL if none ready
 */
inline RelayStateV2* getNextReadyRelay(RelayQueue& q) {
  if (isQueueEmpty(q)) {
    return NULL;
  }

  // Check head relay
  RelayStateV2& relay = q.queue[q.head];

  if (relay.state == RELAY_SCHEDULED) {
    // Check if timer expired (overflow-safe comparison)
    unsigned long now = millis();
    if ((now - relay.scheduleTime) < 0x80000000UL) {
      return &relay;
    }
  }

  return NULL;
}

/**
 * Remove relay from front of queue
 * Call after processing COMPLETED or FAILED relay
 */
inline void dequeueRelay(RelayQueue& q) {
  if (isQueueEmpty(q)) {
    return;
  }

  // Clear slot
  RelayStateV2& slot = q.queue[q.head];
  slot.state = RELAY_IDLE;
  slot.message[0] = '\0';
  slot.targetAddr = 0;
  slot.scheduleTime = 0;
  slot.retryCount = 0;

  // Update queue pointers
  q.head = (q.head + 1) % RELAY_QUEUE_SIZE;
  q.count--;

  #ifdef DEBUG_STATE_MACHINE
    Serial.printf("🔄 Relay dequeued [%d/%d remaining]\n",
                  q.count, RELAY_QUEUE_SIZE);
  #endif
}

/**
 * Schedule retry for failed relay
 *
 * @param relay Relay state to retry
 * @param retryDelayMs Delay before retry (default: 50ms)
 * @return true if retry scheduled, false if max retries reached
 */
inline bool scheduleRelayRetry(RelayStateV2& relay, unsigned long retryDelayMs = 50) {
  relay.retryCount++;

  if (relay.retryCount <= relay.maxRetries) {
    // Schedule retry
    relay.state = RELAY_SCHEDULED;
    relay.scheduleTime = millis() + retryDelayMs;

    #ifdef DEBUG_STATE_MACHINE
      Serial.printf("⚠️  Retry scheduled %d/%d (in %lums)\n",
                    relay.retryCount, relay.maxRetries, retryDelayMs);
    #endif

    return true;
  } else {
    // Max retries reached - mark as failed
    relay.state = RELAY_FAILED;

    #ifdef DEBUG_STATE_MACHINE
      Serial.printf("❌ Max retries reached (%d/%d) - FAILED\n",
                    relay.retryCount, relay.maxRetries);
    #endif

    return false;
  }
}

/**
 * Print queue status (debugging)
 */
#ifdef DEBUG_STATE_MACHINE
inline void printQueueStatus(const RelayQueue& q) {
  Serial.println("═══ Relay Queue Status ═══");
  Serial.printf("Count: %d/%d\n", q.count, RELAY_QUEUE_SIZE);
  Serial.printf("Head: %d, Tail: %d\n", q.head, q.tail);
  Serial.printf("Peak usage: %d\n", q.peakUsage);
  Serial.printf("Total enqueued: %d\n", q.totalEnqueued);
  Serial.printf("Total dropped: %d\n", q.totalDropped);

  if (!isQueueEmpty(q)) {
    Serial.println("\nPending relays:");
    for (uint8_t i = 0; i < q.count; i++) {
      uint8_t idx = (q.head + i) % RELAY_QUEUE_SIZE;
      const RelayStateV2& relay = q.queue[idx];

      Serial.printf("  [%d] State: %s", i, getRelayStateName(relay.state));

      if (relay.state == RELAY_SCHEDULED) {
        long remaining = relay.scheduleTime - millis();
        Serial.printf(" (send in %ldms)", remaining);
      }

      Serial.println();
    }
  }

  Serial.println("═══════════════════════════");
}
#endif

//=============================================================================
// ACK LISTEN WINDOW V2 - Memory Safe
//=============================================================================

enum AckState {
  ACK_IDLE,           // Not waiting for ACK
  ACK_LISTENING,      // Actively listening for ACK
  ACK_RECEIVED,       // ACK received successfully
  ACK_TIMEOUT         // Timeout occurred
};

/**
 * Get ACK state name as string (for debugging)
 */
inline const char* getAckStateName(AckState state) {
  switch (state) {
    case ACK_IDLE:      return "IDLE";
    case ACK_LISTENING: return "LISTENING";
    case ACK_RECEIVED:  return "RECEIVED";
    case ACK_TIMEOUT:   return "TIMEOUT";
    default:            return "UNKNOWN";
  }
}

/**
 * ACK state machine (memory-safe version)
 */
struct AckStateMachineV2 {
  AckState state;
  unsigned long startTime;      // When we started listening
  unsigned long timeout;        // Timeout duration
  char expectedFrom[16];        // Address we expect ACK from (fixed buffer)
};

/**
 * Start listening for ACK
 */
inline void startAckListen(AckStateMachineV2& machine, const char* fromAddr, unsigned long timeoutMs) {
  machine.state = ACK_LISTENING;
  machine.startTime = millis();
  machine.timeout = timeoutMs;

  // Copy address with bounds checking
  strncpy(machine.expectedFrom, fromAddr, sizeof(machine.expectedFrom) - 1);
  machine.expectedFrom[sizeof(machine.expectedFrom) - 1] = '\0';

  #ifdef DEBUG_STATE_MACHINE
    Serial.printf("⏱ ACK listen started: from %s, timeout %lums\n",
                  machine.expectedFrom, timeoutMs);
  #endif
}

/**
 * Overload: String version
 */
inline void startAckListen(AckStateMachineV2& machine, const String& fromAddr, unsigned long timeoutMs) {
  startAckListen(machine, fromAddr.c_str(), timeoutMs);
}

/**
 * Update ACK state machine (overflow-safe)
 */
inline AckState updateAckMachine(AckStateMachineV2& machine, const String& receivedMessage) {
  if (machine.state != ACK_LISTENING) {
    return machine.state;
  }

  // Check for timeout (overflow-safe)
  unsigned long elapsed = millis() - machine.startTime;
  if (elapsed >= machine.timeout && elapsed < 0x80000000UL) {
    machine.state = ACK_TIMEOUT;

    #ifdef DEBUG_STATE_MACHINE
      Serial.println("⏱ ACK timeout");
    #endif

    return machine.state;
  }

  // Check if received message is ACK
  if (!receivedMessage.isEmpty() && receivedMessage.indexOf("ACK") >= 0) {
    machine.state = ACK_RECEIVED;

    #ifdef DEBUG_STATE_MACHINE
      Serial.println("✓ ACK received");
    #endif
  }

  return machine.state;
}

/**
 * Check if ACK was received (resets to IDLE)
 */
inline bool checkAckReceived(AckStateMachineV2& machine) {
  if (machine.state == ACK_RECEIVED) {
    machine.state = ACK_IDLE;
    return true;
  }
  if (machine.state == ACK_TIMEOUT) {
    machine.state = ACK_IDLE;
    return false;
  }
  return false;  // Still listening
}

/**
 * Check if currently waiting for ACK
 */
inline bool isWaitingForAck(const AckStateMachineV2& machine) {
  return machine.state == ACK_LISTENING;
}

/**
 * Cancel ACK listening
 */
inline void cancelAckListen(AckStateMachineV2& machine) {
  if (machine.state == ACK_LISTENING) {
    machine.state = ACK_IDLE;

    #ifdef DEBUG_STATE_MACHINE
      Serial.println("⚠️ ACK listen cancelled");
    #endif
  }
}

//=============================================================================
// SENSOR POLLING OPTIMIZATION (unchanged from V1)
//=============================================================================

inline bool shouldPollTouchSensor(bool bRECEIVER, bool bRELAY, bool touchCommandEnabled) {
  return bRECEIVER || bRELAY || touchCommandEnabled;
}

inline bool shouldPollBatteryMonitor(bool bSENDER, bool bRELAY) {
  return bSENDER || bRELAY;
}

//=============================================================================
// PERFORMANCE MONITORING
//=============================================================================

#ifdef DEBUG_STATE_MACHINE
/**
 * Print complete state machine status
 */
inline void printStateMachineStatus(const RelayQueue& relayQ, const AckStateMachineV2& ack) {
  Serial.println("═══ State Machine Status (V2) ═══");

  // Relay queue
  Serial.printf("Relay Queue: %d/%d", relayQ.count, RELAY_QUEUE_SIZE);
  if (!isQueueEmpty(relayQ)) {
    const RelayStateV2& next = relayQ.queue[relayQ.head];
    Serial.printf(" (next: %s", getRelayStateName(next.state));
    if (next.state == RELAY_SCHEDULED) {
      long remaining = next.scheduleTime - millis();
      Serial.printf(", send in %ldms", remaining);
    }
    Serial.print(")");
  }
  Serial.println();

  // ACK machine
  Serial.printf("ACK: %s", getAckStateName(ack.state));
  if (ack.state == ACK_LISTENING) {
    unsigned long elapsed = millis() - ack.startTime;
    long remaining = ack.timeout - elapsed;
    Serial.printf(" (timeout in %ldms)", remaining);
  }
  Serial.println();

  Serial.println("═════════════════════════════════");
}
#endif

#endif // NON_BLOCKING_STATE_V2_H
