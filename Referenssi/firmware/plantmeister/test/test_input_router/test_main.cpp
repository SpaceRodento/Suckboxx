#include <unity.h>

#define ENABLE_INPUT_ROUTER true
#define ENABLE_EBB_FLOW true   // exercise the ebb-fault ACK path (catch-22 regression)
#include "test_feature_flags.h"

#include "input_router.h"

// ── Stubs that input_router.h declares but does not define ────────
bool config_save(const DeviceConfig*) { return true; }

// ── Test fixture state ────────────────────────────────────────────
static SensorData    g_sensors;
static SystemState   g_state;
static DeviceConfig  g_config;
static PlantConfig   g_plant;
static PlantConfig*  g_activePlant_ptr;
static RouterContext g_ctx;

static void reset_plant_with_phases() {
  memset(&g_plant, 0, sizeof(g_plant));
  strncpy(g_plant.id,   "basil",     sizeof(g_plant.id)   - 1);
  strncpy(g_plant.name, "Basilika",  sizeof(g_plant.name) - 1);
  g_plant.phaseCount = 3;
  g_plant.phases[0].type = GROW_PHASE_ROOTING;
  strncpy(g_plant.phases[0].label, "Juurtuminen", sizeof(g_plant.phases[0].label) - 1);
  g_plant.phases[1].type = GROW_PHASE_SEEDLING;
  strncpy(g_plant.phases[1].label, "Taimivaihe",  sizeof(g_plant.phases[1].label) - 1);
  g_plant.phases[2].type = GROW_PHASE_VEGETATIVE;
  strncpy(g_plant.phases[2].label, "Kasvuvaihe",  sizeof(g_plant.phases[2].label) - 1);
}

void setUp() {
  g_stub_millis = 1000;
  device_init();
  memset(&g_sensors, 0, sizeof(g_sensors));
  memset(&g_state, 0, sizeof(g_state));
  memset(&g_config, 0, sizeof(g_config));
  reset_plant_with_phases();
  g_activePlant_ptr = &g_plant;
  g_ctx.sensors = &g_sensors;
  g_ctx.state = &g_state;
  g_ctx.config = &g_config;
  g_ctx.activePlant = &g_activePlant_ptr;
}

void tearDown() {}

// ── Context validation ────────────────────────────────────────────

void test_routes_null_context_returns_rejected() {
  IntentResult r = input_routeIntent(INTENT_START_GROWING, 0, INTENT_SOURCE_BUTTON, nullptr);
  TEST_ASSERT_FALSE(r.accepted);
  TEST_ASSERT_NOT_NULL(r.rejectReason);
}

void test_routes_null_pointer_in_context_rejected() {
  RouterContext bad = g_ctx;
  bad.config = nullptr;
  IntentResult r = input_routeIntent(INTENT_START_GROWING, 0, INTENT_SOURCE_BUTTON, &bad);
  TEST_ASSERT_FALSE(r.accepted);
}

// ── START_GROWING ────────────────────────────────────────────────

void test_start_growing_accepts_from_idle_with_cutting() {
  device_setState(DEVICE_IDLE);
  IntentResult r = input_routeIntent(INTENT_START_GROWING, 0, INTENT_SOURCE_WIFI, &g_ctx);
  TEST_ASSERT_TRUE(r.accepted);
  TEST_ASSERT_TRUE(g_config.growActive);
  TEST_ASSERT_EQUAL_UINT8(0, g_config.growStartMethod);
  TEST_ASSERT_EQUAL_UINT8(DEVICE_GROWING, g_device.state);
  // Cutting → ROOTING phase (index 0)
  TEST_ASSERT_EQUAL_UINT8(0, g_config.growPhase);
}

void test_start_growing_seed_picks_seedling_phase() {
  device_setState(DEVICE_IDLE);
  IntentResult r = input_routeIntent(INTENT_START_GROWING, 1, INTENT_SOURCE_BUTTON, &g_ctx);
  TEST_ASSERT_TRUE(r.accepted);
  TEST_ASSERT_EQUAL_UINT8(1, g_config.growStartMethod);
  // Seed → SEEDLING phase (index 1)
  TEST_ASSERT_EQUAL_UINT8(1, g_config.growPhase);
}

void test_start_growing_seedling_picks_vegetative_phase() {
  device_setState(DEVICE_IDLE);
  IntentResult r = input_routeIntent(INTENT_START_GROWING, 2, INTENT_SOURCE_LORA, &g_ctx);
  TEST_ASSERT_TRUE(r.accepted);
  TEST_ASSERT_EQUAL_UINT8(2, g_config.growStartMethod);
  // Seedling → VEGETATIVE phase (index 2)
  TEST_ASSERT_EQUAL_UINT8(2, g_config.growPhase);
}

void test_start_growing_rejected_when_growing() {
  device_setState(DEVICE_IDLE);
  input_routeIntent(INTENT_START_GROWING, 0, INTENT_SOURCE_WIFI, &g_ctx);
  // Now in DEVICE_GROWING — second start should reject
  IntentResult r = input_routeIntent(INTENT_START_GROWING, 0, INTENT_SOURCE_WIFI, &g_ctx);
  TEST_ASSERT_FALSE(r.accepted);
  TEST_ASSERT_EQUAL_STRING("wrong state", r.rejectReason);
}

