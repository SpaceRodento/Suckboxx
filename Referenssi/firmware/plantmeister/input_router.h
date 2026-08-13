/*=====================================================================
  input_router.h - Intent-based command routing (header-only)

  Intents are state-changing user decisions. Channels (button, serial,
  LoRa, WiFi) build Intent objects and pass them here together with
  a RouterContext that carries pointers to the live runtime state.

  Design rule: NO module-level globals here. The caller owns the
  context. This keeps the header safe to include from multiple
  translation units (including native tests).
=====================================================================*/

#ifndef INPUT_ROUTER_H
#define INPUT_ROUTER_H

#include "config.h"
#include "structs.h"
#include "device_state.h"
#include "reboot_request.h"
#include "grow_step_fsm.h"   // askel-intentit: ACK/SKIP/SET (WIZARD: grow-steps)

#if ENABLE_INPUT_ROUTER

// Persist updated device config (implemented in config_store.h).
bool config_save(const DeviceConfig* cfg);

#if ENABLE_WIFI_PORTAL
void portal_reactivate();
#endif

static inline int input_findPhaseIndexByType(const PlantConfig* plant, GrowPhaseType type) {
  if (!plant) return -1;
  for (uint8_t i = 0; i < plant->phaseCount; i++) {
    if (plant->phases[i].type == type) return i;
  }
  return -1;
}

static inline uint8_t input_pickStartPhase(const PlantConfig* plant, uint8_t startMethod) {
  if (!plant || plant->phaseCount == 0) return 0;

  GrowPhaseType preferredType = GROW_PHASE_ROOTING;
  switch (startMethod) {
    case 1: preferredType = GROW_PHASE_SEEDLING; break;
    case 2: preferredType = GROW_PHASE_VEGETATIVE; break;
    default: preferredType = GROW_PHASE_ROOTING; break;
  }

  int idx = input_findPhaseIndexByType(plant, preferredType);
  if (idx >= 0) return (uint8_t)idx;
  return 0;
}

enum Intent : uint8_t {
  INTENT_NONE          = 0,
  INTENT_START_GROWING = 1,
  INTENT_STOP_GROWING  = 2,
  INTENT_NEXT_PHASE    = 3,
  INTENT_DELAY_PHASE   = 4,
  INTENT_ACK_FAULT     = 5,
  INTENT_SHUTDOWN      = 6,
  INTENT_REBOOT        = 7,
  INTENT_REACTIVATE_AP = 8,
  INTENT_EBB_FLOOD_NOW = 9,
  INTENT_MAINTENANCE_ENTER = 10,
  INTENT_MAINTENANCE_EXIT  = 11,
  INTENT_ACK_STEP          = 12,  // kuittaa nykyinen askel (vain BUTTON-askel)
  INTENT_SKIP_STEP         = 13,  // ohita nykyinen askel (myos TIMER — tahallinen)
  INTENT_SET_STEP          = 14,  // aseta askelindeksi (value) — CLI/portaali-peruutus
  INTENT_SET_PHASE         = 15,  // aseta vaiheindeksi (value) — huolto/katselmointi
  INTENT_SET_DAY           = 16,  // aseta vaihepaiva (value) — huolto/katselmointi
  INTENT_PREV_STEP         = 17,  // peruuta: edellinen askel / edellisen vaiheen loppu
  INTENT_COUNT
};

enum IntentSource : uint8_t {
  INTENT_SOURCE_UNKNOWN  = 0,
  INTENT_SOURCE_BUTTON   = 1,
  INTENT_SOURCE_SERIAL   = 2,
  INTENT_SOURCE_LORA     = 3,
  INTENT_SOURCE_WIFI     = 4,
  INTENT_SOURCE_INTERNAL = 5,
};

// Caller fills this and passes it by const-pointer to input_routeIntent().
// All pointers must be non-null when the corresponding Intent is dispatched.
// Native tests construct a local context with stub structs.
struct RouterContext {
  SensorData*    sensors;
  SystemState*   state;
  DeviceConfig*  config;
  PlantConfig**  activePlant;
};

struct IntentResult {
  bool        accepted;
  const char* rejectReason;   // English, for logs; nullptr if accepted
};

inline const char* intent_sourceName(IntentSource s) {
  switch (s) {
    case INTENT_SOURCE_BUTTON:   return "button";
    case INTENT_SOURCE_SERIAL:   return "serial";
    case INTENT_SOURCE_LORA:     return "lora";
    case INTENT_SOURCE_WIFI:     return "wifi";
    case INTENT_SOURCE_INTERNAL: return "internal";
    default:                     return "unknown";
  }
}

