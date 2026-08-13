// Tests the pure context-mapping layer (layer 2) of the button
// architecture: button_resolveIntent(gesture, state, config, plant) -> Intent.
// See docs/kehitys/nappi-vuorovaikutus-kartoitus.md §8.1 (K-A, locked
// 14.7.2026) for the per-state table this suite locks into code.
//
// Behavior under test (K-A + maintenance K-B 15.7.2026 + back-nav K-C
// 22.7.2026 + D14 2a removed the legacy config opt-in, 9.8.2026):
//   LONG_PRESS  -> BACK-navigation, ALWAYS:
//                    DEVICE_GROWING     -> INTENT_PREV_STEP (undo a step)
//                    DEVICE_MAINTENANCE -> INTENT_MAINTENANCE_EXIT (safety)
//                    otherwise          -> INTENT_NONE (inert)
//                  Maintenance is entered from the phone only, never the
//                  button; EXIT still always wins in DEVICE_MAINTENANCE.
//   SHORT_PRESS -> DEVICE_IDLE        -> INTENT_START_GROWING
//                  DEVICE_FAULT       -> INTENT_ACK_FAULT
//                  DEVICE_MAINTENANCE -> INTENT_NONE (inert during service)
//                  otherwise          -> button_resolveShortAction(config)
//                  (unchanged v1 fallback: portal buttonAction dropdown,
//                  regression-guarded below so GROWING keeps working exactly
//                  as before this change)
#include <unity.h>

// Narrow feature surface — portal off so input_router.h needs no
// portal_reactivate() stub. We only need the Intent enum + button map.
#define ENABLE_WIFI_PORTAL false
#include "test_feature_flags.h"

#include "button_intent_map.h"

// input_router.h declares this; provide a stub so any emitted inline
// referencing it still links (button_resolveIntent itself never calls it).
bool config_save(const DeviceConfig*) { return true; }

void setUp() {}
void tearDown() {}

// ── Helpers ───────────────────────────────────────────────────────────

static DeviceConfig cfgWithAction(uint8_t action) {
  DeviceConfig cfg = {};
  cfg.buttonAction = action;
  return cfg;
}

// ── Long press: BACK-navigation by default (K-C 22.7.2026) ──────────────
// cfgWithAction() leaves buttonLongAction = 0 = BUTTON_LONG_ACTION_BACK.

void test_long_press_growing_is_prev_step() {
  // Default: a hold while growing steps the guided sequence backwards.
  DeviceConfig cfg = cfgWithAction(BUTTON_ACTION_NONE);
  TEST_ASSERT_EQUAL(INTENT_PREV_STEP,
    button_resolveIntent(BUTTON_EVENT_LONG_PRESS, DEVICE_GROWING, &cfg, nullptr));
}

void test_long_press_idle_is_inert() {
  // Maintenance moved to the phone: outside GROWING the hold does nothing.
  DeviceConfig cfg = cfgWithAction(BUTTON_ACTION_NONE);
  TEST_ASSERT_EQUAL(INTENT_NONE,
    button_resolveIntent(BUTTON_EVENT_LONG_PRESS, DEVICE_IDLE, &cfg, nullptr));
}

void test_long_press_in_maintenance_exits() {
  // Safety fallback preserved in BOTH modes: a hold in MAINTENANCE releases it
  // even though entry is now a phone action. The lock must never be
  // un-exitable from the device.
  DeviceConfig cfg = cfgWithAction(BUTTON_ACTION_NONE);
  TEST_ASSERT_EQUAL(INTENT_MAINTENANCE_EXIT,
    button_resolveIntent(BUTTON_EVENT_LONG_PRESS, DEVICE_MAINTENANCE, &cfg, nullptr));
}

void test_long_press_ignores_short_action_config() {
  // The portal's short-press action must never leak into the long gesture:
  // holding with action=SHUTDOWN configured stays inert outside GROWING.
  DeviceConfig cfg = cfgWithAction(BUTTON_ACTION_SHUTDOWN);
  TEST_ASSERT_EQUAL(INTENT_NONE,
    button_resolveIntent(BUTTON_EVENT_LONG_PRESS, DEVICE_IDLE, &cfg, nullptr));
}

