#include "ebbflow_fsm.h"

namespace ebbflow {

// Hard, latched fault: stops the pump and halts the cycle until manual ack.
// Reserved for conditions that are genuinely unsafe to auto-retry:
// pump stuck on (FLOOD_TIMEOUT), contradictory sensors, repeated overflow.
static void enter_fault(Runtime* rt, Fault code, Result* out, uint32_t now_ms) {
  rt->fault = code;
  rt->fault_latched = true;
  rt->state = FAULT;
  rt->state_since_ms = now_ms;
  out->stop_pump = true;
  out->state_changed = true;
  out->fault_changed = true;
}

// Advisory condition: NOT latched, state keeps running and self-recovers.
// Used for benign-but-noteworthy conditions (reservoir low -> wait for water,
// slow drain -> keep waiting). Never halts the cycle.
static void set_advisory(Runtime* rt, Fault code, Result* out) {
  if (rt->fault != code || rt->fault_latched) {
    out->fault_changed = true;
  }
  rt->fault = code;
  rt->fault_latched = false;
}

static void clear_advisory(Runtime* rt, Result* out) {
  if (rt->fault != FAULT_NONE && !rt->fault_latched) {
    rt->fault = FAULT_NONE;
    out->fault_changed = true;
  }
}

void init_runtime(Runtime* rt, uint32_t now_ms) {
  rt->state = IDLE;
  rt->fault = FAULT_NONE;
  rt->state_since_ms = now_ms;
  rt->last_cycle_start_ms = now_ms;
  rt->fault_latched = false;
  rt->overflow_count = 0;
}

// Handle an overflow detected while water is being added (FLOOD/SOAK).
// Always stops the pump immediately. An isolated event drains and retries;
// once retries are exhausted it latches (stand-pipe presumed failed).
static void handle_overflow(const Config* cfg, Runtime* rt, Result* out, uint32_t now_ms) {
  out->stop_pump = true;
  uint8_t limit = cfg->max_overflow_retries ? cfg->max_overflow_retries
                                            : DEFAULT_MAX_OVERFLOW_RETRIES;
  if (rt->overflow_count + 1 >= limit) {
    rt->overflow_count++;
    enter_fault(rt, FAULT_OVERFLOW, out, now_ms);
    return;
  }
  rt->overflow_count++;
  rt->state = DRAIN;          // relieve: let the bed drain back, then retry
  rt->state_since_ms = now_ms;
  rt->fault = FAULT_OVERFLOW;  // advisory — informs UX, not latched
  rt->fault_latched = false;
  out->state_changed = true;
  out->fault_changed = true;
}

Result step(const Config* cfg, const Inputs* in, Runtime* rt) {
  Result out = {false, false, false, false};

  // Manual ack clears a HARD fault and resumes from a safe state.
  if (in->ack_fault && rt->fault_latched) {
    rt->fault = FAULT_NONE;
    rt->fault_latched = false;
    rt->overflow_count = 0;
    rt->state = in->min_level_ok ? IDLE : DRAIN;
    rt->state_since_ms = in->now_ms;
    out.state_changed = true;
    out.fault_changed = true;
    return out;
  }

  // Hard fault halts until acked.
  if (rt->state == FAULT) {
    return out;
  }

  // Overflow while adding water -> react immediately (drain or latch).
  if (in->overflow_active && (rt->state == FLOOD || rt->state == SOAK)) {
    handle_overflow(cfg, rt, &out, in->now_ms);
    return out;
  }

  switch (rt->state) {
    case IDLE: {
      if (!in->min_level_ok) {
        // Reservoir low: do NOT pump (dry-run guard) and do NOT latch.
        // Advisory only — when water returns the normal interval resumes.
        if (in->pump_running) out.stop_pump = true;
        set_advisory(rt, FAULT_DRY_RUN, &out);
        break;
      }
      clear_advisory(rt, &out);
      // Force-flood (button/portal) shortcuts the interval. The dry-run guard
      // above already returned, so force_flood can never pump an empty reservoir.
      if (in->force_flood ||
          in->now_ms - rt->last_cycle_start_ms >= cfg->flood_interval_ms) {
        rt->state = FLOOD;
        rt->state_since_ms = in->now_ms;
        rt->last_cycle_start_ms = in->now_ms;
        out.start_pump = true;
        out.state_changed = true;
      }
      break;
    }

    case FLOOD: {
      // Pump stuck on past the dose window -> genuine hardware fault, latch.
      if (in->now_ms - rt->state_since_ms > cfg->flood_duration_ms + 1000U) {
        enter_fault(rt, FAULT_FLOOD_TIMEOUT, &out, in->now_ms);
        break;
      }
      if (!in->pump_running) {
        if (!in->overflow_active && rt->fault != FAULT_OVERFLOW) {
          rt->overflow_count = 0;   // clean flood completed -> reset retry counter
        }
        clear_advisory(rt, &out);
        rt->state = SOAK;
        rt->state_since_ms = in->now_ms;
        out.state_changed = true;
      }
      break;
    }

    case SOAK: {
      if (in->now_ms - rt->state_since_ms >= cfg->soak_duration_ms) {
        rt->state = DRAIN;
        rt->state_since_ms = in->now_ms;
        out.state_changed = true;
      }
      break;
    }

    case DRAIN: {
      if (in->min_level_ok) {
        clear_advisory(rt, &out);
        rt->state = IDLE;
        rt->state_since_ms = in->now_ms;
        out.state_changed = true;
        break;
      }
      // Slow/clogged drain is benign (no pump runs from DRAIN, no overflow):
      // do NOT latch. Flag advisory and keep waiting so the cycle never halts.
      if (in->now_ms - rt->state_since_ms >= cfg->drain_timeout_ms) {
        set_advisory(rt, FAULT_DRAIN_TIMEOUT, &out);
      }
      break;
    }

    case FAULT:
    default:
      break;
  }

  return out;
}

}  // namespace ebbflow