inline const char* intent_name(Intent i) {
  switch (i) {
    case INTENT_NONE:           return "none";
    case INTENT_START_GROWING:  return "start_growing";
    case INTENT_STOP_GROWING:   return "stop_growing";
    case INTENT_NEXT_PHASE:     return "next_phase";
    case INTENT_DELAY_PHASE:    return "delay_phase";
    case INTENT_ACK_FAULT:      return "ack_fault";
    case INTENT_SHUTDOWN:       return "shutdown";
    case INTENT_REBOOT:         return "reboot";
    case INTENT_REACTIVATE_AP:  return "reactivate_ap";
    case INTENT_EBB_FLOOD_NOW:  return "ebb_flood_now";
    case INTENT_MAINTENANCE_ENTER: return "maintenance_enter";
    case INTENT_MAINTENANCE_EXIT:  return "maintenance_exit";
    case INTENT_ACK_STEP:       return "ack_step";
    case INTENT_SKIP_STEP:      return "skip_step";
    case INTENT_SET_STEP:       return "set_step";
    case INTENT_SET_PHASE:      return "set_phase";
    case INTENT_SET_DAY:        return "set_day";
    case INTENT_PREV_STEP:      return "prev_step";
    default:                    return "unknown";
  }
}

// Pure function: given an intent + value + source + context, decide
// what to do. May call device_setState() and may write through the
// context pointers. Returns whether the intent was honoured.
//
// CORE-D2 implementation: full intent semantics.
inline IntentResult input_routeIntent(Intent intent,
                                      uint8_t value,
                                      IntentSource source,
                                      const RouterContext* ctx);

// =====================================================================
// IMPLEMENTATION
// =====================================================================

