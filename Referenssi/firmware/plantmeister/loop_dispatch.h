/*=====================================================================
  loop_dispatch.h - Loop state dispatch helper (header-only)
=====================================================================*/

#ifndef LOOP_DISPATCH_H
#define LOOP_DISPATCH_H

#include "config.h"
#include "structs.h"
#include "device_state.h"

struct LoopDispatchContext {
  void* userData;
};

typedef void (*LoopDispatchTask)(const LoopDispatchContext* ctx);

struct LoopDispatchOps {
  LoopDispatchTask heartbeat;
  LoopDispatchTask sensorsIdle;
  LoopDispatchTask sensorsGrowing;
  LoopDispatchTask loraPoll;
  LoopDispatchTask wifiPortal;
  LoopDispatchTask growScheduler;
  LoopDispatchTask ebbFlow;
  LoopDispatchTask actuators;
  LoopDispatchTask uiIdle;
  LoopDispatchTask uiAwaiting;
  LoopDispatchTask uiFault;
  LoopDispatchTask uiMaintenance;
  LoopDispatchTask history;
  LoopDispatchTask serialInput;
  LoopDispatchTask shutdown;
};

inline void loop_dispatch_call(LoopDispatchTask task, const LoopDispatchContext* ctx) {
  if (task) {
    task(ctx);
  }
}

inline void loop_tick_for_state(DeviceState state,
                                const LoopDispatchContext* ctx,
                                const LoopDispatchOps* ops) {
  if (!ops) return;
  const LoopDispatchContext emptyCtx = { nullptr };
  const LoopDispatchContext* safeCtx = ctx ? ctx : &emptyCtx;

  switch (state) {
    case DEVICE_INIT:
    case DEVICE_SELF_TEST:
      break;

    case DEVICE_IDLE:
      loop_dispatch_call(ops->heartbeat, safeCtx);
      loop_dispatch_call(ops->sensorsIdle, safeCtx);
      // Actuators run also in IDLE so manual /api/command requests (motor_up,
      // pump test, light toggle from phone/portal) actually drive the hardware
      // without first entering DEVICE_GROWING.
      loop_dispatch_call(ops->actuators, safeCtx);
      // Ebb&flow force-flood (button / portal "Aja tulva nyt") must also work in
      // IDLE: the grow scheduler does not run here, so this ticks the FSM to honor
      // a pending manual flood and let an in-progress cycle finish. Periodic
      // interval flooding stays a DEVICE_GROWING concern (growScheduler).
      loop_dispatch_call(ops->ebbFlow, safeCtx);
      loop_dispatch_call(ops->loraPoll, safeCtx);
      loop_dispatch_call(ops->wifiPortal, safeCtx);
      loop_dispatch_call(ops->history, safeCtx);
      loop_dispatch_call(ops->uiIdle, safeCtx);
      loop_dispatch_call(ops->serialInput, safeCtx);
      break;

    case DEVICE_GROWING:
      loop_dispatch_call(ops->heartbeat, safeCtx);
      loop_dispatch_call(ops->actuators, safeCtx);
      loop_dispatch_call(ops->sensorsGrowing, safeCtx);
      loop_dispatch_call(ops->growScheduler, safeCtx);
      loop_dispatch_call(ops->loraPoll, safeCtx);
      loop_dispatch_call(ops->wifiPortal, safeCtx);
      loop_dispatch_call(ops->history, safeCtx);
      loop_dispatch_call(ops->uiIdle, safeCtx);
      loop_dispatch_call(ops->serialInput, safeCtx);
      break;

    case DEVICE_FAULT:
      loop_dispatch_call(ops->sensorsIdle, safeCtx);
      loop_dispatch_call(ops->uiFault, safeCtx);
      // FAULT-RECOVERY: salli sarjakomennot (esim. CLEAR_FAULT) myos faultissa
      // jotta operaattori voi palauttaa laitteen ilman fyysista RST-painallusta.
      loop_dispatch_call(ops->serialInput, safeCtx);
      break;

    case DEVICE_MAINTENANCE:
      // Operator is servicing the device. Everything that observes or reports
      // keeps running — sensors, portal, logging — so the operator can watch
      // readings while working and exit remotely if the button is out of reach.
      //
      // Deliberately absent: growScheduler and ebbFlow. Those are what *decide
      // to start* a flood, and nothing may decide that while hands are in the
      // reservoir.
      //
      // `actuators` DOES run, which reads like a contradiction but is not: that
      // task never starts anything. It finishes a move already in flight,
      // releases the motor driver once the carriage arrives, and — the reason
      // it must not be skipped — calls pump_update(), which is where the
      // FLOAT_OVF overflow check lives ("always call it so the safety check
      // runs each loop"). Dropping it here would disable overflow detection
      // during the one activity most likely to spill water, and leave the motor
      // driver energised for the whole service visit. New starts are refused by
      // the maintenance lock inside pump_hal/motor_hal, which is also what
      // covers the direct paths (/api/test/pump, calibration wizards) that
      // never pass through dispatch at all.
      loop_dispatch_call(ops->heartbeat, safeCtx);
      loop_dispatch_call(ops->sensorsIdle, safeCtx);
      loop_dispatch_call(ops->actuators, safeCtx);
      loop_dispatch_call(ops->loraPoll, safeCtx);
      loop_dispatch_call(ops->wifiPortal, safeCtx);
      loop_dispatch_call(ops->history, safeCtx);
      loop_dispatch_call(ops->uiMaintenance, safeCtx);
      loop_dispatch_call(ops->serialInput, safeCtx);
      break;

    case DEVICE_SHUTDOWN:
      loop_dispatch_call(ops->shutdown, safeCtx);
      break;

    default:
      break;
  }
}

#endif // LOOP_DISPATCH_H
