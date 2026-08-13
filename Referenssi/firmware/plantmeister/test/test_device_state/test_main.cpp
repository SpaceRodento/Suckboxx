#include <unity.h>

#include "test_feature_flags.h"
#include "device_state.h"

void setUp() {
  g_stub_millis = 1000;
  device_init();
}

void tearDown() {}

// ── Initialization ─────────────────────────────────────────────────

void test_init_sets_state_to_init() {
  TEST_ASSERT_EQUAL_UINT8(DEVICE_INIT, g_device.state);
  TEST_ASSERT_EQUAL_UINT8(DEVICE_INIT, g_device.prevState);
  TEST_ASSERT_EQUAL_UINT8(DEVICE_FAULT_NONE, g_device.faults);
  TEST_ASSERT_EQUAL_STRING("", g_device.faultMessage);
  TEST_ASSERT_EQUAL_UINT8(DEVICE_FAULT_NONE, g_device.advisories);
  TEST_ASSERT_EQUAL_STRING("", g_device.advisoryMessage);
  TEST_ASSERT_EQUAL_UINT8(0, g_device.historyCount);
  TEST_ASSERT_EQUAL_UINT8(0, g_device.historyIdx);
}

// ── State transitions ─────────────────────────────────────────────

void test_set_state_records_transition() {
  bool ok = device_setState(DEVICE_IDLE);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_UINT8(DEVICE_IDLE, g_device.state);
  TEST_ASSERT_EQUAL_UINT8(DEVICE_INIT, g_device.prevState);
  TEST_ASSERT_EQUAL_UINT8(1, g_device.historyCount);
  TEST_ASSERT_EQUAL_UINT8(DEVICE_INIT, g_device.history[0].from);
  TEST_ASSERT_EQUAL_UINT8(DEVICE_IDLE, g_device.history[0].to);
}

void test_set_state_to_same_state_is_noop() {
  device_setState(DEVICE_IDLE);
  uint8_t count_before = g_device.historyCount;
  bool ok = device_setState(DEVICE_IDLE);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_UINT8(count_before, g_device.historyCount);
}

void test_set_state_invalid_is_rejected() {
  bool ok = device_setState((DeviceState)99);
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_EQUAL_UINT8(DEVICE_INIT, g_device.state);
}

void test_set_state_updates_entered_at_timestamp() {
  g_stub_millis = 5000;
  device_setState(DEVICE_IDLE);
  TEST_ASSERT_EQUAL_UINT32(5000, g_device.enteredAt);
}

// ── Fault handling (BLOCKING bits: WATER, MOTOR) ───────────────────
// Note: DEVICE_FAULT_SENSOR/LORA/etc. are advisory (do not lock) — see the
// advisory section below. Generic "fault locks the device" tests use a
// blocking bit (DEVICE_FAULT_WATER).

void test_set_fault_transitions_to_fault_state() {
  device_setState(DEVICE_IDLE);
  device_setFault(DEVICE_FAULT_WATER, "Ylivuoto");
  TEST_ASSERT_EQUAL_UINT8(DEVICE_FAULT, g_device.state);
  TEST_ASSERT_EQUAL_UINT8(DEVICE_FAULT_WATER, g_device.faults);
  TEST_ASSERT_EQUAL_STRING("Ylivuoto", g_device.faultMessage);
  TEST_ASSERT_EQUAL_UINT8(DEVICE_IDLE, g_device.prevState);
}

void test_set_fault_accumulates_bits() {
  device_setFault(DEVICE_FAULT_WATER, "Vesi");
  device_setFault(DEVICE_FAULT_MOTOR, "Moottori");
  uint8_t expected = DEVICE_FAULT_WATER | DEVICE_FAULT_MOTOR;
  TEST_ASSERT_EQUAL_UINT8(expected, g_device.faults);
  // Second message overwrites first (last fault wins)
  TEST_ASSERT_EQUAL_STRING("Moottori", g_device.faultMessage);
}