inline IntentResult input_routeIntent(Intent intent,
                                      uint8_t value,
                                      IntentSource source,
                                      const RouterContext* ctx) {
  IntentResult r{ false, nullptr };

  if (!ctx || !ctx->sensors || !ctx->state || !ctx->config || !ctx->activePlant) {
    r.rejectReason = "context invalid";
    return r;
  }

  SystemState* state = ctx->state;
  DeviceConfig* config = ctx->config;
  PlantConfig* activePlant = *ctx->activePlant;

  DEBUG_PRINTF("[INFO]  Intent: %u from %s\n",
               (unsigned)intent, intent_sourceName(source));

  switch (intent) {
    case INTENT_START_GROWING: {
      if (g_device.state != DEVICE_IDLE) {
        r.rejectReason = "wrong state";
        return r;
      }
      if (!activePlant || activePlant->phaseCount == 0 || value > 2) {
        if (activePlant && activePlant->phaseCount == 0) {
          // F3: explicit log — silent "context invalid" hid this common misconfiguration
          DEBUG_PRINTF("[WARN]  Intent START_GROWING: plant '%s' has no grow phases — "
                       "scheduled flooding will not run\n", activePlant->id);
          r.rejectReason = "no grow phases";
        } else {
          r.rejectReason = "context invalid";
        }
        return r;
      }

      uint8_t startMethod = value;
      config->growPhase       = input_pickStartPhase(activePlant, startMethod);
      config->growElapsedDays = 0;
      config->growActive      = true;
      config->growStartMethod = startMethod;
      state->growDayStartMs          = millis();
      state->growPhasePendingAdvance = false;
      // Uusi kasvatus -> askeleet listan alkuun (reset-paikka 1/5, grow_step_fsm.h)
      grow_resetStepProgress(state, config, millis());
      config_save(config);
      device_setState(DEVICE_GROWING);
      r.accepted = true;
      return r;
    }

    case INTENT_STOP_GROWING: {
      // A reboot restores config->growActive but NOT DEVICE_GROWING, so the
      // device can sit in IDLE with an orphaned "growing" flag the user cannot
      // clear. Allow STOP from IDLE in exactly that case so growActive can
      // always be turned off without first re-entering GROWING.
      bool orphanedGrow = (g_device.state == DEVICE_IDLE && config->growActive);
      if (g_device.state != DEVICE_GROWING && !orphanedGrow) {
        r.rejectReason = "wrong state";
        return r;
      }
      config->growActive             = false;
      state->growPhasePendingAdvance = false;
      config_save(config);
      device_setState(DEVICE_IDLE);
      r.accepted = true;
      return r;
    }

    case INTENT_NEXT_PHASE: {
      if (g_device.state != DEVICE_GROWING) {
        r.rejectReason = "wrong state";
        return r;
      }
      if (!config->growActive || !activePlant || activePlant->phaseCount == 0) {
        r.rejectReason = "context invalid";
        return r;
      }

      uint8_t next = config->growPhase + 1;
      if (next >= activePlant->phaseCount) {
        DEBUG_INFO(F("Grow: already at last phase"));
        r.rejectReason = "context invalid";
        return r;
      }
      config->growPhase       = next;
      config->growElapsedDays = 0;
      state->growPhasePendingAdvance = false;
      state->growDayStartMs = millis();
      // Vaihe vaihtui -> askellista sen alla vaihtui (reset-paikka 2/5)
      grow_resetStepProgress(state, config, millis());
      config_save(config);
      DEBUG_PRINTF("[INFO]  Grow: advanced to phase %d (%s)\n",
                   config->growPhase, activePlant->phases[config->growPhase].label);
      r.accepted = true;
      return r;
    }

    case INTENT_DELAY_PHASE:
      if (g_device.state != DEVICE_GROWING) {
        r.rejectReason = "wrong state";
        return r;
      }
      if (!state->growPhasePendingAdvance) {
        DEBUG_INFO(F("Grow: no pending phase transition to delay"));
        r.rejectReason = "context invalid";
        return r;
      }
      state->growAdvanceProposedMs = millis();
      DEBUG_INFO(F("Grow: phase transition postponed by user"));
      r.accepted = true;
      return r;

    case INTENT_ACK_FAULT: {
      // ACK clears a device-level FAULT (the lock) and/or a latched ebb&flow
      // FSM fault. The ebb fault is advisory at device level (§8) so the device
      // sits in IDLE, not DEVICE_FAULT — gating ack solely on DEVICE_FAULT made
      // a latched ebb fault unclearable without a reboot (catch-22 hit during
      // the 2026-06-10 soak hw test: an overflow latched the pump, the next
      // force-flood failed to start, and the scheduler latched the FSM as a
      // hard fault). Accept if either fault is present; clear whichever applies.
      bool ebbLatched = false;
#if ENABLE_EBB_FLOW
      ebbLatched = state->ebbFlowFaultLatched;
#endif
      if (g_device.state != DEVICE_FAULT && !ebbLatched) {
        r.rejectReason = "wrong state";
        return r;
      }
#if ENABLE_EBB_FLOW
      if (ebbLatched) {
        // Consumed by scheduler_updateEbbFlow(): the FSM clears its latch and
        // returns to a safe state (IDLE/DRAIN) on the next ebb tick.
        state->ebbFlowAckRequested = true;
      }
#endif
      if (g_device.state == DEVICE_FAULT) {
        // device_clearAllFaults() paattaa kohdetilan itse: oletus IDLE, mutta
        // huollon aikana syntynyt vika palauttaa MAINTENANCE:en (D13). Ala
        // pakota IDLE:a tassa — se vapauttaisi huoltolukot hiljaa.
        device_clearAllFaults();
      }
      r.accepted = true;
      return r;
    }

    case INTENT_SHUTDOWN:
      device_setState(DEVICE_SHUTDOWN);
      r.accepted = true;
      return r;

    case INTENT_REBOOT:
      // Do NOT enter DEVICE_SHUTDOWN — loop_dispatch routes that state to
      // esp_deep_sleep_start(), which would put the device into a zombie
      // state (no WiFi, no USB) before reboot_request_tick() runs.
      reboot_request_schedule(500);
      r.accepted = true;
      return r;

    case INTENT_REACTIVATE_AP:
#if ENABLE_WIFI_PORTAL
      portal_reactivate();
      r.accepted = true;
      return r;
#else
      r.rejectReason = "context invalid";
      return r;
#endif

    case INTENT_EBB_FLOOD_NOW:
#if ENABLE_EBB_FLOW
      // One-shot flag consumed by scheduler_updateEbbFlow(). The FSM only acts
      // on it from IDLE with water present, so it can never pump a dry reservoir.
      state->ebbFlowForceFloodRequested = true;
      r.accepted = true;
      return r;
#else
      r.rejectReason = "context invalid";
      return r;
#endif

    case INTENT_MAINTENANCE_ENTER:
      // Safety state, not a wizard: binary, no steps, never left automatically
      // (device_state.h § maintenance). Actuator locking is derived from the
      // state itself in loop(), so setting the state is the whole action here.
      if (!device_maintenanceEntryAllowed(g_device.state)) {
        r.rejectReason = "wrong state";
        return r;
      }
      device_setState(DEVICE_MAINTENANCE);
      DEBUG_INFO(F("Huoltotila: päällä — pumppu ja moottori lukittu"));
      r.accepted = true;
      return r;

    case INTENT_MAINTENANCE_EXIT:
      if (g_device.state != DEVICE_MAINTENANCE) {
        r.rejectReason = "wrong state";
        return r;
      }
      // growActive is the persistent truth for "a grow is running", so a grow
      // interrupted for servicing resumes rather than being orphaned in IDLE.
      device_setState(device_maintenanceResumeState(config->growActive));
      DEBUG_INFO(F("Huoltotila: pois — normaali toiminta jatkuu"));
      r.accepted = true;
      return r;

    // ── Askel-intentit (grow_steps.h + grow_step_fsm.h) WIZARD: grow-steps ──
    case INTENT_ACK_STEP:
    case INTENT_SKIP_STEP: {
#if ENABLE_GUIDED_GROWING
      if (g_device.state != DEVICE_GROWING) {
        r.rejectReason = "wrong state";
        return r;
      }
      uint8_t stepCount = 0;
      const GrowStep* steps = growstep_stepsForConfig(activePlant, config, &stepCount);
      if (!steps || config->growStepIndex >= stepCount) {
        r.rejectReason = "no active step";
        return r;
      }
      if (intent == INTENT_ACK_STEP &&
          !growstep_ackAllowed(&steps[config->growStepIndex])) {
        // TIMER-askel (liotus): harhapainallus ei saa lyhentaa sita. Tahallinen
        // ohitus on SKIP — pitka painallus (jos optio paalla) tai komento.
        r.rejectReason = "timer step (use skip)";
        return r;
      }
      growstep_advance(state, config, millis());
      config_save(config);
      DEBUG_PRINTF("[INFO]  Grow: step %s -> step %u/%u\n",
                   intent == INTENT_ACK_STEP ? "acked" : "skipped",
                   (unsigned)(config->growStepIndex + 1), (unsigned)stepCount);
      r.accepted = true;
      return r;
#else
      r.rejectReason = "context invalid";
      return r;
#endif
    }

    case INTENT_SET_STEP: {
#if ENABLE_GUIDED_GROWING
      // Peruutus/korjaus-komento (CLI/portaali): harhapainalluksen kuittaama
      // askel palautetaan asettamalla indeksi takaisin. Sallitaan myos kun
      // indeksi on jo listan lopussa (sarja "valmis") — juuri silloin
      // peruutusta tarvitaan. Askelkello alkaa alusta (epookkitagi hoitaa
      // persistenssin oikein).
      if (g_device.state != DEVICE_GROWING) {
        r.rejectReason = "wrong state";
        return r;
      }
      uint8_t stepCount = 0;
      const GrowStep* steps = growstep_stepsForConfig(activePlant, config, &stepCount);
      if (!steps) {
        r.rejectReason = "no step list";
        return r;
      }
      if (value >= stepCount) {
        r.rejectReason = "step out of range";
        return r;
      }
      config->growStepIndex  = value;
      state->growStepStartMs = millis();
      config_save(config);
      DEBUG_PRINTF("[INFO]  Grow: step set to %u/%u\n",
                   (unsigned)(value + 1), (unsigned)stepCount);
      r.accepted = true;
      return r;
#else
      r.rejectReason = "context invalid";
      return r;
#endif
    }

    // ── Peruutus fyysisella napilla (pitka painallus, K-C 22.7.2026) ─────
    // Harhapainalluksen tai liian aikaisin kuitatun askeleen palautus ILMAN
    // puhelinta. Askelakseli ensin: jos indeksi > 0, palaa edelliseen
    // askeleeseen (myos SEURANNASTA, jossa indeksi == count, palautuu
    // viimeiseen OHJE-askeleeseen). Jos ollaan jo ensimmaisessa askeleessa,
    // peruuta EDELLISEEN VAIHEESEEN ja laskeudu sen viimeiseen askeleeseen —
    // mutta ei kasvatuksen aloitusvaihetta alemmas (siemenpolulla ROOTING
    // olisi vaara reitti; input_pickStartPhase on lattia). Kaanteinen
    // NEXT_PHASE:lle, joten "eteen ja taakse" on symmetrinen.
    case INTENT_PREV_STEP: {
#if ENABLE_GUIDED_GROWING
      if (g_device.state != DEVICE_GROWING) {
        r.rejectReason = "wrong state";
        return r;
      }
      if (!config->growActive || !activePlant || activePlant->phaseCount == 0) {
        r.rejectReason = "context invalid";
        return r;
      }
      if (config->growStepIndex > 0) {
        config->growStepIndex--;
        state->growStepStartMs = millis();
        config_save(config);
        DEBUG_PRINTF("[INFO]  Grow: step back -> step %u\n",
                     (unsigned)(config->growStepIndex + 1));
        r.accepted = true;
        return r;
      }
      uint8_t startPhase = input_pickStartPhase(activePlant, config->growStartMethod);
      if (config->growPhase <= startPhase) {
        r.rejectReason = "already at start";
        return r;
      }
      config->growPhase--;
      config->growElapsedDays        = 0;
      state->growPhasePendingAdvance = false;
      state->growDayStartMs          = millis();
      // Laskeudu edellisen vaiheen VIIMEISEEN askeleeseen (type-resolvoituna,
      // ei indeksilla — grow_step_fsm.h otsikko). Jos vaiheella ei ole
      // askeleita, indeksi 0 (SEURANTA-tila suoraan).
      uint8_t prevCount = 0;
      growstep_stepsForConfig(activePlant, config, &prevCount);
      config->growStepIndex  = prevCount > 0 ? (uint8_t)(prevCount - 1) : 0;
      state->growStepStartMs = millis();
      config_save(config);
      DEBUG_PRINTF("[INFO]  Grow: back to phase %u (%s), step %u\n",
                   (unsigned)config->growPhase,
                   activePlant->phases[config->growPhase].label,
                   (unsigned)(config->growStepIndex + 1));
      r.accepted = true;
      return r;
#else
      r.rejectReason = "context invalid";
      return r;
#endif
    }

    // ── Huolto/katselmointi: hyppaa suoraan vaiheeseen/vaihepaivaan ──────
    // Kehitys-/validointikaytto: flashausten valissa menetetaan paivia, ja
    // laitteen tila pitaa voida asettaa kasin jotta nakee milta tietty vaihe
    // tai paiva nayttaa e-inkilla + portaalissa ilman etta kelloa odotetaan.
    // Vaikuttaa OIKEAAN kasvatukseen (config persistoituu). Vaatii GROWING-
    // tilan, kuten NEXT_PHASE — muuten "wrong state".
    case INTENT_SET_PHASE: {
#if ENABLE_GUIDED_GROWING
      // Peilaa NEXT_PHASE:a mutta ottaa mielivaltaisen kohdeindeksin (myos
      // taakse). Nollaa vaihepaivan, pending-ehdotuksen ja askelprogressin —
      // sama nollaus kuin kaikilla vaihesiirtymilla (grow_step_fsm.h, viides
      // reset-paikka: huoltoreitti).
      if (g_device.state != DEVICE_GROWING) {
        r.rejectReason = "wrong state";
        return r;
      }
      if (!config->growActive || !activePlant || activePlant->phaseCount == 0) {
        r.rejectReason = "context invalid";
        return r;
      }
      if (value >= activePlant->phaseCount) {
        r.rejectReason = "phase out of range";
        return r;
      }
      config->growPhase              = value;
      config->growElapsedDays        = 0;
      state->growPhasePendingAdvance = false;
      state->growDayStartMs          = millis();
      grow_resetStepProgress(state, config, millis());
      config_save(config);
      DEBUG_PRINTF("[INFO]  Grow: phase set to %u (%s)\n",
                   (unsigned)config->growPhase,
                   activePlant->phases[config->growPhase].label);
      r.accepted = true;
      return r;
#else
      r.rejectReason = "context invalid";
      return r;
#endif
    }

    case INTENT_SET_DAY: {
#if ENABLE_GUIDED_GROWING
      // Aseta vaihepaiva (growElapsedDays) suoraan. growDayStartMs = nyt, muuten
      // vanha ankkuri (now - start >= 24h) tikittaisi heti ylimaaraisen paivan
      // seuraavalla scheduler-tikilla. Pending nollataan, jotta scheduler
      // arvioi advance-ehdotuksen uudelleen uudella paivaluvulla (jos
      // durationDays tayttyy, ehdotus syntyy taas — juuri se mita halutaan
      // validoida). Askelprogressiin EI kosketa: paiva ja askel ovat eri
      // akseleita (askel on oma komentonsa SET_STEP).
      if (g_device.state != DEVICE_GROWING) {
        r.rejectReason = "wrong state";
        return r;
      }
      if (!config->growActive || !activePlant || activePlant->phaseCount == 0) {
        r.rejectReason = "context invalid";
        return r;
      }
      config->growElapsedDays        = value;
      state->growDayStartMs          = millis();
      state->growPhasePendingAdvance = false;
      config_save(config);
      DEBUG_PRINTF("[INFO]  Grow: phase day set to %u\n", (unsigned)value);
      r.accepted = true;
      return r;
#else
      r.rejectReason = "context invalid";
      return r;
#endif
    }

    default:
      r.rejectReason = "unknown intent";
      return r;
  }
}

#endif // ENABLE_INPUT_ROUTER
#endif // INPUT_ROUTER_H
