/*=====================================================================
  grow_step_fsm.h - Askelmoottorin puhdas logiikka (natively testable)

  Data asuu grow_steps.h:ssa (tekstit + ajastimet + kuittaustapa); tama
  moduuli paattaa mita datasta seuraa: mika askel on aktiivinen, mika
  teksti naytetaan, eteneeko ajastin itsestaan, kelpaako kuittaus.
  LittleFS-persistenssi on grow_clock.h:ssa (askelkello kulkee samaa
  reittia kuin paiva-/valokello), intent-kasittely input_router.h:ssa
  ja automaattinen eteneminen scheduler.h:ssa. WIZARD: grow-steps.

  ── KRIITTINEN: vaiheen TYYPPI resolvoidaan tassa, EI indeksilla ─────
  cfg->growPhase on INDEKSI plant->phases[]-taulukkoon (structs.h),
  mutta askellistat avaimennetaan vaiheen TYYPILLA (GROW_PHASE_*).
  Basilikalla indeksi ja tyyppi sattuvat samoiksi, koska plant_lookup.h
  lisaa vaiheet enum-jarjestyksessa — joten indeksin vertaaminen
  tyyppiin lapaisisi JOKAISEN testin ja hajoaisi hiljaa vasta kun
  plants.json jarjestaa vaiheet vapaasti (plant_database.h sallii sen).
  growstep_stepsForConfig() on ainoa paikka joka tekee muunnoksen:
  phases[growPhase].type. Ala ohita sita vertaamalla indeksia suoraan.

  ── Kuka omistaa minka tilan ─────────────────────────────────────────
    cfg->growStepIndex        paatos ("missa askeleessa ollaan")
                              -> config.json, kuten growPhase
    state->growStepStartMs    millis()-ankkuri kuluneelle ajalle
                              -> growclock.json elapsed+epookkitagi
  Indeksia EI persistoida growclockiin totuutena: growclockin sopimus
  on "tiedosto saa havita" — havinnyt tiedosto ei saa kaskea liottamaan
  jo kylvettya kivivillaa. Tagi vastaa vain "onko elapsed viela minun".
=====================================================================*/

#ifndef GROW_STEP_FSM_H
#define GROW_STEP_FSM_H

#include "config.h"
#include "structs.h"
#include "grow_steps.h"

#include <stdint.h>

// ── Puhtaat apufunktiot ────────────────────────────────────────────

static inline bool growstep_timerElapsed(uint32_t elapsedMs, uint32_t timerMin) {
  return timerMin > 0 && elapsedMs >= timerMin * 60000UL;
}

// TIMER-askel etenee itsestaan kun kello tayttyy. BUTTON-askeleella
// timerMin on vain teksti-/vilkkukynnys, ei portti — ei auto-etenemista.
static inline bool growstep_autoAdvanceDue(const GrowStep* s, uint32_t elapsedMs) {
  return s && s->advanceOn == STEP_ADVANCE_TIMER &&
         growstep_timerElapsed(elapsedMs, s->timerMin);
}

// Kuittaus (lyhyt painallus / GROW_STEP_ACK) etenee vain BUTTON-askeleen.
// TIMER-askeleella kuittaus hylataan: harhapainallus liotuksen aikana ei
// saa lyhentaa liotusta — tahallinen ohitus on erikseen (SKIP).
static inline bool growstep_ackAllowed(const GrowStep* s) {
  return s && s->advanceOn == STEP_ADVANCE_BUTTON;
}

// Vilkkuvihje (kayttajan valinta 16.7.2026): valoa vilkutetaan vain kun
// on jotain tehtavaa — BUTTON-askel jolla ei ole ajastinta, tai jonka
// ajastin on tayttynyt. Idatys ei siis vilku paivina 0-5.
static inline bool growstep_awaitingUser(const GrowStep* s, uint32_t elapsedMs) {
  if (!s || s->advanceOn != STEP_ADVANCE_BUTTON) return false;
  return s->timerMin == 0 || growstep_timerElapsed(elapsedMs, s->timerMin);
}

// Nayttoteksti nyt: textLate korvaa alkutekstin kun kynnys tayttyy.
static inline const char* growstep_text(const GrowStep* s, uint32_t elapsedMs) {
  if (!s) return "";
  if (s->textLate && growstep_timerElapsed(elapsedMs, s->timerMin)) return s->textLate;
  return s->text ? s->text : "";
}

// TIMER-askeleen jaljella olevat sekunnit (laskee alas, pohja 0).
// BUTTON-askeleelle -1: sen kello ei laske alas vaan ylos (elapsed).
static inline int32_t growstep_remainingSec(const GrowStep* s, uint32_t elapsedMs) {
  if (!s || s->advanceOn != STEP_ADVANCE_TIMER || s->timerMin == 0) return -1;
  uint32_t totalSec   = s->timerMin * 60UL;
  uint32_t elapsedSec = elapsedMs / 1000UL;
  return (elapsedSec >= totalSec) ? 0 : (int32_t)(totalSec - elapsedSec);
}

// ── Konfiguraatio -> aktiivinen askellista ─────────────────────────

