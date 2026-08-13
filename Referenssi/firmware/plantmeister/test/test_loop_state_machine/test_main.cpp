#include <unity.h>

#include "test_feature_flags.h"

#include "loop_dispatch.h"

struct TaskCounters {
  int heartbeat;
  int sensors_idle;
  int sensors_growing;
  int lora_poll;
  int wifi_portal;
  int grow_scheduler;
  int ebb_flow;
  int actuators;
  int ui_idle;
  int ui_awaiting;
  int ui_fault;
  int ui_maintenance;
  int history;
  int serial_input;
  int shutdown;
};

static TaskCounters* counters_from(const LoopDispatchContext* ctx) {
  return static_cast<TaskCounters*>(ctx ? ctx->userData : nullptr);
}

static void count_heartbeat(const LoopDispatchContext* ctx) {
  TaskCounters* counters = counters_from(ctx);
  if (counters) counters->heartbeat++;
}

static void count_sensors_idle(const LoopDispatchContext* ctx) {
  TaskCounters* counters = counters_from(ctx);
  if (counters) counters->sensors_idle++;
}

static void count_sensors_growing(const LoopDispatchContext* ctx) {
  TaskCounters* counters = counters_from(ctx);
  if (counters) counters->sensors_growing++;
}

static void count_lora_poll(const LoopDispatchContext* ctx) {
  TaskCounters* counters = counters_from(ctx);
  if (counters) counters->lora_poll++;
}

static void count_wifi_portal(const LoopDispatchContext* ctx) {
  TaskCounters* counters = counters_from(ctx);
  if (counters) counters->wifi_portal++;
}

static void count_grow_scheduler(const LoopDispatchContext* ctx) {
  TaskCounters* counters = counters_from(ctx);
  if (counters) counters->grow_scheduler++;
}

static void count_ebb_flow(const LoopDispatchContext* ctx) {
  TaskCounters* counters = counters_from(ctx);
  if (counters) counters->ebb_flow++;
}

static void count_actuators(const LoopDispatchContext* ctx) {
  TaskCounters* counters = counters_from(ctx);
  if (counters) counters->actuators++;
}

static void count_ui_idle(const LoopDispatchContext* ctx) {
  TaskCounters* counters = counters_from(ctx);
  if (counters) counters->ui_idle++;
}

static void count_ui_awaiting(const LoopDispatchContext* ctx) {
  TaskCounters* counters = counters_from(ctx);
  if (counters) counters->ui_awaiting++;
}

static void count_ui_fault(const LoopDispatchContext* ctx) {
  TaskCounters* counters = counters_from(ctx);
  if (counters) counters->ui_fault++;
}

static void count_ui_maintenance(const LoopDispatchContext* ctx) {
  TaskCounters* counters = counters_from(ctx);
  if (counters) counters->ui_maintenance++;
}

static void count_history(const LoopDispatchContext* ctx) {
  TaskCounters* counters = counters_from(ctx);
  if (counters) counters->history++;
}

static void count_serial_input(const LoopDispatchContext* ctx) {
  TaskCounters* counters = counters_from(ctx);
  if (counters) counters->serial_input++;
}

static void count_shutdown(const LoopDispatchContext* ctx) {
  TaskCounters* counters = counters_from(ctx);
  if (counters) counters->shutdown++;
}

static LoopDispatchOps build_ops() {
  LoopDispatchOps ops = {};
  ops.heartbeat = count_heartbeat;
  ops.sensorsIdle = count_sensors_idle;
  ops.sensorsGrowing = count_sensors_growing;
  ops.loraPoll = count_lora_poll;
  ops.wifiPortal = count_wifi_portal;
  ops.growScheduler = count_grow_scheduler;
  ops.ebbFlow = count_ebb_flow;
  ops.actuators = count_actuators;
  ops.uiIdle = count_ui_idle;
  ops.uiAwaiting = count_ui_awaiting;
  ops.uiFault = count_ui_fault;
  ops.uiMaintenance = count_ui_maintenance;
  ops.history = count_history;
  ops.serialInput = count_serial_input;
  ops.shutdown = count_shutdown;
  return ops;
}

static void dispatch_state(DeviceState state, TaskCounters* counters) {
  LoopDispatchContext ctx = { counters };
  LoopDispatchOps ops = build_ops();
  loop_tick_for_state(state, &ctx, &ops);
}

void setUp() {
  g_stub_millis = 1234;
}

void tearDown() {}

