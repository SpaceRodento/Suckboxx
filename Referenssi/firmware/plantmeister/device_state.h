/*=====================================================================
  device_state.h - PlantMeister Top-Level Device State Machine

  Single source of truth for device-level state.
  All UI (LED, e-ink) and input routing reads g_device.state.

  See PLANTMEISTER.md section 3.4.6 for full state diagram.
=====================================================================*/

#ifndef DEVICE_STATE_H
#define DEVICE_STATE_H

#include "config.h"
#include "structs.h"
#include <stdio.h>
#include <string.h>

// ═══════════════════════════════════════════════════════════════════
// DEVICE STATE ENUM
// ═══════════════════════════════════════════════════════════════════

// D14 (2b, docs/kehitys/m0.1-toteutussuunnitelma.md § 2): DEVICE_AWAITING_USER
// was removed 9.8.2026 — device_setState(DEVICE_AWAITING_USER) appeared in no
// production path, only in the 7 branches that guarded against it. Renumbered
// (not reserved): the state number is not persisted to LittleFS (config_store.h
// never reads/writes g_device.state) and not sent raw in a LoRa payload
// (lora_payload.h sends ebbFlowState, not DeviceState). It IS still sent as a
// raw int over /api/state (wifi_portal_api_state.h, wifi_portal_routes_get.h)
// for the e-ink dev-view diagnostics screen — that screen's own numeric label
// mapping (firmware/plantmeister/eink_dev_view.h, firmware/e-ink-seinataulu/
// dev_view.h) was already out of sync with this enum before this change and is
// out of scope for D14 (e-ink-seinataulu is excluded by the task spec).
enum DeviceState : uint8_t {
  DEVICE_INIT          = 0,   // Boot, structs initialized
  DEVICE_SELF_TEST     = 1,   // 3 s startup self-test
  DEVICE_IDLE          = 2,   // Device ok, no growing cycle active
  DEVICE_GROWING       = 3,   // Growing cycle in progress
  DEVICE_FAULT         = 4,   // Fault, requires human attention
  DEVICE_SHUTDOWN      = 5,   // Long button press → clean shutdown
  DEVICE_MAINTENANCE   = 6,   // Operator servicing the device; actuators locked
  DEVICE_STATE_COUNT
};

// Fault subtypes (bitmask, can have multiple active)
#define DEVICE_FAULT_NONE          0x00
#define DEVICE_FAULT_SELF_TEST     0x01   // Self-test failed
#define DEVICE_FAULT_SENSOR        0x02   // Sensor read failure
#define DEVICE_FAULT_MOTOR         0x04   // Motor stalled / position unknown
#define DEVICE_FAULT_WATER         0x08   // Water low / overflow
#define DEVICE_FAULT_LORA          0x10   // LoRa radio unresponsive
#define DEVICE_FAULT_BROWNOUT      0x20   // Detected unclean shutdown
#define DEVICE_FAULT_FAN           0x40   // Fan commanded on but tacho shows no spin (ADVISORY)
#define DEVICE_FAULT_UNKNOWN       0x80

// Two-tier fault model (architecture.md § 8 graceful degradation):
//   BLOCKING  → device locks into DEVICE_FAULT, requires human (CLEAR_FAULT).
//   ADVISORY  → device keeps operating, condition surfaced for information only.
// Only conditions that make operation unsafe or impossible may block. A missing
// optional peripheral is advisory, NOT a fault. Do not add bits to the blocking
// mask without a documented safety rationale.
#define DEVICE_FAULT_BLOCKING_MASK (DEVICE_FAULT_WATER | DEVICE_FAULT_MOTOR)

// ── Maintenance mode (docs/kehitys/wizardit-ohjatut-puuhat-kartoitus.md P1) ──
// Operator is physically servicing the device (topping up water, changing
// nutrient, cleaning). A pump or motor starting with a hand in the reservoir is
// the highest-severity entry in that FMEA (S=5), so this is a *safety* state,
// not a wizard: it is binary (on/off), it has no step sequence, and it is never
// left automatically. A timeout that auto-resumed would start the pump under the
// operator's hand — exactly the hazard it exists to prevent (FMEA R3: on
// timeout, go to the safe state; maintenance already *is* the safe state).
// Long-running maintenance is surfaced via the health summary instead.
#define DEVICE_MAINTENANCE_LONG_MS  (2UL * 60UL * 60UL * 1000UL)  // 2 h ⇒ health warns

// May maintenance be entered from `current`? Only from states where the device
// is operating normally. A blocking FAULT already halts actuators and demands
// CLEAR_FAULT first, so it is not a maintenance entry point; INIT/SELF_TEST are
// mid-boot and SHUTDOWN is terminal. Pure → unit-testable (test_device_state).
static inline bool device_maintenanceEntryAllowed(DeviceState current) {
  return current == DEVICE_IDLE || current == DEVICE_GROWING;
}