// ── Maintenance: short press must stay inert ────────────────────────────

void test_short_press_in_maintenance_yields_no_intent() {
  // The safety property: a stray press with a hand in the reservoir must not
  // drop the lock, and must not trigger the configurable action either.
  DeviceConfig cfg = cfgWithAction(BUTTON_ACTION_EBB_FLOOD);
  TEST_ASSERT_EQUAL(INTENT_NONE,
    button_resolveIntent(BUTTON_EVENT_SHORT_PRESS, DEVICE_MAINTENANCE, &cfg, nullptr));
}

// ── Short press: locked per-state rows (§8.1) ───────────────────────────

void test_short_press_idle_starts_growing() {
  DeviceConfig cfg = cfgWithAction(BUTTON_ACTION_NONE);
  TEST_ASSERT_EQUAL(INTENT_START_GROWING,
    button_resolveIntent(BUTTON_EVENT_SHORT_PRESS, DEVICE_IDLE, &cfg, nullptr));
}

void test_short_press_idle_ignores_button_action_config() {
  // IDLE has a locked meaning regardless of the portal's buttonAction
  // dropdown — the per-state row overrides the configurable fallback.
  DeviceConfig cfg = cfgWithAction(BUTTON_ACTION_SHUTDOWN);
  TEST_ASSERT_EQUAL(INTENT_START_GROWING,
    button_resolveIntent(BUTTON_EVENT_SHORT_PRESS, DEVICE_IDLE, &cfg, nullptr));
}

void test_short_press_idle_null_config_still_starts_growing() {
  // IDLE's row never reads config, so a null pointer (defensive callers)
  // must not crash and must still resolve to the locked Intent.
  TEST_ASSERT_EQUAL(INTENT_START_GROWING,
    button_resolveIntent(BUTTON_EVENT_SHORT_PRESS, DEVICE_IDLE, nullptr, nullptr));
}

void test_short_press_fault_acks_fault() {
  DeviceConfig cfg = cfgWithAction(BUTTON_ACTION_NONE);
  TEST_ASSERT_EQUAL(INTENT_ACK_FAULT,
    button_resolveIntent(BUTTON_EVENT_SHORT_PRESS, DEVICE_FAULT, &cfg, nullptr));
}

void test_short_press_fault_ignores_button_action_config() {
  DeviceConfig cfg = cfgWithAction(BUTTON_ACTION_REBOOT);
  TEST_ASSERT_EQUAL(INTENT_ACK_FAULT,
    button_resolveIntent(BUTTON_EVENT_SHORT_PRESS, DEVICE_FAULT, &cfg, nullptr));
}

// ── Short press: other states keep the configurable action (regression) ──
// GROWING acknowledgement is an open design question (§8.3 point 5) — not a
// locked Intent yet, so these states must keep falling back to the
// pre-existing portal buttonAction mapping unchanged.

void test_short_press_growing_none_yields_no_intent() {
  DeviceConfig cfg = cfgWithAction(BUTTON_ACTION_NONE);
  TEST_ASSERT_EQUAL(INTENT_NONE,
    button_resolveIntent(BUTTON_EVENT_SHORT_PRESS, DEVICE_GROWING, &cfg, nullptr));
}

void test_short_press_growing_ebb_flood_via_config() {
  DeviceConfig cfg = cfgWithAction(BUTTON_ACTION_EBB_FLOOD);
  TEST_ASSERT_EQUAL(INTENT_EBB_FLOOD_NOW,
    button_resolveIntent(BUTTON_EVENT_SHORT_PRESS, DEVICE_GROWING, &cfg, nullptr));
}