void test_start_growing_rejected_when_fault() {
  device_setFault(DEVICE_FAULT_WATER, "X");
  IntentResult r = input_routeIntent(INTENT_START_GROWING, 0, INTENT_SOURCE_WIFI, &g_ctx);
  TEST_ASSERT_FALSE(r.accepted);
  TEST_ASSERT_EQUAL_STRING("wrong state", r.rejectReason);
}

void test_start_growing_rejected_when_invalid_value() {
  device_setState(DEVICE_IDLE);
  IntentResult r = input_routeIntent(INTENT_START_GROWING, 99, INTENT_SOURCE_WIFI, &g_ctx);
  TEST_ASSERT_FALSE(r.accepted);
  TEST_ASSERT_EQUAL_STRING("context invalid", r.rejectReason);
}

void test_start_growing_rejected_when_plant_has_no_phases() {
  device_setState(DEVICE_IDLE);
  g_plant.phaseCount = 0;
  IntentResult r = input_routeIntent(INTENT_START_GROWING, 0, INTENT_SOURCE_WIFI, &g_ctx);
  TEST_ASSERT_FALSE(r.accepted);
  TEST_ASSERT_EQUAL_STRING("no grow phases", r.rejectReason);
}

// ── STOP_GROWING ─────────────────────────────────────────────────

void test_stop_growing_accepted_from_growing() {
  device_setState(DEVICE_IDLE);
  input_routeIntent(INTENT_START_GROWING, 0, INTENT_SOURCE_WIFI, &g_ctx);
  IntentResult r = input_routeIntent(INTENT_STOP_GROWING, 0, INTENT_SOURCE_WIFI, &g_ctx);
  TEST_ASSERT_TRUE(r.accepted);
  TEST_ASSERT_FALSE(g_config.growActive);
  TEST_ASSERT_EQUAL_UINT8(DEVICE_IDLE, g_device.state);
}

void test_stop_growing_rejected_from_idle() {
  device_setState(DEVICE_IDLE);
  IntentResult r = input_routeIntent(INTENT_STOP_GROWING, 0, INTENT_SOURCE_WIFI, &g_ctx);
  TEST_ASSERT_FALSE(r.accepted);
  TEST_ASSERT_EQUAL_STRING("wrong state", r.rejectReason);
}

// Orphaned-grow recovery: a reboot restores growActive without restoring
// DEVICE_GROWING. STOP must still clear the flag from IDLE in that case.
void test_stop_growing_accepted_from_idle_when_grow_active() {
  device_setState(DEVICE_IDLE);
  g_config.growActive = true;  // simulate flag restored from config after reboot
  IntentResult r = input_routeIntent(INTENT_STOP_GROWING, 0, INTENT_SOURCE_WIFI, &g_ctx);
  TEST_ASSERT_TRUE(r.accepted);
  TEST_ASSERT_FALSE(g_config.growActive);
  TEST_ASSERT_EQUAL_UINT8(DEVICE_IDLE, g_device.state);
}

// ── NEXT_PHASE ───────────────────────────────────────────────────

void test_next_phase_advances_when_growing() {
  device_setState(DEVICE_IDLE);
  input_routeIntent(INTENT_START_GROWING, 0, INTENT_SOURCE_WIFI, &g_ctx);
  // Now at phase 0 (ROOTING)
  TEST_ASSERT_EQUAL_UINT8(0, g_config.growPhase);
  IntentResult r = input_routeIntent(INTENT_NEXT_PHASE, 0, INTENT_SOURCE_BUTTON, &g_ctx);
  TEST_ASSERT_TRUE(r.accepted);
  TEST_ASSERT_EQUAL_UINT8(1, g_config.growPhase);
  TEST_ASSERT_EQUAL_UINT16(0, g_config.growElapsedDays);
}

void test_next_phase_rejected_when_idle() {
  device_setState(DEVICE_IDLE);
  IntentResult r = input_routeIntent(INTENT_NEXT_PHASE, 0, INTENT_SOURCE_BUTTON, &g_ctx);
  TEST_ASSERT_FALSE(r.accepted);
  TEST_ASSERT_EQUAL_STRING("wrong state", r.rejectReason);
}

void test_next_phase_rejected_at_last_phase() {
  device_setState(DEVICE_IDLE);
  input_routeIntent(INTENT_START_GROWING, 0, INTENT_SOURCE_WIFI, &g_ctx);
  // Force to last phase (index 2 = phaseCount - 1)
  g_config.growPhase = g_plant.phaseCount - 1;
  IntentResult r = input_routeIntent(INTENT_NEXT_PHASE, 0, INTENT_SOURCE_BUTTON, &g_ctx);
  TEST_ASSERT_FALSE(r.accepted);
  TEST_ASSERT_EQUAL_STRING("context invalid", r.rejectReason);
}

// ── DELAY_PHASE ──────────────────────────────────────────────────