// Where maintenance returns to on exit. Mirrors device_shouldResumeGrowing():
// growActive is the persistent truth for "a grow program is running", so a grow
// interrupted for servicing resumes instead of being orphaned in IDLE.
//
// Deliberately NOT persisted across reboot: a power cut mid-service resumes the
// grow rather than waking into maintenance. Accepted because the alternative is
// worse — a persisted maintenance flag that outlives the service visit would
// silently stop watering for good, and the reboot path already cannot start a
// pump instantly (flooding is interval-driven, and the operator is present by
// definition). Revisit only if servicing routinely involves cutting power.
static inline DeviceState device_maintenanceResumeState(bool growActive) {
  return growActive ? DEVICE_GROWING : DEVICE_IDLE;
}

// True when maintenance has been active long enough to be worth flagging. The
// plant is not being watered while the operator services it, so a maintenance
// mode forgotten overnight is a real (if slow) hazard — surfaced, never fixed
// by an automatic resume.
static inline bool device_maintenanceIsLong(DeviceState current,
                                            unsigned long timeInStateMs) {
  return current == DEVICE_MAINTENANCE && timeInStateMs >= DEVICE_MAINTENANCE_LONG_MS;
}

// True when a freshly-booted device should resume DEVICE_GROWING. growActive is
// the persistent single source of truth for "a grow program is running"; a clean
// reboot must restore the matching runtime state instead of orphaning the flag in
// IDLE — an orphaned flag silently disables periodic flooding (a DEVICE_GROWING
// concern, see loop_dispatch.h) even though the program looks active in config.
// Excluded after a crash (safeMode): stay IDLE for investigation, do not auto-
// resume actuators. Only resumes from a cleanly-reached IDLE, so a blocking FAULT
// is never overridden. Pure boolean logic → unit-testable (test_device_state).
static inline bool device_shouldResumeGrowing(bool safeMode, bool growActive,
                                              DeviceState current) {
  return !safeMode && growActive && current == DEVICE_IDLE;
}

// ═══════════════════════════════════════════════════════════════════
// DEVICE STATE STRUCT
// ═══════════════════════════════════════════════════════════════════

#define DEVICE_FAULT_MSG_LEN     48
#define DEVICE_HISTORY_SIZE      32
#define DEVICE_HEALTH_LEN        112    // Human-readable health summary buffer
#define DEVICE_INIT_STALL_MS     10000UL // INIT longer than this ⇒ suspect boot hang

struct DeviceStateTransition {
  DeviceState   from;
  DeviceState   to;
  unsigned long timestamp;
  uint8_t       faults;
};

struct DeviceStateInfo {
  DeviceState   state;
  DeviceState   prevState;
  unsigned long enteredAt;          // millis() when current state was entered
  uint8_t       faults;             // Bitmask of active BLOCKING faults
  char          faultMessage[DEVICE_FAULT_MSG_LEN]; // Last fault description (Finnish, user-facing)
  uint8_t       advisories;         // Bitmask of active ADVISORY conditions (non-blocking)
  char          advisoryMessage[DEVICE_FAULT_MSG_LEN]; // Last advisory description (Finnish, user-facing)
  bool          safeMode;           // Boot-loop safe mode: minimal image (WiFi/OTA/status only)

  // Ringbuffer of recent transitions (for /api/device/history)
  DeviceStateTransition history[DEVICE_HISTORY_SIZE];
  uint8_t               historyIdx;  // Next write position
  uint8_t               historyCount;
};

extern DeviceStateInfo g_device;

// ═══════════════════════════════════════════════════════════════════
// API
// ═══════════════════════════════════════════════════════════════════

// Initialize state machine. Sets state = DEVICE_INIT, clears faults.
void device_init();

// Request state transition. Logs transition to history.
// Returns true if transition is allowed, false if rejected (e.g. fault active).
bool device_setState(DeviceState newState);

// Set fault. Routes each bit by DEVICE_FAULT_BLOCKING_MASK:
//   blocking bits → g_device.faults, transitions to DEVICE_FAULT.
//   advisory bits → g_device.advisories, state unchanged (informational).
// Message must be UTF-8 Finnish, max DEVICE_FAULT_MSG_LEN-1 chars.
void device_setFault(uint8_t faultBit, const char* message);

// Set advisory condition explicitly. Never changes device state. Use this for
// non-blocking degradation (missing optional sensor, LoRa down, etc.).
void device_setAdvisory(uint8_t advisoryBit, const char* message);

