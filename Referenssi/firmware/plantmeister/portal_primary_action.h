/*=====================================================================
  portal_primary_action.h - Portaalin toimintopalkin resolveri

  Yksi totuus sille, mika on kayttajan ENSISIJAINEN toiminto juuri nyt.

  ── Miksi tama on olemassa ───────────────────────────────────────────
  Vanhassa portaalissa "seuraava toiminto" oli hajallaan kolmessa
  erilliselementissa (faultBanner, readyCard, ggPendingWrap) ja kahdessa
  paallekkaisessa kaynnistysnapissa — rautatestissa 16.7.2026 kayttaja
  ei tiennyt mita nappia painaa ("pitaa olla aivan ilmiselvaa etta
  painan ainoaa oikeaa nappia"). Sama periaate kuin laitteen yhden napin
  UX:ssa (architecture.md §5) ja e-inkin COACH-advisoryssa
  (resolve_advisory.h): tilakone paattaa, UI vain renderoi.

  Resolveri on laitepaassa eika selaimessa, jotta sama vastaus on
  tarjolla kaikille pinnoille (portaali, e-ink, CLI) ja prioriteetti-
  jarjestys on natiivitestattu (test_portal_primary_action).

  Prioriteettijarjestys (ylin voittaa) — docs/kehitys/
  web-ui-uudelleensuunnittelu.md §3.2:
    1. FAULT            -> tyhjennys on aina ensin
    2. askelkuittaus    -> kasvatusaskel odottaa kayttajaa (grow-steps)
    3. vaihesiirtyma    -> guided growing odottaa hyvaksyntaa
    4. aloita kasvatus  -> IDLE, kayttoonotto tehty, ei kasvatusta
    5. advisory         -> tieto degradaatiosta (ei nappia)
    6. ei mitaan        -> GROWING ja kaikki hyvin

  KORJATTU 17.7.2026 (rautatesti): advisory oli ennen prioriteetti 4, siis
  ABOVE "aloita kasvatus". Kayttajan laitteessa oli MLX90614-advisory
  (lehti-IR ei vastaa), joten palkki naytti anturivaroituksen ILMAN nappia
  ja aloitusnappi katosi kokonaan — samalla kun Koti-nakyma neuvoi "Aloita
  kasvatus ylapalkista". Ohje osoitti tyhjaan. Kayttaja: "En ymmarra mita
  tarkoittaa kun sanotaan Aloita kasvatus ylapalkista".

  Periaate joka tasta opittiin: ADVISORYLLA EI OLE NAPPIA, joten se ei saa
  koskaan syrjayttaa toimintoa jolla on. Advisory on konteksti, ei toiminto
  — se on palkin taytetta silloin kun tehtavaa ei ole. Puuttuva anturi on
  advisory nimenomaan siksi ETTEI se estaisi toimintaa (architecture.md §8
  Aukko B); olisi nurinkurista jos se sitten estaisi sen UI:ssa.

  Advisory nakyy silti aina: /api/status health-kentta ja palkin
  toissijainen rivi kertovat siita riippumatta siita mika toiminto voitti.

  Onboarding-tilassa palautetaan aina NONE: /setup-sivu omistaa sen
  polun, eika paasivun palkki saa kilpailla sen kanssa.

  Pure + header-only -> natiivitestattava ilman Arduino-runtimea.
=====================================================================*/

#ifndef PORTAL_PRIMARY_ACTION_H
#define PORTAL_PRIMARY_ACTION_H

#include <stdint.h>

enum PrimaryAction : uint8_t {
  PRIMARY_ACTION_NONE = 0,       // ei nappia (esim. GROWING, kaikki hyvin)
  PRIMARY_ACTION_CLEAR_FAULT,    // "Tyhjenna vika" -> CLEAR_FAULT
  PRIMARY_ACTION_STEP_ACK,       // "Tehty" -> GROW_STEP_ACK
  PRIMARY_ACTION_APPROVE_PHASE,  // "Hyvaksy siirtyma" -> /api/grow/next
  PRIMARY_ACTION_ADVISORY,       // advisory-teksti, ei nappia
  PRIMARY_ACTION_START_GROW,     // "Aloita kasvatus" -> /api/grow/start
};

struct PrimaryActionInput {
  bool fault;               // device_state == FAULT
  bool stepAckAllowed;      // kasvatusaskel nakyvissa ja kuitattavissa
  bool phasePendingAdvance; // guided growing odottaa siirtyman hyvaksyntaa
  bool advisoryActive;      // device_advisories != 0
  bool growActive;          // kasvatus kaynnissa
  bool onboarding;          // kayttoonotto kesken (/setup omistaa UX:n)
};

static inline PrimaryAction portal_resolvePrimaryAction(const PrimaryActionInput* in) {
  if (!in) return PRIMARY_ACTION_NONE;
  // Onboarding ensin: paasivu ei tarjoa toimintoja ennen kayttoonottoa —
  // etenkaan "Aloita kasvatus", koska pakolliset valinnat voivat puuttua.
  if (in->onboarding)          return PRIMARY_ACTION_NONE;
  if (in->fault)               return PRIMARY_ACTION_CLEAR_FAULT;
  if (in->stepAckAllowed)      return PRIMARY_ACTION_STEP_ACK;
  if (in->phasePendingAdvance) return PRIMARY_ACTION_APPROVE_PHASE;
  // Kaynnistys on kartoituksen §6 vaatima eksplisiittinen ele: palkki vain
  // TARJOAA sen — mikaan taalla ei kaynnista mitaan itse.
  //
  // Tama on ENNEN advisorya: advisorylla ei ole nappia, joten se ei saa
  // syrjayttaa toimintoa jolla on (ks. tiedoston otsikko — MLX90614-advisory
  // soi aloitusnapin 17.7.2026).
  if (!in->growActive)         return PRIMARY_ACTION_START_GROW;
  if (in->advisoryActive)      return PRIMARY_ACTION_ADVISORY;
  return PRIMARY_ACTION_NONE;
}

// Koneluettava nimi /api/status-kenttaan. JS renderoi taman perusteella —
// pidettava synkassa wifi_portal_html_script.h renderActionBar():n kanssa.
static inline const char* portal_primaryActionName(PrimaryAction a) {
  switch (a) {
    case PRIMARY_ACTION_CLEAR_FAULT:   return "clear_fault";
    case PRIMARY_ACTION_STEP_ACK:      return "step_ack";
    case PRIMARY_ACTION_APPROVE_PHASE: return "approve_phase";
    case PRIMARY_ACTION_ADVISORY:      return "advisory";
    case PRIMARY_ACTION_START_GROW:    return "start_grow";
    default:                           return "none";
  }
}

#endif // PORTAL_PRIMARY_ACTION_H
