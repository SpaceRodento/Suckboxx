#ifndef EBBFLOW_FSM_H
#define EBBFLOW_FSM_H

#include <stdint.h>

namespace ebbflow {

enum State : uint8_t {
  IDLE = 0,
  FLOOD = 1,
  SOAK = 2,
  DRAIN = 3,
  FAULT = 4
};

enum Fault : uint8_t {
  FAULT_NONE = 0,
  FAULT_DRY_RUN = 1,
  FAULT_OVERFLOW = 2,
  FAULT_FLOOD_TIMEOUT = 3,
  FAULT_DRAIN_TIMEOUT = 4,
  FAULT_SENSOR = 5
};

struct Config {
  uint32_t flood_interval_ms;
  uint32_t flood_duration_ms;
  uint32_t soak_duration_ms;
  uint32_t drain_timeout_ms;
  uint8_t  max_overflow_retries;  // consecutive overflowing floods before hard latch (0 = use default)
};

// Consecutive overflow events tolerated before a hard (latched) OVERFLOW fault.
// An isolated overflow drains and retries; a stuck stand-pipe trips repeatedly
// and must latch so the pump never cycles into a permanently overflowing bed.
static const uint8_t DEFAULT_MAX_OVERFLOW_RETRIES = 3;

struct Inputs {
  uint32_t now_ms;
  bool min_level_ok;
  bool overflow_active;
  bool pump_running;
  bool ack_fault;
  bool force_flood;   // one-shot: start a flood immediately from IDLE, bypassing the interval
};

struct Runtime {
  State state;
  // fault: most recent fault/condition code. When fault_latched is true it is a
  // HARD fault (state == FAULT, requires manual ack). When false it is an
  // ADVISORY condition (DRY_RUN waiting for water, slow DRAIN) — the cycle keeps
  // running and self-recovers, the code is informational for UX only.
  Fault fault;
  uint32_t state_since_ms;
  uint32_t last_cycle_start_ms;
  bool fault_latched;
  uint8_t overflow_count;  // consecutive overflowing floods (reset on clean flood)
};

struct Result {
  bool start_pump;
  bool stop_pump;
  bool state_changed;
  bool fault_changed;
};

void init_runtime(Runtime* rt, uint32_t now_ms);
Result step(const Config* cfg, const Inputs* in, Runtime* rt);

}  // namespace ebbflow

#endif