void test_set_fault_truncates_long_message() {
  // DEVICE_FAULT_MSG_LEN = 48, so 47 chars + nullterm
  const char* long_msg = "A_very_long_fault_message_that_definitely_exceeds_the_buffer_limit_set_by_the_module";
  device_setFault(DEVICE_FAULT_WATER, long_msg);
  TEST_ASSERT_EQUAL_INT(DEVICE_FAULT_MSG_LEN - 1, (int)strlen(g_device.faultMessage));
  // Should not crash, should be null-terminated
  TEST_ASSERT_EQUAL_CHAR('\0', g_device.faultMessage[DEVICE_FAULT_MSG_LEN - 1]);
}

void test_clear_fault_returns_to_idle() {
  // Aiemmin tama palautti prevState-tilaan. Nyt: oletuksena IDLE jotta
  // operaattori paasee aina luotettavaan stable-tilaan CLEAR_FAULT:in
  // jalkeen (prevState voi olla SELF_TEST joka jumiisi laitteen).
  // Ainoa poikkeus on MAINTENANCE, ks. test_clear_fault_returns_to_maintenance.
  device_setState(DEVICE_IDLE);
  device_setFault(DEVICE_FAULT_WATER, "Ylivuoto");
  device_clearFault(DEVICE_FAULT_WATER);
  TEST_ASSERT_EQUAL_UINT8(DEVICE_IDLE, g_device.state);
  TEST_ASSERT_EQUAL_UINT8(DEVICE_FAULT_NONE, g_device.faults);
  TEST_ASSERT_EQUAL_STRING("", g_device.faultMessage);
}

void test_clear_blocking_fault_from_self_test_returns_to_idle() {
  // CLEAR_FAULT palauttaa aina IDLE-tilaan, myos kun fault syntyi
  // SELF_TEST-tilassa (prevState voisi muuten jumittaa laitteen).
  device_setState(DEVICE_SELF_TEST);
  device_setFault(DEVICE_FAULT_WATER, "Ylivuoto");
  TEST_ASSERT_EQUAL_UINT8(DEVICE_FAULT, g_device.state);
  device_clearAllFaults();
  TEST_ASSERT_EQUAL_UINT8(DEVICE_IDLE, g_device.state);
  TEST_ASSERT_FALSE(device_hasFault());
}

void test_clear_fault_returns_to_maintenance() {
  // D13: huoltotila on kayttajan tekema turvalukko (pumppu + moottori pois).
  // Jos vika syntyy huollon aikana, kuittaus saa poistaa vian mutta EI
  // huoltotilaa — muuten lukot vapautuvat hiljaa kesken huollon.
  device_setState(DEVICE_MAINTENANCE);
  device_setFault(DEVICE_FAULT_WATER, "Ylivuoto");
  TEST_ASSERT_EQUAL_UINT8(DEVICE_FAULT, g_device.state);
  device_clearFault(DEVICE_FAULT_WATER);
  TEST_ASSERT_EQUAL_UINT8(DEVICE_MAINTENANCE, g_device.state);
  TEST_ASSERT_FALSE(device_hasFault());
}

void test_clear_partial_fault_keeps_fault_state() {
  device_setFault(DEVICE_FAULT_WATER, "V");
  device_setFault(DEVICE_FAULT_MOTOR, "M");
  device_clearFault(DEVICE_FAULT_WATER);
  TEST_ASSERT_EQUAL_UINT8(DEVICE_FAULT, g_device.state);
  TEST_ASSERT_EQUAL_UINT8(DEVICE_FAULT_MOTOR, g_device.faults);
}

void test_clear_all_faults_clears_bitmask() {
  device_setFault(DEVICE_FAULT_WATER | DEVICE_FAULT_MOTOR, "Useita");
  device_clearAllFaults();
  TEST_ASSERT_EQUAL_UINT8(DEVICE_FAULT_NONE, g_device.faults);
  TEST_ASSERT_FALSE(device_hasFault());
}

void test_has_fault_reports_status() {
  TEST_ASSERT_FALSE(device_hasFault());
  device_setFault(DEVICE_FAULT_WATER, "X");
  TEST_ASSERT_TRUE(device_hasFault());
  device_clearAllFaults();
  TEST_ASSERT_FALSE(device_hasFault());
}

// ── Advisory handling (non-blocking: SENSOR, LORA, BROWNOUT, ...) ───