// Clear specific advisory bit(s).
void device_clearAdvisory(uint8_t advisoryBit);

// Returns true if any advisory bit is set.
bool device_hasAdvisory();

// Clear specific fault bit. If bitmask becomes zero, transitions back to
// previous non-fault state (or DEVICE_IDLE if previous was fault too).
void device_clearFault(uint8_t faultBit);

// Clear all faults. Same transition rule as device_clearFault.
void device_clearAllFaults();

// Returns true if any fault bit is set.
bool device_hasFault();

// Safe mode: boot-loop recovery posture. When active, setup() booted a minimal
// image (WiFi + OTA + status only). NOT a fault — it never blocks state
// transitions; it is surfaced in the health summary and /api/status so a fix
// can be flashed.
void device_setSafeMode(bool on);
bool device_inSafeMode();

// Human-readable state name for logs and API responses.
const char* device_stateName(DeviceState state);

// Build a one-line, human-readable health summary (Finnish) into `out`.
// Single source of truth for /api/status, /api/state and logs: a reader (human
// or Claude) can tell at a glance whether the device is operational, degraded
// (advisory), faulted, or stuck booting — without decoding bitmasks. This is the
// field that prevents the "device looks broken" misdiagnosis.
void device_healthSummary(char* out, size_t len);

// Time spent in current state (ms).
unsigned long device_timeInState();

// ═══════════════════════════════════════════════════════════════════
// IMPLEMENTATION
// ═══════════════════════════════════════════════════════════════════

DeviceStateInfo g_device;

static const char* DEVICE_STATE_NAMES[DEVICE_STATE_COUNT] = {
  "INIT", "SELF_TEST", "IDLE", "GROWING", "FAULT", "SHUTDOWN", "MAINTENANCE"
};

static void device_logTransition(DeviceState from, DeviceState to) {
  DeviceStateTransition& t = g_device.history[g_device.historyIdx];
  t.from = from;
  t.to = to;
  t.timestamp = millis();
  t.faults = g_device.faults;
  g_device.historyIdx = (g_device.historyIdx + 1) % DEVICE_HISTORY_SIZE;
  if (g_device.historyCount < DEVICE_HISTORY_SIZE) g_device.historyCount++;
}

void device_init() {
  g_device.state = DEVICE_INIT;
  g_device.prevState = DEVICE_INIT;
  g_device.enteredAt = millis();
  g_device.faults = DEVICE_FAULT_NONE;
  g_device.faultMessage[0] = '\0';
  g_device.advisories = DEVICE_FAULT_NONE;
  g_device.advisoryMessage[0] = '\0';
  g_device.safeMode = false;
  g_device.historyIdx = 0;
  g_device.historyCount = 0;
}

bool device_setState(DeviceState newState) {
  if (newState >= DEVICE_STATE_COUNT) return false;
  if (newState == g_device.state) return true;

  // FAULT can only be cleared via device_clearFault*, not direct setState
  if (g_device.state == DEVICE_FAULT && newState != DEVICE_SHUTDOWN && newState != DEVICE_FAULT) {
    if (g_device.faults != DEVICE_FAULT_NONE) return false;
  }

  device_logTransition(g_device.state, newState);
  g_device.prevState = g_device.state;
  g_device.state = newState;
  g_device.enteredAt = millis();
  return true;
}

void device_setAdvisory(uint8_t advisoryBit, const char* message) {
  if (advisoryBit == DEVICE_FAULT_NONE) return;
  g_device.advisories |= advisoryBit;
  if (message) {
    strncpy(g_device.advisoryMessage, message, DEVICE_FAULT_MSG_LEN - 1);
    g_device.advisoryMessage[DEVICE_FAULT_MSG_LEN - 1] = '\0';
  }
  // Never changes device state — advisory is informational (architecture.md § 8).
}

void device_clearAdvisory(uint8_t advisoryBit) {
  g_device.advisories &= ~advisoryBit;
  if (g_device.advisories == DEVICE_FAULT_NONE) g_device.advisoryMessage[0] = '\0';
}

bool device_hasAdvisory() {
  return g_device.advisories != DEVICE_FAULT_NONE;
}

void device_setFault(uint8_t faultBit, const char* message) {
  // Route each bit by severity: blocking bits lock the device, advisory bits
  // only record information without changing state.
  uint8_t blocking = faultBit & DEVICE_FAULT_BLOCKING_MASK;
  uint8_t advisory = faultBit & ~DEVICE_FAULT_BLOCKING_MASK;

  if (advisory) device_setAdvisory(advisory, message);

  if (!blocking) return;

  g_device.faults |= blocking;
  if (message) {
    strncpy(g_device.faultMessage, message, DEVICE_FAULT_MSG_LEN - 1);
    g_device.faultMessage[DEVICE_FAULT_MSG_LEN - 1] = '\0';
  }
  if (g_device.state != DEVICE_FAULT) {
    device_logTransition(g_device.state, DEVICE_FAULT);
    g_device.prevState = g_device.state;
    g_device.state = DEVICE_FAULT;
    g_device.enteredAt = millis();
  }
}