// Ainoa tyyppimuunnospiste (ks. otsikko). NULL kun kasvatus ei ole
// kaynnissa, vaihe on rajojen ulkopuolella tai listaa ei ole.
static inline const GrowStep* growstep_stepsForConfig(const PlantConfig* plant,
                                                      const DeviceConfig* cfg,
                                                      uint8_t* countOut) {
  if (countOut) *countOut = 0;
  if (!plant || !cfg || !cfg->growActive) return NULL;
  if (cfg->growPhase >= plant->phaseCount) return NULL;
  GrowPhaseType type = plant->phases[cfg->growPhase].type;
  return grow_stepsFor(plant->id, (uint8_t)type, cfg->growStartMethod, countOut);
}

// Onko askelsarja aktiivinen juuri nyt (lista olemassa JA indeksi sen
// sisalla). indeksi >= count on sarjan normaali paatepiste, ei virhe:
// e-ink palaa mittarikortteihin ja vaihe jatkaa omalla ohjeellaan.
static inline bool growstep_sequenceActive(const PlantConfig* plant,
                                           const DeviceConfig* cfg) {
  uint8_t count = 0;
  const GrowStep* steps = growstep_stepsForConfig(plant, cfg, &count);
  return steps != NULL && cfg->growStepIndex < count;
}

// Onko vaiheen askelsarja kaytu loppuun: lista oli olemassa JA kaikki sen
// askeleet on kuitattu. Ero sequenceActive():n kieltoon on olennainen napin
// kannalta, koska "ei aktiivista askelta" on kaksi eri tilannetta:
//   1. talle kasville/vaiheelle ei ole askelia lainkaan  -> count == 0
//   2. kayttaja on kuitannut opastuksen loppuun           -> index >= count
// Vain jalkimmaisessa kayttaja on suorittanut vaiheen ohjeet, ja vain siina
// nappi voi tarkoittaa "olen valmis, siirry seuraavaan vaiheeseen".
static inline bool growstep_sequenceComplete(const PlantConfig* plant,
                                             const DeviceConfig* cfg) {
  uint8_t count = 0;
  const GrowStep* steps = growstep_stepsForConfig(plant, cfg, &count);
  return steps != NULL && count > 0 && cfg->growStepIndex >= count;
}

// ── Nakyma (API + UX kayttavat samaa) ──────────────────────────────

struct GrowStepView {
  bool        active;        // askel nakyvissa (false -> muut kentat eivat pade)
  uint8_t     index;         // 0-pohjainen
  uint8_t     count;
  const char* text;          // resolvoitu (textLate kynnyksen jalkeen)
  bool        late;          // textLate kaytossa
  bool        ackAllowed;    // BUTTON-askel: lyhyt painallus etenee
  bool        awaitingUser;  // vilkkuvihje (ks. growstep_awaitingUser)
  uint32_t    timerMin;
  uint32_t    elapsedSec;    // kulunut nykyisessa askeleessa
  int32_t     remainingSec;  // TIMER: laskee alas; -1 = ei alaslaskuria
};

static inline bool growstep_buildView(const PlantConfig* plant,
                                      const DeviceConfig* cfg,
                                      uint32_t elapsedMs,
                                      GrowStepView* out) {
  if (!out) return false;
  out->active = false;
  uint8_t count = 0;
  const GrowStep* steps = growstep_stepsForConfig(plant, cfg, &count);
  if (!steps || cfg->growStepIndex >= count) return false;

  const GrowStep* s = &steps[cfg->growStepIndex];
  out->active       = true;
  out->index        = cfg->growStepIndex;
  out->count        = count;
  out->text         = growstep_text(s, elapsedMs);
  out->late         = (out->text == s->textLate) && s->textLate != NULL;
  out->ackAllowed   = growstep_ackAllowed(s);
  out->awaitingUser = growstep_awaitingUser(s, elapsedMs);
  out->timerMin     = s->timerMin;
  out->elapsedSec   = elapsedMs / 1000UL;
  out->remainingSec = growstep_remainingSec(s, elapsedMs);
  return true;
}

// ── Mutaatiot (kutsuja hoitaa config_save + lokituksen) ────────────

// Nollaa askeleet listan alkuun. Kutsuttava AINA kun indeksin alla oleva
// lista voi vaihtua (ks. structs.h growStepIndex). Callers counted 10.8.2026:
// NINE, not the five this comment used to claim - command_handler.h x3,
// input_router.h x3, wifi_portal_routes_post.h x2, scheduler.h x1. The
// compiler does not police this list and it has already drifted once, which
// is the whole argument for M0.1: grow_transition.h becomes the single write
// point and the count stops mattering. Do not hand-maintain the list here.
static inline void grow_resetStepProgress(SystemState* state, DeviceConfig* cfg,
                                          unsigned long nowMs) {
  cfg->growStepIndex = 0;
  if (state) state->growStepStartMs = nowMs;
}

// Siirry seuraavaan askeleeseen ja nollaa askelkello. Indeksin annetaan
// kasvaa count:iin asti — se on "sarja valmis", jonka sequenceActive ja
// buildView tulkitsevat inertiksi (puolustus syvyydessa: vaikka lista
// lyhenisi alta, out-of-bounds-lukua ei koskaan tehda).
static inline void growstep_advance(SystemState* state, DeviceConfig* cfg,
                                    unsigned long nowMs) {
  cfg->growStepIndex++;
  if (state) state->growStepStartMs = nowMs;
}

#endif // GROW_STEP_FSM_H