void test_short_press_growing_shutdown_still_reachable_via_config() {
  // BUTTON_ACTION_SHUTDOWN is an opt-in portal choice, untouched by K-A —
  // only the LONG_PRESS gesture lost its hardwired shutdown meaning.
  DeviceConfig cfg = cfgWithAction(BUTTON_ACTION_SHUTDOWN);
  TEST_ASSERT_EQUAL(INTENT_SHUTDOWN,
    button_resolveIntent(BUTTON_EVENT_SHORT_PRESS, DEVICE_GROWING, &cfg, nullptr));
}

void test_short_press_self_test_reboot_via_config() {
  // DEVICE_SELF_TEST stands in for "any other state without a locked row" —
  // it must keep falling back to the configurable action same as before D14
  // removed DEVICE_AWAITING_USER, which used to serve as this example.
  DeviceConfig cfg = cfgWithAction(BUTTON_ACTION_REBOOT);
  TEST_ASSERT_EQUAL(INTENT_REBOOT,
    button_resolveIntent(BUTTON_EVENT_SHORT_PRESS, DEVICE_SELF_TEST, &cfg, nullptr));
}

void test_short_press_null_config_is_safe_in_growing() {
  TEST_ASSERT_EQUAL(INTENT_NONE,
    button_resolveIntent(BUTTON_EVENT_SHORT_PRESS, DEVICE_GROWING, nullptr, nullptr));
}

// ── Grow-step context (16.7.2026): active step changes the button map ───
// While a step sequence is showing an active step in DEVICE_GROWING:
//   SHORT -> INTENT_ACK_STEP, ALWAYS (no buttonAction fallback — the
//            configured action can be "flood now", which must not fire
//            while rockwool soaks in a bucket)
//   LONG  -> INTENT_PREV_STEP, always (back-navigation, K-C); the router
//            bounds it (undo a mis-ack). maintenance-EXIT always wins in
//            MAINTENANCE.

// A plant whose active phase has a step list: basil, seedling phase,
// seed start (the real list in grow_steps.h).
static PlantConfig plantWithSteps() {
  PlantConfig p = {};
  strncpy(p.id, "basil", sizeof(p.id) - 1);
  p.phaseCount = 1;
  p.phases[0] = {};
  p.phases[0].type = GROW_PHASE_SEEDLING;
  return p;
}

// Used in SHORT_PRESS regression tests below to prove button_resolveIntent's
// short-press path never reads buttonLongAction — any value other than
// BUTTON_LONG_ACTION_BACK (0) does; D14 (2a) removed the only other named
// constant, so this stands in for "some other stored value" without one.
static const uint8_t kArbitraryNonBackLongAction = 1;

static DeviceConfig cfgWithActiveStep(uint8_t shortAction, uint8_t longAction) {
  DeviceConfig cfg = cfgWithAction(shortAction);
  cfg.buttonLongAction = longAction;
  cfg.growActive       = true;
  cfg.growPhase        = 0;
  cfg.growStartMethod  = 1;   // siemen
  cfg.growStepIndex    = 0;
  return cfg;
}

void test_short_press_during_step_is_always_ack_step() {
  // The dangerous combination: buttonAction=EBB_FLOOD + active soak step.
  // Without the step override a stray press would start the pump.
  PlantConfig plant = plantWithSteps();
  DeviceConfig cfg = cfgWithActiveStep(BUTTON_ACTION_EBB_FLOOD,
                                       kArbitraryNonBackLongAction);
  TEST_ASSERT_EQUAL(INTENT_ACK_STEP,
    button_resolveIntent(BUTTON_EVENT_SHORT_PRESS, DEVICE_GROWING, &cfg, &plant));
}

// ── Sequence completed: the press confirms the phase transition (3.8.2026) ──
// Found on hardware: seedling phase day 11/14, all four basil steps acked,
// user pressed the button expecting the next phase and the log said
// "Button: gesture 1 (no action)". The phase guidance was asking "check the
// roots - ready to move?" while no gesture could answer it. The plant, not
// the calendar, decides when a phase is over, so the press now advances.