void test_delay_phase_accepted_when_pending() {
  device_setState(DEVICE_IDLE);
  input_routeIntent(INTENT_START_GROWING, 0, INTENT_SOURCE_WIFI, &g_ctx);
  g_state.growPhasePendingAdvance = true;
  IntentResult r = input_routeIntent(INTENT_DELAY_PHASE, 0, INTENT_SOURCE_BUTTON, &g_ctx);
  TEST_ASSERT_TRUE(r.accepted);
  TEST_ASSERT_EQUAL_UINT32(g_stub_millis, g_state.growAdvanceProposedMs);
}

void test_delay_phase_rejected_when_no_pending() {
  device_setState(DEVICE_IDLE);
  input_routeIntent(INTENT_START_GROWING, 0, INTENT_SOURCE_WIFI, &g_ctx);
  g_state.growPhasePendingAdvance = false;
  IntentResult r = input_routeIntent(INTENT_DELAY_PHASE, 0, INTENT_SOURCE_BUTTON, &g_ctx);
  TEST_ASSERT_FALSE(r.accepted);
  TEST_ASSERT_EQUAL_STRING("context invalid", r.rejectReason);
}

void test_delay_phase_rejected_from_idle() {
  device_setState(DEVICE_IDLE);
  g_state.growPhasePendingAdvance = true;
  IntentResult r = input_routeIntent(INTENT_DELAY_PHASE, 0, INTENT_SOURCE_BUTTON, &g_ctx);
  TEST_ASSERT_FALSE(r.accepted);
  TEST_ASSERT_EQUAL_STRING("wrong state", r.rejectReason);
}

// ── ACK_FAULT ────────────────────────────────────────────────────

void test_ack_fault_clears_faults_when_in_fault_state() {
  device_setFault(DEVICE_FAULT_WATER, "Ylivuoto");  // blocking fault (SENSOR on advisory)
  TEST_ASSERT_EQUAL_UINT8(DEVICE_FAULT, g_device.state);
  IntentResult r = input_routeIntent(INTENT_ACK_FAULT, 0, INTENT_SOURCE_BUTTON, &g_ctx);
  TEST_ASSERT_TRUE(r.accepted);
  TEST_ASSERT_EQUAL_UINT8(DEVICE_IDLE, g_device.state);
  TEST_ASSERT_FALSE(device_hasFault());
}

void test_ack_fault_in_maintenance_returns_to_maintenance() {
  // D13: huoltotila on turvalukko (pumppu + moottori pois). Jos vika syntyy
  // huollon aikana, kuittaus poistaa vian mutta EI huoltotilaa — muuten
  // lukot vapautuisivat hiljaa kun kadet ovat viela altaassa.
  device_setState(DEVICE_MAINTENANCE);
  device_setFault(DEVICE_FAULT_WATER, "Ylivuoto");
  TEST_ASSERT_EQUAL_UINT8(DEVICE_FAULT, g_device.state);
  IntentResult r = input_routeIntent(INTENT_ACK_FAULT, 0, INTENT_SOURCE_BUTTON, &g_ctx);
  TEST_ASSERT_TRUE(r.accepted);
  TEST_ASSERT_EQUAL_UINT8(DEVICE_MAINTENANCE, g_device.state);
  TEST_ASSERT_FALSE(device_hasFault());
}

void test_ack_fault_rejected_when_no_fault() {
  device_setState(DEVICE_IDLE);
  IntentResult r = input_routeIntent(INTENT_ACK_FAULT, 0, INTENT_SOURCE_BUTTON, &g_ctx);
  TEST_ASSERT_FALSE(r.accepted);
  TEST_ASSERT_EQUAL_STRING("wrong state", r.rejectReason);
}

void test_ack_fault_clears_ebb_latch_even_in_idle() {
  // Regression (catch-22, 2026-06-10 soak hw test): a latched ebb&flow FSM
  // fault is advisory at device level, so the device stays IDLE — ACK must
  // still be accepted and signal the FSM to clear, not require a reboot.
  device_setState(DEVICE_IDLE);
  g_state.ebbFlowFaultLatched = true;
  g_state.ebbFlowAckRequested = false;
  IntentResult r = input_routeIntent(INTENT_ACK_FAULT, 0, INTENT_SOURCE_BUTTON, &g_ctx);
  TEST_ASSERT_TRUE(r.accepted);
  TEST_ASSERT_TRUE(g_state.ebbFlowAckRequested);     // FSM will clear on next tick
  TEST_ASSERT_EQUAL_UINT8(DEVICE_IDLE, g_device.state);  // device was never locked
}

void test_ack_fault_clears_both_device_fault_and_ebb_latch() {
  device_setFault(DEVICE_FAULT_WATER, "Ylivuoto");   // blocking → device locked
  g_state.ebbFlowFaultLatched = true;
  g_state.ebbFlowAckRequested = false;
  IntentResult r = input_routeIntent(INTENT_ACK_FAULT, 0, INTENT_SOURCE_BUTTON, &g_ctx);
  TEST_ASSERT_TRUE(r.accepted);
  TEST_ASSERT_EQUAL_UINT8(DEVICE_IDLE, g_device.state);
  TEST_ASSERT_FALSE(device_hasFault());
  TEST_ASSERT_TRUE(g_state.ebbFlowAckRequested);
}

// ── SHUTDOWN ─────────────────────────────────────────────────────

// ── Maintenance ───────────────────────────────────────────────────