void test_init_state_runs_nothing() {
  TaskCounters counters = {};
  dispatch_state(DEVICE_INIT, &counters);

  TEST_ASSERT_EQUAL_INT(0, counters.heartbeat);
  TEST_ASSERT_EQUAL_INT(0, counters.sensors_idle);
  TEST_ASSERT_EQUAL_INT(0, counters.sensors_growing);
  TEST_ASSERT_EQUAL_INT(0, counters.lora_poll);
  TEST_ASSERT_EQUAL_INT(0, counters.wifi_portal);
  TEST_ASSERT_EQUAL_INT(0, counters.grow_scheduler);
  TEST_ASSERT_EQUAL_INT(0, counters.actuators);
  TEST_ASSERT_EQUAL_INT(0, counters.ui_idle);
  TEST_ASSERT_EQUAL_INT(0, counters.ui_awaiting);
  TEST_ASSERT_EQUAL_INT(0, counters.ui_fault);
  TEST_ASSERT_EQUAL_INT(0, counters.history);
  TEST_ASSERT_EQUAL_INT(0, counters.serial_input);
  TEST_ASSERT_EQUAL_INT(0, counters.shutdown);
}

void test_idle_runs_sensors_lora_wifi_ui_and_serial() {
  TaskCounters counters = {};
  dispatch_state(DEVICE_IDLE, &counters);

  TEST_ASSERT_EQUAL_INT(1, counters.heartbeat);
  TEST_ASSERT_EQUAL_INT(1, counters.sensors_idle);
  TEST_ASSERT_EQUAL_INT(1, counters.lora_poll);
  TEST_ASSERT_EQUAL_INT(1, counters.wifi_portal);
  TEST_ASSERT_EQUAL_INT(1, counters.history);
  TEST_ASSERT_EQUAL_INT(1, counters.ui_idle);
  // SERIAL-DISPATCH-1: sarjakomennot kuunneltava myos idle-tilassa
  // (esim. GROW_START joka siirtaa idle -> growing)
  TEST_ASSERT_EQUAL_INT(1, counters.serial_input);
  // MANUAL-ACTUATOR-TEST: actuators ajetaan myos IDLE:ssa jotta puhelin /
  // portaali voi ajaa motor/pump/light-komentoja ilman GROWING-tilaa.
  TEST_ASSERT_EQUAL_INT(1, counters.actuators);
  // EBB-IDLE-FORCE-FLOOD: ebbFlow-task ajetaan IDLE:ssa jotta manuaalinen
  // force-flood (nappi / portaali "Aja tulva nyt") toimii ilman GROWING-tilaa.
  TEST_ASSERT_EQUAL_INT(1, counters.ebb_flow);

  TEST_ASSERT_EQUAL_INT(0, counters.sensors_growing);
  TEST_ASSERT_EQUAL_INT(0, counters.grow_scheduler);
  TEST_ASSERT_EQUAL_INT(0, counters.ui_awaiting);
  TEST_ASSERT_EQUAL_INT(0, counters.ui_fault);
  TEST_ASSERT_EQUAL_INT(0, counters.shutdown);
}

void test_growing_runs_full_dispatch() {
  TaskCounters counters = {};
  dispatch_state(DEVICE_GROWING, &counters);

  TEST_ASSERT_EQUAL_INT(1, counters.heartbeat);
  TEST_ASSERT_EQUAL_INT(1, counters.actuators);
  TEST_ASSERT_EQUAL_INT(1, counters.sensors_growing);
  TEST_ASSERT_EQUAL_INT(1, counters.grow_scheduler);
  // GROWING tikittää ebb:n growScheduler→scheduler_update kautta, joten erillistä
  // ebbFlow-taskia EI ajeta (vain IDLE:ssä) — muuten FSM tikitettäisiin kahdesti.
  TEST_ASSERT_EQUAL_INT(0, counters.ebb_flow);
  TEST_ASSERT_EQUAL_INT(1, counters.lora_poll);
  TEST_ASSERT_EQUAL_INT(1, counters.wifi_portal);
  TEST_ASSERT_EQUAL_INT(1, counters.history);
  TEST_ASSERT_EQUAL_INT(1, counters.ui_idle);
  TEST_ASSERT_EQUAL_INT(1, counters.serial_input);

  TEST_ASSERT_EQUAL_INT(0, counters.sensors_idle);
  TEST_ASSERT_EQUAL_INT(0, counters.ui_awaiting);
  TEST_ASSERT_EQUAL_INT(0, counters.ui_fault);
  TEST_ASSERT_EQUAL_INT(0, counters.shutdown);
}