void test_advisory_does_not_lock_device() {
  // architecture.md § 8: irti oleva oheislaite EI saa lukita laitetta.
  device_setState(DEVICE_IDLE);
  device_setAdvisory(DEVICE_FAULT_SENSOR, "Anturi ei vastaa");
  TEST_ASSERT_EQUAL_UINT8(DEVICE_IDLE, g_device.state);   // ei FAULT
  TEST_ASSERT_FALSE(device_hasFault());
  TEST_ASSERT_TRUE(device_hasAdvisory());
  TEST_ASSERT_EQUAL_UINT8(DEVICE_FAULT_SENSOR, g_device.advisories);
  TEST_ASSERT_EQUAL_STRING("Anturi ei vastaa", g_device.advisoryMessage);
}

void test_advisory_while_growing_does_not_disturb() {
  device_setState(DEVICE_IDLE);
  device_setState(DEVICE_GROWING);
  device_setAdvisory(DEVICE_FAULT_LORA, "Radio ei vastaa");
  TEST_ASSERT_EQUAL_UINT8(DEVICE_GROWING, g_device.state);
  TEST_ASSERT_TRUE(device_hasAdvisory());
}

void test_set_fault_routes_advisory_bit_without_locking() {
  // Boot self-test syottaa anturipuutteen device_setFault:in kautta.
  // SENSOR on advisory -> ei saa lukita, vaan paatya advisories-kenttaan.
  device_setState(DEVICE_IDLE);
  device_setFault(DEVICE_FAULT_SENSOR, "Anturi ei vastaa");
  TEST_ASSERT_EQUAL_UINT8(DEVICE_IDLE, g_device.state);
  TEST_ASSERT_EQUAL_UINT8(DEVICE_FAULT_NONE, g_device.faults);
  TEST_ASSERT_EQUAL_UINT8(DEVICE_FAULT_SENSOR, g_device.advisories);
  TEST_ASSERT_EQUAL_STRING("Anturi ei vastaa", g_device.advisoryMessage);
}

void test_set_fault_mixed_bits_route_correctly() {
  device_setState(DEVICE_IDLE);
  device_setFault(DEVICE_FAULT_WATER | DEVICE_FAULT_SENSOR, "Sekamuoto");
  // Blocking-bitti lukitsee, advisory-bitti tallentuu erikseen.
  TEST_ASSERT_EQUAL_UINT8(DEVICE_FAULT, g_device.state);
  TEST_ASSERT_EQUAL_UINT8(DEVICE_FAULT_WATER, g_device.faults);
  TEST_ASSERT_EQUAL_UINT8(DEVICE_FAULT_SENSOR, g_device.advisories);
}

void test_clear_advisory_clears_bit() {
  device_setAdvisory(DEVICE_FAULT_SENSOR, "X");
  device_clearAdvisory(DEVICE_FAULT_SENSOR);
  TEST_ASSERT_FALSE(device_hasAdvisory());
  TEST_ASSERT_EQUAL_STRING("", g_device.advisoryMessage);
}

void test_clear_all_faults_clears_advisories_too() {
  device_setAdvisory(DEVICE_FAULT_SENSOR, "Anturi");
  device_setFault(DEVICE_FAULT_WATER, "Ylivuoto");
  device_clearAllFaults();
  TEST_ASSERT_FALSE(device_hasFault());
  TEST_ASSERT_FALSE(device_hasAdvisory());
  TEST_ASSERT_EQUAL_UINT8(DEVICE_IDLE, g_device.state);
}

void test_self_test_sensor_advisory_boots_idle() {
  // Replikoi bootin reititys: SELF_TEST -> anturipuute advisory -> IDLE.
  // Tama on testi joka olisi estanyt 2026-06-09 vaarindiagnoosin.
  device_setState(DEVICE_SELF_TEST);
  uint8_t selfTestFaults = DEVICE_FAULT_SENSOR;  // self_test_run() tulos
  if (selfTestFaults) device_setFault(selfTestFaults, "Anturi ei vastaa");
  if (g_device.state != DEVICE_FAULT) device_setState(DEVICE_IDLE);
  TEST_ASSERT_EQUAL_UINT8(DEVICE_IDLE, g_device.state);
  TEST_ASSERT_TRUE(device_hasAdvisory());
  TEST_ASSERT_FALSE(device_hasFault());
}