void test_maintenance_enter_from_idle() {
  device_setState(DEVICE_IDLE);
  IntentResult r = input_routeIntent(INTENT_MAINTENANCE_ENTER, 0, INTENT_SOURCE_BUTTON, &g_ctx);
  TEST_ASSERT_TRUE(r.accepted);
  TEST_ASSERT_EQUAL_UINT8(DEVICE_MAINTENANCE, g_device.state);
}

void test_maintenance_enter_from_growing_keeps_program_active() {
  // Servicing pauses the grow; it must not cancel it. growActive stays true so
  // the exit resumes GROWING instead of orphaning the program in IDLE.
  device_setState(DEVICE_GROWING);
  g_config.growActive = true;
  IntentResult r = input_routeIntent(INTENT_MAINTENANCE_ENTER, 0, INTENT_SOURCE_BUTTON, &g_ctx);
  TEST_ASSERT_TRUE(r.accepted);
  TEST_ASSERT_EQUAL_UINT8(DEVICE_MAINTENANCE, g_device.state);
  TEST_ASSERT_TRUE(g_config.growActive);
}

void test_maintenance_enter_rejected_in_fault() {
  device_setFault(DEVICE_FAULT_WATER, "Ylivuoto");
  IntentResult r = input_routeIntent(INTENT_MAINTENANCE_ENTER, 0, INTENT_SOURCE_BUTTON, &g_ctx);
  TEST_ASSERT_FALSE(r.accepted);
  TEST_ASSERT_EQUAL_STRING("wrong state", r.rejectReason);
  TEST_ASSERT_EQUAL_UINT8(DEVICE_FAULT, g_device.state);
}

void test_maintenance_exit_resumes_growing_when_program_active() {
  device_setState(DEVICE_GROWING);
  g_config.growActive = true;
  input_routeIntent(INTENT_MAINTENANCE_ENTER, 0, INTENT_SOURCE_BUTTON, &g_ctx);
  IntentResult r = input_routeIntent(INTENT_MAINTENANCE_EXIT, 0, INTENT_SOURCE_BUTTON, &g_ctx);
  TEST_ASSERT_TRUE(r.accepted);
  TEST_ASSERT_EQUAL_UINT8(DEVICE_GROWING, g_device.state);
}

void test_maintenance_exit_returns_to_idle_without_program() {
  device_setState(DEVICE_IDLE);
  g_config.growActive = false;
  input_routeIntent(INTENT_MAINTENANCE_ENTER, 0, INTENT_SOURCE_BUTTON, &g_ctx);
  IntentResult r = input_routeIntent(INTENT_MAINTENANCE_EXIT, 0, INTENT_SOURCE_BUTTON, &g_ctx);
  TEST_ASSERT_TRUE(r.accepted);
  TEST_ASSERT_EQUAL_UINT8(DEVICE_IDLE, g_device.state);
}

void test_maintenance_exit_rejected_when_not_in_maintenance() {
  device_setState(DEVICE_IDLE);
  IntentResult r = input_routeIntent(INTENT_MAINTENANCE_EXIT, 0, INTENT_SOURCE_WIFI, &g_ctx);
  TEST_ASSERT_FALSE(r.accepted);
  TEST_ASSERT_EQUAL_STRING("wrong state", r.rejectReason);
}

void test_maintenance_enter_is_idempotent_from_maintenance() {
  // A second hold must not silently toggle anything; the map sends EXIT there.
  device_setState(DEVICE_IDLE);
  input_routeIntent(INTENT_MAINTENANCE_ENTER, 0, INTENT_SOURCE_BUTTON, &g_ctx);
  IntentResult r = input_routeIntent(INTENT_MAINTENANCE_ENTER, 0, INTENT_SOURCE_BUTTON, &g_ctx);
  TEST_ASSERT_FALSE(r.accepted);
  TEST_ASSERT_EQUAL_UINT8(DEVICE_MAINTENANCE, g_device.state);
}

void test_shutdown_always_accepted() {
  device_setState(DEVICE_IDLE);
  IntentResult r = input_routeIntent(INTENT_SHUTDOWN, 0, INTENT_SOURCE_BUTTON, &g_ctx);
  TEST_ASSERT_TRUE(r.accepted);
  TEST_ASSERT_EQUAL_UINT8(DEVICE_SHUTDOWN, g_device.state);
}

void test_shutdown_accepted_from_fault() {
  device_setFault(DEVICE_FAULT_WATER, "X");
  IntentResult r = input_routeIntent(INTENT_SHUTDOWN, 0, INTENT_SOURCE_BUTTON, &g_ctx);
  TEST_ASSERT_TRUE(r.accepted);
  TEST_ASSERT_EQUAL_UINT8(DEVICE_SHUTDOWN, g_device.state);
}

// ── Askel-intentit (ACK/SKIP/SET, grow_step_fsm.h) ───────────────
// Basil + siemen -> aktiivinen vaihe SEEDLING (indeksi 1) -> oikea askellista
// grow_steps.h:sta (Fable §3.1): [0]=valmistele BUTTON, [1]=liotus TIMER 30 min,
// [2]=kylvo BUTTON, [3]=idatys BUTTON.

static void start_seed_grow() {
  device_setState(DEVICE_IDLE);
  input_routeIntent(INTENT_START_GROWING, 1, INTENT_SOURCE_WIFI, &g_ctx);
}