void test_short_press_after_sequence_advances_phase() {
  // Sequence finished (index == count) -> the press means "guidance done,
  // move on". This deliberately overrides the configurable action: reaching
  // the end of the walkthrough is a stronger signal than the dropdown, and
  // "flood now" is exactly what must not happen when the user meant "next".
  PlantConfig plant = plantWithSteps();
  DeviceConfig cfg = cfgWithActiveStep(BUTTON_ACTION_EBB_FLOOD,
                                       kArbitraryNonBackLongAction);
  cfg.growStepIndex = 4;   // basil-siemenlistan pituus (Fable §3.1: 4 askelta)
  TEST_ASSERT_EQUAL(INTENT_NEXT_PHASE,
    button_resolveIntent(BUTTON_EVENT_SHORT_PRESS, DEVICE_GROWING, &cfg, &plant));
}

void test_short_press_after_sequence_advances_even_past_the_count() {
  // Defensive: a stale/oversized index must not fall back to the old silence.
  PlantConfig plant = plantWithSteps();
  DeviceConfig cfg = cfgWithActiveStep(BUTTON_ACTION_NONE,
                                       BUTTON_LONG_ACTION_BACK);
  cfg.growStepIndex = 99;
  TEST_ASSERT_EQUAL(INTENT_NEXT_PHASE,
    button_resolveIntent(BUTTON_EVENT_SHORT_PRESS, DEVICE_GROWING, &cfg, &plant));
}

void test_short_press_without_any_step_list_keeps_button_action() {
  // Regression guard for the 7 plants that have no step list at all: "no
  // active step" must keep meaning the configurable action there. Only a
  // sequence that EXISTED and was completed unlocks the phase advance.
  PlantConfig plant = {};
  strncpy(plant.id, "no-such-plant", sizeof(plant.id) - 1);
  plant.phaseCount = 1;
  plant.phases[0] = {};
  plant.phases[0].type = GROW_PHASE_CUSTOM;   // no list registered for this
  DeviceConfig cfg = cfgWithActiveStep(BUTTON_ACTION_EBB_FLOOD,
                                       BUTTON_LONG_ACTION_BACK);
  cfg.growStepIndex = 4;
  TEST_ASSERT_EQUAL(INTENT_EBB_FLOOD_NOW,
    button_resolveIntent(BUTTON_EVENT_SHORT_PRESS, DEVICE_GROWING, &cfg, &plant));
}

void test_completed_sequence_does_not_advance_outside_growing() {
  // The step context must not leak into other states: a completed sequence
  // in MAINTENANCE stays inert, and in IDLE the press still starts a grow.
  PlantConfig plant = plantWithSteps();
  DeviceConfig cfg = cfgWithActiveStep(BUTTON_ACTION_NONE,
                                       BUTTON_LONG_ACTION_BACK);
  cfg.growStepIndex = 4;
  TEST_ASSERT_EQUAL(INTENT_NONE,
    button_resolveIntent(BUTTON_EVENT_SHORT_PRESS, DEVICE_MAINTENANCE, &cfg, &plant));
  TEST_ASSERT_EQUAL(INTENT_START_GROWING,
    button_resolveIntent(BUTTON_EVENT_SHORT_PRESS, DEVICE_IDLE, &cfg, &plant));
}

void test_long_press_still_undoes_after_sequence_completed() {
  // The advance must stay reversible from the device: the hold is the undo.
  PlantConfig plant = plantWithSteps();
  DeviceConfig cfg = cfgWithActiveStep(BUTTON_ACTION_NONE,
                                       BUTTON_LONG_ACTION_BACK);
  cfg.growStepIndex = 4;
  TEST_ASSERT_EQUAL(INTENT_PREV_STEP,
    button_resolveIntent(BUTTON_EVENT_LONG_PRESS, DEVICE_GROWING, &cfg, &plant));
}

void test_long_press_during_step_is_prev_step() {
  // Default (BACK): a hold during an active step steps the sequence backwards.
  // The map just emits PREV_STEP; the router bounds it (undo a mis-ack).
  PlantConfig plant = plantWithSteps();
  DeviceConfig cfg = cfgWithActiveStep(BUTTON_ACTION_NONE,
                                       BUTTON_LONG_ACTION_BACK);
  TEST_ASSERT_EQUAL(INTENT_PREV_STEP,
    button_resolveIntent(BUTTON_EVENT_LONG_PRESS, DEVICE_GROWING, &cfg, &plant));
}