// ── Health summary (ihmisluettava /api/status-kentta) ──────────────

void test_health_ok_when_idle_clean() {
  device_setState(DEVICE_IDLE);
  char h[DEVICE_HEALTH_LEN];
  device_healthSummary(h, sizeof(h));
  TEST_ASSERT_EQUAL_STRING("Toiminnassa", h);
}

void test_health_reports_advisory_as_degraded() {
  device_setState(DEVICE_IDLE);
  device_setAdvisory(DEVICE_FAULT_SENSOR, "Anturi ei vastaa");
  char h[DEVICE_HEALTH_LEN];
  device_healthSummary(h, sizeof(h));
  // Pitaa sanoa eksplisiittisesti ettei advisory esta toimintaa.
  TEST_ASSERT_NOT_NULL(strstr(h, "degraded"));
  TEST_ASSERT_NOT_NULL(strstr(h, "advisory"));
}

void test_health_reports_fault() {
  device_setFault(DEVICE_FAULT_WATER, "Ylivuoto");
  char h[DEVICE_HEALTH_LEN];
  device_healthSummary(h, sizeof(h));
  TEST_ASSERT_NOT_NULL(strstr(h, "FAULT"));
}

void test_health_warns_on_stalled_init() {
  // INIT pidempaan kuin DEVICE_INIT_STALL_MS => epailty boot-hang (Aukko A).
  // setUp: enteredAt=1000, state=INIT. Siirra kelloa yli kynnyksen.
  g_stub_millis = 1000 + DEVICE_INIT_STALL_MS + 1000;
  char h[DEVICE_HEALTH_LEN];
  device_healthSummary(h, sizeof(h));
  TEST_ASSERT_NOT_NULL(strstr(h, "VAROITUS"));
}

void test_health_no_stall_warning_during_normal_init_window() {
  // Lyhyt INIT (alle kynnyksen) ei saa varoittaa.
  g_stub_millis = 1000 + 2000;  // 2 s INIT:issa
  char h[DEVICE_HEALTH_LEN];
  device_healthSummary(h, sizeof(h));
  TEST_ASSERT_NULL(strstr(h, "VAROITUS"));
}

// ── Fault-locked transitions ──────────────────────────────────────

void test_cannot_leave_fault_directly_while_faults_active() {
  device_setFault(DEVICE_FAULT_WATER, "X");
  bool ok = device_setState(DEVICE_IDLE);
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_EQUAL_UINT8(DEVICE_FAULT, g_device.state);
}

void test_can_transition_from_fault_to_shutdown() {
  device_setFault(DEVICE_FAULT_WATER, "X");
  bool ok = device_setState(DEVICE_SHUTDOWN);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_UINT8(DEVICE_SHUTDOWN, g_device.state);
}

// ── History ringbuffer ────────────────────────────────────────────

void test_history_records_transitions_in_order() {
  device_setState(DEVICE_IDLE);
  device_setState(DEVICE_GROWING);
  TEST_ASSERT_EQUAL_UINT8(2, g_device.historyCount);
  TEST_ASSERT_EQUAL_UINT8(DEVICE_INIT,  g_device.history[0].from);
  TEST_ASSERT_EQUAL_UINT8(DEVICE_IDLE,  g_device.history[0].to);
  TEST_ASSERT_EQUAL_UINT8(DEVICE_IDLE,  g_device.history[1].from);
  TEST_ASSERT_EQUAL_UINT8(DEVICE_GROWING, g_device.history[1].to);
}

void test_history_count_caps_at_buffer_size() {
  for (int i = 0; i < DEVICE_HISTORY_SIZE + 5; i++) {
    DeviceState target = (i % 2 == 0) ? DEVICE_IDLE : DEVICE_GROWING;
    device_setState(target);
  }
  TEST_ASSERT_EQUAL_UINT8(DEVICE_HISTORY_SIZE, g_device.historyCount);
}