void test_start_growing_resets_step_progress() {
  g_config.growStepIndex = 2;   // roikkuva arvo edellisesta kasvatuksesta
  start_seed_grow();
  TEST_ASSERT_EQUAL_UINT8(0, g_config.growStepIndex);
  TEST_ASSERT_EQUAL_UINT32(g_stub_millis, g_state.growStepStartMs);
}

void test_next_phase_resets_step_progress() {
  start_seed_grow();
  g_config.growStepIndex = 2;
  input_routeIntent(INTENT_NEXT_PHASE, 0, INTENT_SOURCE_WIFI, &g_ctx);
  TEST_ASSERT_EQUAL_UINT8(0, g_config.growStepIndex);
}

void test_ack_step_rejected_on_timer_step() {
  // Askel 1 on liotus (TIMER): harhapainallus ei saa lyhentaa sita.
  // (Askel 0 = valmistele BUTTON -> kuitataan ensin pois.)
  start_seed_grow();
  input_routeIntent(INTENT_ACK_STEP, 0, INTENT_SOURCE_BUTTON, &g_ctx);  // valmistele ohi
  IntentResult r = input_routeIntent(INTENT_ACK_STEP, 0, INTENT_SOURCE_BUTTON, &g_ctx);
  TEST_ASSERT_FALSE(r.accepted);
  TEST_ASSERT_EQUAL_STRING("timer step (use skip)", r.rejectReason);
  TEST_ASSERT_EQUAL_UINT8(1, g_config.growStepIndex);
}

void test_skip_step_advances_even_timer_step() {
  // SKIP on tahallinen ele/komento — se ohittaa myos ajastimen (askel 1 = liotus).
  start_seed_grow();
  input_routeIntent(INTENT_ACK_STEP, 0, INTENT_SOURCE_BUTTON, &g_ctx);  // valmistele ohi -> liotus
  IntentResult r = input_routeIntent(INTENT_SKIP_STEP, 0, INTENT_SOURCE_BUTTON, &g_ctx);
  TEST_ASSERT_TRUE(r.accepted);
  TEST_ASSERT_EQUAL_UINT8(2, g_config.growStepIndex);   // liotus (TIMER) ohitettu
}

void test_ack_step_advances_button_step_and_ends_sequence() {
  start_seed_grow();
  // Askel 0 (valmistele) on BUTTON -> kuittaus etenee.
  IntentResult r = input_routeIntent(INTENT_ACK_STEP, 0, INTENT_SOURCE_BUTTON, &g_ctx);
  TEST_ASSERT_TRUE(r.accepted);
  TEST_ASSERT_EQUAL_UINT8(1, g_config.growStepIndex);
  // Askel 1 (liotus) on TIMER -> ohitetaan SKIP:lla.
  input_routeIntent(INTENT_SKIP_STEP, 0, INTENT_SOURCE_WIFI, &g_ctx);
  TEST_ASSERT_EQUAL_UINT8(2, g_config.growStepIndex);
  // Askeleet 2 (kylvo) ja 3 (idatys) ovat BUTTON -> kuittaus etenee.
  r = input_routeIntent(INTENT_ACK_STEP, 0, INTENT_SOURCE_BUTTON, &g_ctx);
  TEST_ASSERT_TRUE(r.accepted);
  TEST_ASSERT_EQUAL_UINT8(3, g_config.growStepIndex);
  r = input_routeIntent(INTENT_ACK_STEP, 0, INTENT_SOURCE_BUTTON, &g_ctx);
  TEST_ASSERT_TRUE(r.accepted);
  TEST_ASSERT_EQUAL_UINT8(4, g_config.growStepIndex);   // == count -> sarja valmis

  // Sarjan jalkeen kuittaus hylataan — vaihetta EI edeta kuittauksesta.
  r = input_routeIntent(INTENT_ACK_STEP, 0, INTENT_SOURCE_BUTTON, &g_ctx);
  TEST_ASSERT_FALSE(r.accepted);
  TEST_ASSERT_EQUAL_STRING("no active step", r.rejectReason);
  TEST_ASSERT_EQUAL_UINT8(1, g_config.growPhase);       // vaihe ennallaan
}

void test_ack_step_rejected_outside_growing() {
  device_setState(DEVICE_IDLE);
  IntentResult r = input_routeIntent(INTENT_ACK_STEP, 0, INTENT_SOURCE_BUTTON, &g_ctx);
  TEST_ASSERT_FALSE(r.accepted);
  TEST_ASSERT_EQUAL_STRING("wrong state", r.rejectReason);
}

void test_ack_step_rejected_when_no_step_list() {
  // Vaihe jonka TYYPILLE ei ole askellistaa. Ennen tama nojasi VEGETATIVEn
  // (kaupan taimi, method 2) askeleettomuuteen, mutta taso B (19.7.) lisasi
  // VEGETATIVE/HARVEST-listat KAIKILLE aloitustavoille (STEP_METHOD_ANY) ->
  // CUSTOM on ainoa varmasti listaton tyyppi. Pakotetaan aktiivinen vaihe
  // siihen, jotta ACK hylkaytyy "no active step" -syylla.
  device_setState(DEVICE_IDLE);
  input_routeIntent(INTENT_START_GROWING, 2, INTENT_SOURCE_WIFI, &g_ctx);
  g_plant.phases[g_config.growPhase].type = GROW_PHASE_CUSTOM;  // listaton tyyppi
  IntentResult r = input_routeIntent(INTENT_ACK_STEP, 0, INTENT_SOURCE_BUTTON, &g_ctx);
  TEST_ASSERT_FALSE(r.accepted);
  TEST_ASSERT_EQUAL_STRING("no active step", r.rejectReason);
}