void test_long_press_after_sequence_is_prev_step() {
  // Even in SEURANTA (index == count) the hold is PREV_STEP — the router
  // re-enters the last step. Not the configurable action, not maintenance.
  PlantConfig plant = plantWithSteps();
  DeviceConfig cfg = cfgWithActiveStep(BUTTON_ACTION_EBB_FLOOD,
                                       BUTTON_LONG_ACTION_BACK);
  cfg.growStepIndex = 4;   // sarja valmis (4 askelta)
  TEST_ASSERT_EQUAL(INTENT_PREV_STEP,
    button_resolveIntent(BUTTON_EVENT_LONG_PRESS, DEVICE_GROWING, &cfg, &plant));
}

void test_long_press_maintenance_exit_wins_over_prev_step() {
  // The lock must never become un-exitable from the device: EXIT is checked
  // before back-navigation, so a hold in MAINTENANCE always releases it.
  PlantConfig plant = plantWithSteps();
  DeviceConfig cfg = cfgWithActiveStep(BUTTON_ACTION_NONE,
                                       BUTTON_LONG_ACTION_BACK);
  TEST_ASSERT_EQUAL(INTENT_MAINTENANCE_EXIT,
    button_resolveIntent(BUTTON_EVENT_LONG_PRESS, DEVICE_MAINTENANCE, &cfg, &plant));
}

void test_step_context_ignored_outside_growing_state() {
  // FAULT: short press still acks; long press is inert (BACK, not GROWING).
  PlantConfig plant = plantWithSteps();
  DeviceConfig cfg = cfgWithActiveStep(BUTTON_ACTION_NONE,
                                       BUTTON_LONG_ACTION_BACK);
  TEST_ASSERT_EQUAL(INTENT_ACK_FAULT,
    button_resolveIntent(BUTTON_EVENT_SHORT_PRESS, DEVICE_FAULT, &cfg, &plant));
  TEST_ASSERT_EQUAL(INTENT_NONE,
    button_resolveIntent(BUTTON_EVENT_LONG_PRESS, DEVICE_FAULT, &cfg, &plant));
}

// ── Demo mode (growDemoMode, boot-cleared): one short press walks the whole
// guidance sequence at the device — active step -> SKIP (even a TIMER step,
// no soak wait), sequence done -> NEXT_PHASE. Only in DEVICE_GROWING. ──────

void test_short_press_demo_skips_active_step() {
  // Demo overrides even the ACK_STEP row so a TIMER step advances too, and the
  // dangerous EBB_FLOOD buttonAction still never leaks through.
  PlantConfig plant = plantWithSteps();
  DeviceConfig cfg = cfgWithActiveStep(BUTTON_ACTION_EBB_FLOOD,
                                       kArbitraryNonBackLongAction);
  cfg.growDemoMode = true;
  TEST_ASSERT_EQUAL(INTENT_SKIP_STEP,
    button_resolveIntent(BUTTON_EVENT_SHORT_PRESS, DEVICE_GROWING, &cfg, &plant));
}

void test_short_press_demo_advances_phase_after_sequence() {
  // Sequence finished -> demo advances to the next phase instead of the
  // configurable action (which is EBB_FLOOD here). Layer 3 still gates it.
  PlantConfig plant = plantWithSteps();
  DeviceConfig cfg = cfgWithActiveStep(BUTTON_ACTION_EBB_FLOOD,
                                       kArbitraryNonBackLongAction);
  cfg.growDemoMode  = true;
  cfg.growStepIndex = 4;   // basil-siemenlistan pituus (4) -> sarja valmis
  TEST_ASSERT_EQUAL(INTENT_NEXT_PHASE,
    button_resolveIntent(BUTTON_EVENT_SHORT_PRESS, DEVICE_GROWING, &cfg, &plant));
}