void test_history_index_wraps_modulo() {
  for (int i = 0; i < DEVICE_HISTORY_SIZE + 1; i++) {
    DeviceState target = (i % 2 == 0) ? DEVICE_IDLE : DEVICE_GROWING;
    device_setState(target);
  }
  // After N+1 transitions, index points to slot 1 (overwrote slot 0)
  TEST_ASSERT_EQUAL_UINT8(1, g_device.historyIdx);
}

// ── State names ───────────────────────────────────────────────────

void test_state_name_returns_known_strings() {
  TEST_ASSERT_EQUAL_STRING("INIT",      device_stateName(DEVICE_INIT));
  TEST_ASSERT_EQUAL_STRING("IDLE",      device_stateName(DEVICE_IDLE));
  TEST_ASSERT_EQUAL_STRING("GROWING",   device_stateName(DEVICE_GROWING));
  TEST_ASSERT_EQUAL_STRING("FAULT",     device_stateName(DEVICE_FAULT));
  TEST_ASSERT_EQUAL_STRING("SHUTDOWN",  device_stateName(DEVICE_SHUTDOWN));
}

void test_state_name_returns_unknown_for_invalid() {
  TEST_ASSERT_EQUAL_STRING("UNKNOWN", device_stateName((DeviceState)99));
}

// ── Time in state ─────────────────────────────────────────────────

void test_time_in_state_grows_with_millis() {
  g_stub_millis = 1000;
  device_setState(DEVICE_IDLE);
  g_stub_millis = 4500;
  TEST_ASSERT_EQUAL_UINT32(3500, device_timeInState());
}

// ── Safe mode (boot-loop recovery posture) ─────────────────────────

void test_safe_mode_default_off() {
  // device_init() (setUp) leaves safe mode off; health must not mention it.
  TEST_ASSERT_FALSE(device_inSafeMode());
  device_setState(DEVICE_IDLE);
  char h[DEVICE_HEALTH_LEN];
  device_healthSummary(h, sizeof(h));
  TEST_ASSERT_NULL(strstr(h, "SAFE MODE"));
}

void test_safe_mode_health_summary() {
  device_setState(DEVICE_IDLE);
  device_setSafeMode(true);
  TEST_ASSERT_TRUE(device_inSafeMode());
  char h[DEVICE_HEALTH_LEN];
  device_healthSummary(h, sizeof(h));
  TEST_ASSERT_EQUAL_INT(0, strncmp(h, "SAFE MODE", 9));
}

void test_safe_mode_takes_priority_over_fault() {
  // Safe mode is the highest-priority health branch (plan § 4.3): even with a
  // blocking fault bit set, the summary reports SAFE MODE first.
  device_setSafeMode(true);
  device_setFault(DEVICE_FAULT_WATER, "Ylivuoto");
  char h[DEVICE_HEALTH_LEN];
  device_healthSummary(h, sizeof(h));
  TEST_ASSERT_EQUAL_INT(0, strncmp(h, "SAFE MODE", 9));
}

void test_safe_mode_does_not_block_transitions() {
  // Safe mode is a boot posture, NOT a fault — IDLE/GROWING transitions must
  // still work so the OTA/reboot path is never locked.
  device_setSafeMode(true);
  device_setState(DEVICE_IDLE);
  TEST_ASSERT_EQUAL_UINT8(DEVICE_IDLE, g_device.state);
  bool ok = device_setState(DEVICE_GROWING);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_UINT8(DEVICE_GROWING, g_device.state);
}

// ── Boot resume of a persisted grow program ────────────────────────

void test_resume_growing_when_grow_active_and_idle() {
  // Clean boot reached IDLE with a persisted active program -> resume GROWING.
  TEST_ASSERT_TRUE(device_shouldResumeGrowing(false, true, DEVICE_IDLE));
}

void test_no_resume_when_grow_inactive() {
  // No program persisted -> stay IDLE.
  TEST_ASSERT_FALSE(device_shouldResumeGrowing(false, false, DEVICE_IDLE));
}

void test_no_resume_in_safe_mode() {
  // After a crash we stay IDLE for investigation, never auto-resume actuators.
  TEST_ASSERT_FALSE(device_shouldResumeGrowing(true, true, DEVICE_IDLE));
}

void test_no_resume_when_blocking_fault_holds_device() {
  // A blocking FAULT leaves boot in DEVICE_FAULT (not IDLE) — must not override.
  TEST_ASSERT_FALSE(device_shouldResumeGrowing(false, true, DEVICE_FAULT));
}