void test_set_step_restores_index_after_stray_ack() {
  // Turvaverkko (16.7.2026): harhapainalluksen kuittaama askel palautetaan
  // komennolla — myos sarjan lopusta (index == count), jolloin peruutusta
  // nimenomaan tarvitaan.
  start_seed_grow();
  g_config.growStepIndex = 4;   // "valmis" (index == count, 4 askelta)
  IntentResult r = input_routeIntent(INTENT_SET_STEP, 2, INTENT_SOURCE_WIFI, &g_ctx);
  TEST_ASSERT_TRUE(r.accepted);
  TEST_ASSERT_EQUAL_UINT8(2, g_config.growStepIndex);
  TEST_ASSERT_EQUAL_UINT32(g_stub_millis, g_state.growStepStartMs);
}

void test_set_step_rejects_out_of_range() {
  start_seed_grow();
  // stepCount == 4 (Fable §3.1) -> indeksi 4 on rajojen ulkopuolella.
  IntentResult r = input_routeIntent(INTENT_SET_STEP, 4, INTENT_SOURCE_WIFI, &g_ctx);
  TEST_ASSERT_FALSE(r.accepted);
  TEST_ASSERT_EQUAL_STRING("step out of range", r.rejectReason);
}

// ── PREV_STEP (peruutus fyysisella napilla, pitka painallus, K-C 22.7.2026) ─
// Askel taakse; askeleessa 0 taakse edelliseen vaiheeseen sen viimeiseen
// askeleeseen. Lattia = kasvatuksen aloitusvaihe (siemenpolulla SEEDLING).

void test_prev_step_decrements_within_phase() {
  start_seed_grow();                // SEEDLING (indeksi 1), stepIndex 0
  g_config.growStepIndex  = 2;
  g_state.growStepStartMs = 0;
  IntentResult r = input_routeIntent(INTENT_PREV_STEP, 0, INTENT_SOURCE_BUTTON, &g_ctx);
  TEST_ASSERT_TRUE(r.accepted);
  TEST_ASSERT_EQUAL_UINT8(1, g_config.growStepIndex);
  TEST_ASSERT_EQUAL_UINT8(1, g_config.growPhase);            // vaihe ennallaan
  TEST_ASSERT_EQUAL_UINT32(g_stub_millis, g_state.growStepStartMs);
}

void test_prev_step_from_seuranta_reenters_last_step() {
  // SEURANTA (index == count): peruutus palauttaa viimeiseen OHJE-askeleeseen.
  start_seed_grow();
  g_config.growStepIndex = 4;       // == count (basil-siemen: 4 askelta)
  IntentResult r = input_routeIntent(INTENT_PREV_STEP, 0, INTENT_SOURCE_BUTTON, &g_ctx);
  TEST_ASSERT_TRUE(r.accepted);
  TEST_ASSERT_EQUAL_UINT8(3, g_config.growStepIndex);        // viimeinen askel
}

void test_prev_step_at_first_step_goes_to_previous_phase_last_step() {
  // Askel 0 kasvuvaiheessa -> peruuta taimivaiheeseen, sen viimeiseen askeleeseen.
  start_seed_grow();
  input_routeIntent(INTENT_NEXT_PHASE, 0, INTENT_SOURCE_WIFI, &g_ctx);  // -> VEGETATIVE (2)
  TEST_ASSERT_EQUAL_UINT8(2, g_config.growPhase);
  TEST_ASSERT_EQUAL_UINT8(0, g_config.growStepIndex);
  g_config.growElapsedDays = 5;
  IntentResult r = input_routeIntent(INTENT_PREV_STEP, 0, INTENT_SOURCE_BUTTON, &g_ctx);
  TEST_ASSERT_TRUE(r.accepted);
  TEST_ASSERT_EQUAL_UINT8(1, g_config.growPhase);            // takaisin SEEDLING
  TEST_ASSERT_EQUAL_UINT8(3, g_config.growStepIndex);        // SEEDLINGin viimeinen (4-1)
  TEST_ASSERT_EQUAL_UINT16(0, g_config.growElapsedDays);     // vaihepaiva nollaantuu
  TEST_ASSERT_EQUAL_UINT32(g_stub_millis, g_state.growDayStartMs);
}

void test_prev_step_at_start_phase_first_step_rejected() {
  // Aloitusvaiheen (siemen: SEEDLING) askel 0: ei peruutusta ROOTINGiin alle.
  start_seed_grow();                // SEEDLING = aloitusvaihe, stepIndex 0
  IntentResult r = input_routeIntent(INTENT_PREV_STEP, 0, INTENT_SOURCE_BUTTON, &g_ctx);
  TEST_ASSERT_FALSE(r.accepted);
  TEST_ASSERT_EQUAL_STRING("already at start", r.rejectReason);
  TEST_ASSERT_EQUAL_UINT8(1, g_config.growPhase);            // ei muutu
  TEST_ASSERT_EQUAL_UINT8(0, g_config.growStepIndex);
}