void test_demo_does_not_hijack_non_growing_states() {
  // Demo only touches GROWING: IDLE still starts a grow, FAULT still acks.
  PlantConfig plant = plantWithSteps();
  DeviceConfig cfg = cfgWithActiveStep(BUTTON_ACTION_NONE,
                                       kArbitraryNonBackLongAction);
  cfg.growDemoMode = true;
  TEST_ASSERT_EQUAL(INTENT_START_GROWING,
    button_resolveIntent(BUTTON_EVENT_SHORT_PRESS, DEVICE_IDLE, &cfg, &plant));
  TEST_ASSERT_EQUAL(INTENT_ACK_FAULT,
    button_resolveIntent(BUTTON_EVENT_SHORT_PRESS, DEVICE_FAULT, &cfg, &plant));
}

void test_demo_off_keeps_ack_step_behavior() {
  // Regression: with demo off the active-step short press is still ACK_STEP
  // (layer 3's TIMER soak-safety then still applies, unchanged).
  PlantConfig plant = plantWithSteps();
  DeviceConfig cfg = cfgWithActiveStep(BUTTON_ACTION_NONE,
                                       kArbitraryNonBackLongAction);
  cfg.growDemoMode = false;
  TEST_ASSERT_EQUAL(INTENT_ACK_STEP,
    button_resolveIntent(BUTTON_EVENT_SHORT_PRESS, DEVICE_GROWING, &cfg, &plant));
}

// ── NONE gesture: always INTENT_NONE regardless of state/config ────────

void test_none_gesture_yields_no_intent() {
  DeviceConfig cfg = cfgWithAction(BUTTON_ACTION_EBB_FLOOD);
  TEST_ASSERT_EQUAL(INTENT_NONE,
    button_resolveIntent(BUTTON_EVENT_NONE, DEVICE_IDLE, &cfg, nullptr));
}

// ── main ──────────────────────────────────────────────────────────────

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_long_press_growing_is_prev_step);
  RUN_TEST(test_long_press_idle_is_inert);
  RUN_TEST(test_long_press_in_maintenance_exits);
  RUN_TEST(test_long_press_ignores_short_action_config);
  RUN_TEST(test_short_press_in_maintenance_yields_no_intent);
  RUN_TEST(test_short_press_idle_starts_growing);
  RUN_TEST(test_short_press_idle_ignores_button_action_config);
  RUN_TEST(test_short_press_idle_null_config_still_starts_growing);
  RUN_TEST(test_short_press_fault_acks_fault);
  RUN_TEST(test_short_press_fault_ignores_button_action_config);
  RUN_TEST(test_short_press_growing_none_yields_no_intent);
  RUN_TEST(test_short_press_growing_ebb_flood_via_config);
  RUN_TEST(test_short_press_growing_shutdown_still_reachable_via_config);
  RUN_TEST(test_short_press_self_test_reboot_via_config);
  RUN_TEST(test_short_press_null_config_is_safe_in_growing);
  RUN_TEST(test_short_press_during_step_is_always_ack_step);
  RUN_TEST(test_short_press_after_sequence_advances_phase);
  RUN_TEST(test_short_press_after_sequence_advances_even_past_the_count);
  RUN_TEST(test_short_press_without_any_step_list_keeps_button_action);
  RUN_TEST(test_completed_sequence_does_not_advance_outside_growing);
  RUN_TEST(test_long_press_still_undoes_after_sequence_completed);
  RUN_TEST(test_long_press_during_step_is_prev_step);
  RUN_TEST(test_long_press_after_sequence_is_prev_step);
  RUN_TEST(test_long_press_maintenance_exit_wins_over_prev_step);
  RUN_TEST(test_step_context_ignored_outside_growing_state);
  RUN_TEST(test_short_press_demo_skips_active_step);
  RUN_TEST(test_short_press_demo_advances_phase_after_sequence);
  RUN_TEST(test_demo_does_not_hijack_non_growing_states);
  RUN_TEST(test_demo_off_keeps_ack_step_behavior);
  RUN_TEST(test_none_gesture_yields_no_intent);
  return UNITY_END();
}