void test_fault_runs_sensors_ui_and_serial_recovery() {
  TaskCounters counters = {};
  dispatch_state(DEVICE_FAULT, &counters);

  TEST_ASSERT_EQUAL_INT(1, counters.sensors_idle);
  TEST_ASSERT_EQUAL_INT(1, counters.ui_fault);
  // FAULT-RECOVERY: serialInput sallittu fault-tilassa (CLEAR_FAULT yms.)
  TEST_ASSERT_EQUAL_INT(1, counters.serial_input);

  TEST_ASSERT_EQUAL_INT(0, counters.heartbeat);
  TEST_ASSERT_EQUAL_INT(0, counters.sensors_growing);
  TEST_ASSERT_EQUAL_INT(0, counters.lora_poll);
  // wifi_portal-task EI aja FAULT-tilassa tarkoituksella: state-riippuvainen
  // AP-timeout (portal_update) on lukittu, MUTTA mDNS + OTA-reboot-timer +
  // STA-reconnect ajetaan SUORAAN loop()-tasolla (portal_loopCore), state-
  // dispatchin ohi. Sama kaava kuin sensors_readFloatSwitches: turvallisuus +
  // recovery escape state dispatch. Tata testaa erikseen integraatio-suite
  // (loop()-funktion sisalta), ei loop_dispatch_call-yksikkotesti.
  TEST_ASSERT_EQUAL_INT(0, counters.wifi_portal);
  TEST_ASSERT_EQUAL_INT(0, counters.grow_scheduler);
  TEST_ASSERT_EQUAL_INT(0, counters.actuators);
  TEST_ASSERT_EQUAL_INT(0, counters.ui_idle);
  TEST_ASSERT_EQUAL_INT(0, counters.ui_awaiting);
  TEST_ASSERT_EQUAL_INT(0, counters.history);
  TEST_ASSERT_EQUAL_INT(0, counters.shutdown);
}

void test_shutdown_runs_only_shutdown() {
  TaskCounters counters = {};
  dispatch_state(DEVICE_SHUTDOWN, &counters);

  TEST_ASSERT_EQUAL_INT(1, counters.shutdown);

  TEST_ASSERT_EQUAL_INT(0, counters.heartbeat);
  TEST_ASSERT_EQUAL_INT(0, counters.sensors_idle);
  TEST_ASSERT_EQUAL_INT(0, counters.sensors_growing);
  TEST_ASSERT_EQUAL_INT(0, counters.lora_poll);
  TEST_ASSERT_EQUAL_INT(0, counters.wifi_portal);
  TEST_ASSERT_EQUAL_INT(0, counters.grow_scheduler);
  TEST_ASSERT_EQUAL_INT(0, counters.actuators);
  TEST_ASSERT_EQUAL_INT(0, counters.ui_idle);
  TEST_ASSERT_EQUAL_INT(0, counters.ui_awaiting);
  TEST_ASSERT_EQUAL_INT(0, counters.ui_fault);
  TEST_ASSERT_EQUAL_INT(0, counters.history);
  TEST_ASSERT_EQUAL_INT(0, counters.serial_input);
}

// ── Maintenance ───────────────────────────────────────────────────────

void test_maintenance_never_runs_actuators_or_schedulers() {
  // The core safety property of the state, locked in code: nothing that *decides
  // to start* a flood may tick while the operator is servicing the device. If a
  // future change adds one of these to the maintenance case, this test fails
  // rather than the operator finding out with a hand in the reservoir.
  TaskCounters counters = {};
  dispatch_state(DEVICE_MAINTENANCE, &counters);
  TEST_ASSERT_EQUAL_INT(0, counters.grow_scheduler);
  TEST_ASSERT_EQUAL_INT(0, counters.ebb_flow);
  TEST_ASSERT_EQUAL_INT(0, counters.sensors_growing);
}

void test_maintenance_still_runs_actuator_housekeeping() {
  // Counter-intuitive but deliberate: the actuators task starts nothing. It
  // carries pump_update() (FLOAT_OVF overflow detection) and releases the motor
  // driver after a move. Skipping it would blind overflow detection during the
  // activity most likely to spill water. New starts are refused one layer down,
  // by the maintenance lock in pump_hal/motor_hal.
  TaskCounters counters = {};
  dispatch_state(DEVICE_MAINTENANCE, &counters);
  TEST_ASSERT_EQUAL_INT(1, counters.actuators);
}

void test_maintenance_keeps_observing_and_reporting() {
  // Everything that only observes or reports keeps running, so the operator can
  // watch readings while working and exit remotely if the button is out of reach.
  TaskCounters counters = {};
  dispatch_state(DEVICE_MAINTENANCE, &counters);
  TEST_ASSERT_EQUAL_INT(1, counters.heartbeat);
  TEST_ASSERT_EQUAL_INT(1, counters.sensors_idle);
  TEST_ASSERT_EQUAL_INT(1, counters.wifi_portal);
  TEST_ASSERT_EQUAL_INT(1, counters.serial_input);
  TEST_ASSERT_EQUAL_INT(1, counters.history);
  TEST_ASSERT_EQUAL_INT(1, counters.ui_maintenance);
  TEST_ASSERT_EQUAL_INT(0, counters.ui_idle);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_init_state_runs_nothing);
  RUN_TEST(test_idle_runs_sensors_lora_wifi_ui_and_serial);
  RUN_TEST(test_growing_runs_full_dispatch);
  RUN_TEST(test_fault_runs_sensors_ui_and_serial_recovery);
  RUN_TEST(test_shutdown_runs_only_shutdown);
  RUN_TEST(test_maintenance_never_runs_actuators_or_schedulers);
  RUN_TEST(test_maintenance_still_runs_actuator_housekeeping);
  RUN_TEST(test_maintenance_keeps_observing_and_reporting);
  return UNITY_END();
}