void test_prev_step_rejected_when_idle() {
  device_setState(DEVICE_IDLE);
  IntentResult r = input_routeIntent(INTENT_PREV_STEP, 0, INTENT_SOURCE_BUTTON, &g_ctx);
  TEST_ASSERT_FALSE(r.accepted);
  TEST_ASSERT_EQUAL_STRING("wrong state", r.rejectReason);
}

// ── SET_PHASE / SET_DAY (huolto/katselmointi) ─────────────────────
// Suora tilan asetus flashausten valiin: hyppaa vaiheeseen/paivaan ilman etta
// kelloa odotetaan. Peilaa NEXT_PHASE:n rajoja mutta ottaa kohdearvon.

void test_set_phase_jumps_to_arbitrary_index_when_growing() {
  start_seed_grow();                 // method=seed -> SEEDLING (indeksi 1)
  g_config.growElapsedDays = 5;      // roikkuva paiva edellisesta vaiheesta
  g_config.growStepIndex   = 2;      // roikkuva askel
  g_state.growPhasePendingAdvance = true;
  IntentResult r = input_routeIntent(INTENT_SET_PHASE, 2, INTENT_SOURCE_WIFI, &g_ctx);
  TEST_ASSERT_TRUE(r.accepted);
  TEST_ASSERT_EQUAL_UINT8(2, g_config.growPhase);
  TEST_ASSERT_EQUAL_UINT16(0, g_config.growElapsedDays);   // vaihepaiva nollaantuu
  TEST_ASSERT_EQUAL_UINT8(0, g_config.growStepIndex);      // askelprogressi nollaantuu
  TEST_ASSERT_FALSE(g_state.growPhasePendingAdvance);
  TEST_ASSERT_EQUAL_UINT32(g_stub_millis, g_state.growDayStartMs);
}

void test_set_phase_jumps_backward() {
  start_seed_grow();
  g_config.growPhase = 2;
  IntentResult r = input_routeIntent(INTENT_SET_PHASE, 0, INTENT_SOURCE_WIFI, &g_ctx);
  TEST_ASSERT_TRUE(r.accepted);
  TEST_ASSERT_EQUAL_UINT8(0, g_config.growPhase);
}

void test_set_phase_rejects_out_of_range() {
  start_seed_grow();
  // phaseCount == 3 -> indeksi 3 on rajojen ulkopuolella
  IntentResult r = input_routeIntent(INTENT_SET_PHASE, 3, INTENT_SOURCE_WIFI, &g_ctx);
  TEST_ASSERT_FALSE(r.accepted);
  TEST_ASSERT_EQUAL_STRING("phase out of range", r.rejectReason);
}

void test_set_phase_rejected_when_idle() {
  device_setState(DEVICE_IDLE);
  IntentResult r = input_routeIntent(INTENT_SET_PHASE, 1, INTENT_SOURCE_WIFI, &g_ctx);
  TEST_ASSERT_FALSE(r.accepted);
  TEST_ASSERT_EQUAL_STRING("wrong state", r.rejectReason);
}

void test_set_day_sets_elapsed_and_reanchors() {
  start_seed_grow();
  g_state.growPhasePendingAdvance = true;
  IntentResult r = input_routeIntent(INTENT_SET_DAY, 12, INTENT_SOURCE_WIFI, &g_ctx);
  TEST_ASSERT_TRUE(r.accepted);
  TEST_ASSERT_EQUAL_UINT16(12, g_config.growElapsedDays);
  TEST_ASSERT_EQUAL_UINT32(g_stub_millis, g_state.growDayStartMs);  // ei heti-tikkia
  TEST_ASSERT_FALSE(g_state.growPhasePendingAdvance);  // scheduler arvioi uudelleen
}

void test_set_day_does_not_touch_step_index() {
  start_seed_grow();
  g_config.growStepIndex = 1;   // paiva ja askel ovat eri akseleita
  input_routeIntent(INTENT_SET_DAY, 3, INTENT_SOURCE_WIFI, &g_ctx);
  TEST_ASSERT_EQUAL_UINT8(1, g_config.growStepIndex);
}

void test_set_day_rejected_when_idle() {
  device_setState(DEVICE_IDLE);
  IntentResult r = input_routeIntent(INTENT_SET_DAY, 5, INTENT_SOURCE_WIFI, &g_ctx);
  TEST_ASSERT_FALSE(r.accepted);
  TEST_ASSERT_EQUAL_STRING("wrong state", r.rejectReason);
}

// ── Unknown intent ──────────────────────────────────────────────

void test_unknown_intent_rejected() {
  device_setState(DEVICE_IDLE);
  IntentResult r = input_routeIntent((Intent)99, 0, INTENT_SOURCE_BUTTON, &g_ctx);
  TEST_ASSERT_FALSE(r.accepted);
  TEST_ASSERT_EQUAL_STRING("unknown intent", r.rejectReason);
}

// ── Intent helpers ──────────────────────────────────────────────