// ── Maintenance mode ──────────────────────────────────────────────

void test_maintenance_entry_allowed_from_operating_states() {
  TEST_ASSERT_TRUE(device_maintenanceEntryAllowed(DEVICE_IDLE));
  TEST_ASSERT_TRUE(device_maintenanceEntryAllowed(DEVICE_GROWING));
}

void test_maintenance_entry_blocked_from_non_operating_states() {
  // FAULT already halts actuators and demands CLEAR_FAULT first; INIT/SELF_TEST
  // are mid-boot; SHUTDOWN is terminal.
  TEST_ASSERT_FALSE(device_maintenanceEntryAllowed(DEVICE_INIT));
  TEST_ASSERT_FALSE(device_maintenanceEntryAllowed(DEVICE_SELF_TEST));
  TEST_ASSERT_FALSE(device_maintenanceEntryAllowed(DEVICE_FAULT));
  TEST_ASSERT_FALSE(device_maintenanceEntryAllowed(DEVICE_SHUTDOWN));
  TEST_ASSERT_FALSE(device_maintenanceEntryAllowed(DEVICE_MAINTENANCE));
}

void test_maintenance_resumes_growing_when_program_active() {
  // A grow interrupted for servicing must resume, not be orphaned in IDLE.
  TEST_ASSERT_EQUAL_UINT8(DEVICE_GROWING, device_maintenanceResumeState(true));
}

void test_maintenance_resumes_idle_when_no_program() {
  TEST_ASSERT_EQUAL_UINT8(DEVICE_IDLE, device_maintenanceResumeState(false));
}

void test_maintenance_is_long_only_after_threshold() {
  TEST_ASSERT_FALSE(device_maintenanceIsLong(DEVICE_MAINTENANCE, 0));
  TEST_ASSERT_FALSE(device_maintenanceIsLong(DEVICE_MAINTENANCE,
                                             DEVICE_MAINTENANCE_LONG_MS - 1));
  TEST_ASSERT_TRUE(device_maintenanceIsLong(DEVICE_MAINTENANCE,
                                            DEVICE_MAINTENANCE_LONG_MS));
}

void test_maintenance_is_long_false_in_other_states() {
  // A device that has simply been GROWING for days is not "long maintenance".
  TEST_ASSERT_FALSE(device_maintenanceIsLong(DEVICE_GROWING,
                                             DEVICE_MAINTENANCE_LONG_MS * 10));
}

void test_maintenance_state_transition_is_recorded() {
  device_setState(DEVICE_GROWING);
  TEST_ASSERT_TRUE(device_setState(DEVICE_MAINTENANCE));
  TEST_ASSERT_EQUAL_UINT8(DEVICE_MAINTENANCE, g_device.state);
  TEST_ASSERT_EQUAL_UINT8(DEVICE_GROWING, g_device.prevState);
}

void test_blocking_fault_cannot_be_overridden_by_maintenance() {
  // Servicing must not be a back door out of a blocking fault: the water/motor
  // latch exists precisely because the device is unsafe to operate.
  device_setFault(DEVICE_FAULT_WATER, "Ylivuoto");
  TEST_ASSERT_EQUAL_UINT8(DEVICE_FAULT, g_device.state);
  TEST_ASSERT_FALSE(device_setState(DEVICE_MAINTENANCE));
  TEST_ASSERT_EQUAL_UINT8(DEVICE_FAULT, g_device.state);
}

void test_health_reports_maintenance_above_advisory() {
  // A locked pump must never read as a fault or as plain "degraded" — the
  // health field is what a human (or Claude) checks first.
  device_setState(DEVICE_IDLE);
  device_setAdvisory(DEVICE_FAULT_SENSOR, "Anturi ei vastaa");
  device_setState(DEVICE_MAINTENANCE);
  char buf[DEVICE_HEALTH_LEN];
  device_healthSummary(buf, sizeof(buf));
  TEST_ASSERT_NOT_NULL(strstr(buf, "HUOLTOTILA"));
}

