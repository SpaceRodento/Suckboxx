/*=====================================================================
  button_intent_map.h - Context mapping: (gesture, state) -> Intent

  Layer 2 of the button architecture (docs/kehitys/nappi-arkkitehtuuri-
  suunnitelma.md). Pure, side-effect-free: given a raw button gesture,
  the current DeviceState and the live DeviceConfig, decide which Intent
  the button means *right now*. The Intent is then handed to
  input_routeIntent() (layer 3) which owns all state validation.

  Why a separate pure layer: the meaning of the single physical button
  grows over time (calibrations, grow starts, per-state actions). Keeping
  the gesture->meaning table here -- not inlined in plantmeister.ino --
  makes future additions a table edit with a unit test, never a rewrite
  of the wiring lambda. Gesture detection (button_input.h) stays
  state-blind; routing (input_router.h) stays unchanged.

  No module-level globals. Native tests call button_resolveIntent()
  directly with stub structs.
=====================================================================*/

#ifndef BUTTON_INTENT_MAP_H
#define BUTTON_INTENT_MAP_H

#include "config.h"
#include "structs.h"
#include "device_state.h"
#include "button_input.h"
#include "input_router.h"
#include "grow_step_fsm.h"   // askelkonteksti: onko sarja aktiivinen juuri nyt

#if ENABLE_INPUT_ROUTER

// Resolve the configurable short-press meaning from DeviceConfig.
// Mirrors the portal dropdown (BUTTON_ACTION_*). Kept separate so the
// per-state table below can override it later for specific states.
static inline Intent button_resolveShortAction(const DeviceConfig* config) {
  uint8_t action = config ? config->buttonAction : BUTTON_ACTION_NONE;
  switch (action) {
    case BUTTON_ACTION_EBB_FLOOD: return INTENT_EBB_FLOOD_NOW;
    case BUTTON_ACTION_SHUTDOWN:  return INTENT_SHUTDOWN;
    case BUTTON_ACTION_REBOOT:    return INTENT_REBOOT;
    case BUTTON_ACTION_NONE:
    default:                      return INTENT_NONE;
  }
}