void test_find_phase_index_by_type_returns_match() {
  int idx = input_findPhaseIndexByType(&g_plant, GROW_PHASE_SEEDLING);
  TEST_ASSERT_EQUAL_INT(1, idx);
}

void test_find_phase_index_by_type_returns_negative_on_miss() {
  int idx = input_findPhaseIndexByType(&g_plant, GROW_PHASE_HARVEST);
  TEST_ASSERT_LESS_THAN(0, idx);
}

void test_pick_start_phase_falls_back_to_zero_if_missing() {
  // Plant without SEEDLING phase → seed-start (method 1) falls back to first
  memset(&g_plant, 0, sizeof(g_plant));
  g_plant.phaseCount = 2;
  g_plant.phases[0].type = GROW_PHASE_ROOTING;
  g_plant.phases[1].type = GROW_PHASE_VEGETATIVE;
  uint8_t idx = input_pickStartPhase(&g_plant, 1);  // method=seed
  TEST_ASSERT_EQUAL_UINT8(0, idx);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_routes_null_context_returns_rejected);
  RUN_TEST(test_routes_null_pointer_in_context_rejected);

  RUN_TEST(test_start_growing_accepts_from_idle_with_cutting);
  RUN_TEST(test_start_growing_seed_picks_seedling_phase);
  RUN_TEST(test_start_growing_seedling_picks_vegetative_phase);
  RUN_TEST(test_start_growing_rejected_when_growing);
  RUN_TEST(test_start_growing_rejected_when_fault);
  RUN_TEST(test_start_growing_rejected_when_invalid_value);
  RUN_TEST(test_start_growing_rejected_when_plant_has_no_phases);

  RUN_TEST(test_stop_growing_accepted_from_growing);
  RUN_TEST(test_stop_growing_rejected_from_idle);
  RUN_TEST(test_stop_growing_accepted_from_idle_when_grow_active);

  RUN_TEST(test_next_phase_advances_when_growing);
  RUN_TEST(test_next_phase_rejected_when_idle);
  RUN_TEST(test_next_phase_rejected_at_last_phase);

  RUN_TEST(test_delay_phase_accepted_when_pending);
  RUN_TEST(test_delay_phase_rejected_when_no_pending);
  RUN_TEST(test_delay_phase_rejected_from_idle);

  RUN_TEST(test_ack_fault_clears_faults_when_in_fault_state);
  RUN_TEST(test_ack_fault_in_maintenance_returns_to_maintenance);
  RUN_TEST(test_ack_fault_rejected_when_no_fault);
  RUN_TEST(test_ack_fault_clears_ebb_latch_even_in_idle);
  RUN_TEST(test_ack_fault_clears_both_device_fault_and_ebb_latch);

  RUN_TEST(test_maintenance_enter_from_idle);
  RUN_TEST(test_maintenance_enter_from_growing_keeps_program_active);
  RUN_TEST(test_maintenance_enter_rejected_in_fault);
  RUN_TEST(test_maintenance_exit_resumes_growing_when_program_active);
  RUN_TEST(test_maintenance_exit_returns_to_idle_without_program);
  RUN_TEST(test_maintenance_exit_rejected_when_not_in_maintenance);
  RUN_TEST(test_maintenance_enter_is_idempotent_from_maintenance);

  RUN_TEST(test_shutdown_always_accepted);
  RUN_TEST(test_shutdown_accepted_from_fault);

  RUN_TEST(test_start_growing_resets_step_progress);
  RUN_TEST(test_next_phase_resets_step_progress);
  RUN_TEST(test_ack_step_rejected_on_timer_step);
  RUN_TEST(test_skip_step_advances_even_timer_step);
  RUN_TEST(test_ack_step_advances_button_step_and_ends_sequence);
  RUN_TEST(test_ack_step_rejected_outside_growing);
  RUN_TEST(test_ack_step_rejected_when_no_step_list);
  RUN_TEST(test_set_step_restores_index_after_stray_ack);
  RUN_TEST(test_set_step_rejects_out_of_range);

  RUN_TEST(test_prev_step_decrements_within_phase);
  RUN_TEST(test_prev_step_from_seuranta_reenters_last_step);
  RUN_TEST(test_prev_step_at_first_step_goes_to_previous_phase_last_step);
  RUN_TEST(test_prev_step_at_start_phase_first_step_rejected);
  RUN_TEST(test_prev_step_rejected_when_idle);

  RUN_TEST(test_set_phase_jumps_to_arbitrary_index_when_growing);
  RUN_TEST(test_set_phase_jumps_backward);
  RUN_TEST(test_set_phase_rejects_out_of_range);
  RUN_TEST(test_set_phase_rejected_when_idle);
  RUN_TEST(test_set_day_sets_elapsed_and_reanchors);
  RUN_TEST(test_set_day_does_not_touch_step_index);
  RUN_TEST(test_set_day_rejected_when_idle);

  RUN_TEST(test_unknown_intent_rejected);

  RUN_TEST(test_find_phase_index_by_type_returns_match);
  RUN_TEST(test_find_phase_index_by_type_returns_negative_on_miss);
  RUN_TEST(test_pick_start_phase_falls_back_to_zero_if_missing);
  return UNITY_END();
}