void test_health_flags_long_maintenance() {
  device_setState(DEVICE_IDLE);
  device_setState(DEVICE_MAINTENANCE);
  g_stub_millis += DEVICE_MAINTENANCE_LONG_MS;
  char buf[DEVICE_HEALTH_LEN];
  device_healthSummary(buf, sizeof(buf));
  TEST_ASSERT_NOT_NULL(strstr(buf, "pitk"));
}

void test_state_name_covers_maintenance() {
  TEST_ASSERT_EQUAL_STRING("MAINTENANCE", device_stateName(DEVICE_MAINTENANCE));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_init_sets_state_to_init);
  RUN_TEST(test_set_state_records_transition);
  RUN_TEST(test_set_state_to_same_state_is_noop);
  RUN_TEST(test_set_state_invalid_is_rejected);
  RUN_TEST(test_set_state_updates_entered_at_timestamp);
  RUN_TEST(test_set_fault_transitions_to_fault_state);
  RUN_TEST(test_set_fault_accumulates_bits);
  RUN_TEST(test_set_fault_truncates_long_message);
  RUN_TEST(test_clear_fault_returns_to_idle);
  RUN_TEST(test_clear_blocking_fault_from_self_test_returns_to_idle);
  RUN_TEST(test_clear_fault_returns_to_maintenance);
  RUN_TEST(test_clear_partial_fault_keeps_fault_state);
  RUN_TEST(test_clear_all_faults_clears_bitmask);
  RUN_TEST(test_has_fault_reports_status);
  RUN_TEST(test_advisory_does_not_lock_device);
  RUN_TEST(test_advisory_while_growing_does_not_disturb);
  RUN_TEST(test_set_fault_routes_advisory_bit_without_locking);
  RUN_TEST(test_set_fault_mixed_bits_route_correctly);
  RUN_TEST(test_clear_advisory_clears_bit);
  RUN_TEST(test_clear_all_faults_clears_advisories_too);
  RUN_TEST(test_self_test_sensor_advisory_boots_idle);
  RUN_TEST(test_health_ok_when_idle_clean);
  RUN_TEST(test_health_reports_advisory_as_degraded);
  RUN_TEST(test_health_reports_fault);
  RUN_TEST(test_health_warns_on_stalled_init);
  RUN_TEST(test_health_no_stall_warning_during_normal_init_window);
  RUN_TEST(test_cannot_leave_fault_directly_while_faults_active);
  RUN_TEST(test_can_transition_from_fault_to_shutdown);
  RUN_TEST(test_history_records_transitions_in_order);
  RUN_TEST(test_history_count_caps_at_buffer_size);
  RUN_TEST(test_history_index_wraps_modulo);
  RUN_TEST(test_state_name_returns_known_strings);
  RUN_TEST(test_state_name_returns_unknown_for_invalid);
  RUN_TEST(test_time_in_state_grows_with_millis);
  RUN_TEST(test_safe_mode_default_off);
  RUN_TEST(test_safe_mode_health_summary);
  RUN_TEST(test_safe_mode_takes_priority_over_fault);
  RUN_TEST(test_safe_mode_does_not_block_transitions);
  RUN_TEST(test_resume_growing_when_grow_active_and_idle);
  RUN_TEST(test_no_resume_when_grow_inactive);
  RUN_TEST(test_no_resume_in_safe_mode);
  RUN_TEST(test_no_resume_when_blocking_fault_holds_device);
  RUN_TEST(test_maintenance_entry_allowed_from_operating_states);
  RUN_TEST(test_maintenance_entry_blocked_from_non_operating_states);
  RUN_TEST(test_maintenance_resumes_growing_when_program_active);
  RUN_TEST(test_maintenance_resumes_idle_when_no_program);
  RUN_TEST(test_maintenance_is_long_only_after_threshold);
  RUN_TEST(test_maintenance_is_long_false_in_other_states);
  RUN_TEST(test_maintenance_state_transition_is_recorded);
  RUN_TEST(test_blocking_fault_cannot_be_overridden_by_maintenance);
  RUN_TEST(test_health_reports_maintenance_above_advisory);
  RUN_TEST(test_health_flags_long_maintenance);
  RUN_TEST(test_state_name_covers_maintenance);
  return UNITY_END();
}