// Pure: map a gesture + device state + config + active plant to an Intent.
//
// K-A behavior (docs/kehitys/nappi-vuorovaikutus-kartoitus.md §8.1, locked
// 14.7.2026):
//   LONG_PRESS  -> BACK-navigation, ALWAYS (K-C delta, 22.7.2026, user
//                  decision; D14 2a, 9.8.2026, removed the legacy config
//                  opt-in that could change this). In DEVICE_GROWING it emits
//                  INTENT_PREV_STEP: step the guided sequence backwards (prev
//                  step, or the previous phase's last step when at step 0,
//                  floored at the grow's start phase). It does not enter
//                  maintenance -- that is a phone/CLI action now (pm.py
//                  maintenance on|off, MAINTENANCE_ON/OFF). Reclaiming the
//                  gesture for "undo a mis-press" fits what the operator
//                  reaches for during a walkthrough; entering a safety lock
//                  suits a deliberate, non-physical channel (the phone)
//                  better than an easy-to-trigger hold. Shutdown was already
//                  off the button (K-A, 14.7.2026 -- external power cut, not
//                  a gesture). MAINTENANCE_EXIT always wins when already in
//                  maintenance -- the lock must never become un-exitable from
//                  the device even if the phone is unavailable.
//   SHORT_PRESS -> per-state row when the state has a locked Intent:
//                    DEVICE_IDLE  -> INTENT_START_GROWING
//                    DEVICE_FAULT -> INTENT_ACK_FAULT
//                  DEVICE_GROWING with an ACTIVE STEP -> INTENT_ACK_STEP,
//                  ALWAYS, with no buttonAction fallback: the configurable
//                  action can be "flood now", which must not fire while the
//                  rockwool is soaking in a bucket. Layer 3 still rejects
//                  the ack on TIMER steps (a stray press cannot shorten a
//                  soak).
//                  DEVICE_GROWING with a COMPLETED step sequence ->
//                  INTENT_NEXT_PHASE (3.8.2026): the guidance has been walked
//                  through, so the press confirms the phase transition even if
//                  the phase calendar still has days left. Before this the
//                  gesture was silently inert for the rest of the phase.
//                  Every other state falls back to the configurable action
//                  (g_config.buttonAction) -- unchanged v1 behavior.
//
// Layer 3 (input_routeIntent) still validates every transition -- this
// layer only decides *which* Intent the gesture means right now.
inline Intent button_resolveIntent(ButtonEvent gesture,
                                   DeviceState state,
                                   const DeviceConfig* config,
                                   const PlantConfig* plant) {
#if ENABLE_GUIDED_GROWING
  // Askelsarja on "aktiivinen" napin kannalta vain DEVICE_GROWING:ssa —
  // FAULT/MAINTENANCE-tilojen napin merkitys ei saa muuttua askelten takia.
  bool stepActive = (state == DEVICE_GROWING) && config &&
                    growstep_sequenceActive(plant, config);
  // Opastus kuitattu loppuun, vaihe viela kesken kalenterin mukaan. Tama on
  // eri asia kuin "ei askelia lainkaan" — ks. growstep_sequenceComplete().
  bool stepDone = (state == DEVICE_GROWING) && config &&
                  growstep_sequenceComplete(plant, config);
#else
  bool stepActive = false;
  bool stepDone   = false;
  (void)plant;
#endif

  switch (gesture) {
    case BUTTON_EVENT_LONG_PRESS:
      // Exit maintenance always wins — the lock must never become un-exitable
      // from the device even if the phone is unavailable.
      if (state == DEVICE_MAINTENANCE) return INTENT_MAINTENANCE_EXIT;
      // Step/phase back-navigation while growing (layer 3 = INTENT_PREV_STEP).
      // Inert elsewhere — maintenance is entered from the phone only
      // (pm.py maintenance on), never the button (D14 2a removed the config
      // opt-in that used to allow it).
      if (state == DEVICE_GROWING) return INTENT_PREV_STEP;
      return INTENT_NONE;

    case BUTTON_EVENT_SHORT_PRESS:
      switch (state) {
        case DEVICE_IDLE:  return INTENT_START_GROWING;
        case DEVICE_FAULT: return INTENT_ACK_FAULT;
        // Deliberately inert during service: the configurable short action can
        // be "flood now", which is the one thing that must not happen with a
        // hand in the reservoir. Exit is the long press.
        case DEVICE_MAINTENANCE: return INTENT_NONE;
        default:
#if ENABLE_GUIDED_GROWING
          // Demo-tila (growDemoMode, boot-cleared): yksi ele vie AINA
          // eteenpain koko opastusputken lapi ilman kelloa. Askel aktiivinen
          // -> SKIP (etenee myos TIMER-askeleen, ei odota 30 min liotusta);
          // askeleet loppu -> NEXT_PHASE (seuraavan vaiheen askeleet alusta).
          // Vain GROWING:ssa; layer 3 hylkaa NEXT_PHASE:n viimeisessa vaiheessa.
          if (config && config->growDemoMode && state == DEVICE_GROWING) {
            return stepActive ? INTENT_SKIP_STEP : INTENT_NEXT_PHASE;
          }
#endif
          if (stepActive) return INTENT_ACK_STEP;
#if ENABLE_GUIDED_GROWING
          // Opastus lapikayty -> lyhyt painallus vahvistaa vaihesiirron.
          //
          // Ilman tata nappi kuolee kesken kasvatuksen: kun vaiheen viimeinen
          // askel on kuitattu mutta vaiheen kalenteripaivat ovat viela kesken,
          // ele ei tuota mitaan intentia (todettu raudalla 3.8.2026, taimivaihe
          // 11/14: "Button: gesture 1 (no action)"). Samaan aikaan e-ink kysyy
          // vaiheohjeessa "Tarkista juuret - valmis siirtoon?" — laite siis
          // pyytaa vastausta jota mikaan ele ei voinut antaa. Kasvi, ei
          // kalenteri, ratkaisee milloin vaihe on ohi (plant empowerment), joten
          // kayttajan on paastava eteenpain etuajassa.
          //
          // Rajaus on tahallisen tiukka: vain kun askellista OLI olemassa ja se
          // on kuitattu loppuun. Kasvit joilla ei ole askelia sailyttavat vanhan
          // buttonAction-fallbackin muuttumattomana. Peruutus on pitka painallus
          // (INTENT_PREV_STEP palaa edellisen vaiheen viimeiseen askeleeseen),
          // ja layer 3 hylkaa siirron viimeisessa vaiheessa.
          if (stepDone) return INTENT_NEXT_PHASE;
#endif
          return button_resolveShortAction(config);
      }

    case BUTTON_EVENT_NONE:
    default:
      return INTENT_NONE;
  }
}

#endif // ENABLE_INPUT_ROUTER
#endif // BUTTON_INTENT_MAP_H