void device_clearFault(uint8_t faultBit) {
  g_device.faults &= ~faultBit;
  if (g_device.faults == DEVICE_FAULT_NONE && g_device.state == DEVICE_FAULT) {
    // Oletus IDLE: prevState voi olla SELF_TEST / INIT joista ei voida palata
    // operatiiviseen toimintaan (ne ovat transitorisia). IDLE on ainoa stable
    // state josta operaattori voi jatkaa komentoja.
    //
    // Poikkeus MAINTENANCE (D13, 3.8.2026): huoltotila on kayttajan tekema
    // eksplisiittinen turvalukko (pumppu + moottori pois), josta poistutaan
    // vain eksplisiittisella eleella. Jos vika syntyi huollon aikana, IDLE:en
    // palaaminen vapauttaisi lukot hiljaa kesken huollon — kadet voivat olla
    // altaassa. Kuittaus saa poistaa vian, ei huoltotilaa.
    DeviceState target = (g_device.prevState == DEVICE_MAINTENANCE)
                           ? DEVICE_MAINTENANCE
                           : DEVICE_IDLE;
    g_device.faultMessage[0] = '\0';
    device_logTransition(g_device.state, target);
    g_device.prevState = g_device.state;
    g_device.state = target;
    g_device.enteredAt = millis();
  }
}

void device_clearAllFaults() {
  device_clearAdvisory(0xFF);
  device_clearFault(0xFF);
}

bool device_hasFault() {
  return g_device.faults != DEVICE_FAULT_NONE;
}

void device_setSafeMode(bool on) {
  g_device.safeMode = on;
}

bool device_inSafeMode() {
  return g_device.safeMode;
}

const char* device_stateName(DeviceState state) {
  if (state >= DEVICE_STATE_COUNT) return "UNKNOWN";
  return DEVICE_STATE_NAMES[state];
}

unsigned long device_timeInState() {
  return millis() - g_device.enteredAt;
}

void device_healthSummary(char* out, size_t len) {
  if (!out || len == 0) return;
  if (g_device.safeMode) {
    // Highest priority: a boot-loop forced a minimal recovery image. Say so
    // plainly so a reader knows the device is up but degraded by design.
    snprintf(out, len,
             "SAFE MODE: boot-loop havaittu - vain WiFi+OTA aktiivisia. Flashaa korjattu firmware (/update).");
  } else if (g_device.state == DEVICE_FAULT) {
    snprintf(out, len, "FAULT: %s - vaatii kuittauksen (CLEAR_FAULT)",
             g_device.faultMessage[0] ? g_device.faultMessage : "vika havaittu");
  } else if (g_device.state == DEVICE_MAINTENANCE) {
    // Ranks above advisory: actuators are intentionally locked, so a reader
    // must not mistake "pump does nothing" for a fault. Long service is called
    // out because the plant is not being watered meanwhile.
    unsigned long mins = device_timeInState() / 60000UL;
    if (device_maintenanceIsLong(g_device.state, device_timeInState())) {
      snprintf(out, len,
               "HUOLTOTILA %lu min (pitkä): aktuaattorit lukittu, kastelu seisoo - lopeta huolto napista",
               mins);
    } else {
      snprintf(out, len,
               "HUOLTOTILA %lu min: aktuaattorit lukittu turvallisesti - lopeta huolto napista",
               mins);
    }
  } else if (g_device.state == DEVICE_INIT &&
             device_timeInState() > DEVICE_INIT_STALL_MS) {
    // Portal task answers HTTP even while setup() is stuck, so a long INIT is a
    // strong signal of a blocking boot-time peripheral call (architecture § 8
    // Aukko A), NOT a normal state. Surface it explicitly.
    snprintf(out, len,
             "VAROITUS: boot ei valmistunut (INIT %lus) - epäilty blokkaava oheislaite-I/O",
             (unsigned long)(device_timeInState() / 1000UL));
  } else if (device_hasAdvisory()) {
    snprintf(out, len, "Toiminnassa (degraded): %s - advisory, ei estä toimintaa",
             g_device.advisoryMessage[0] ? g_device.advisoryMessage : "oheislaite puuttuu");
  } else {
    snprintf(out, len, "Toiminnassa");
  }
}

#endif // DEVICE_STATE_H
