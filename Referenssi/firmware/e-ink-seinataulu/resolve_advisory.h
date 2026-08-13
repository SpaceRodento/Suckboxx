/*=====================================================================
  resolve_advisory.h - resolveAdvisory(): DisplayData -> one-line coaching
  message for the COACH profile (coach_view.h).

  Pure logic, no GxEPD2/Adafruit_GFX dependency, so it is unit-testable the
  same way as resolve_field.h (see test/test_resolve_advisory/). Priority
  order (first match wins) reflects severity, not data-tier order:

    1. !dataValid            -> "odotetaan dataa"
    2. device FAULT          -> most severe, always shown first
    3. MAINTENANCE           -> operator is servicing; actuators locked. Ranks
                                 above the water rows because an open, low
                                 reservoir is expected during service.
    4. ebb&flow water fault  -> pump/sensor issue during a flood cycle
    5. water level low       -> reservoir needs a refill
    6. air temperature high  -> heat stress. Ranks ABOVE VPD because the two
                                 co-occur (heat dries the air) but the fix
                                 differs: adding humidity does nothing against
                                 direct sun, shading does.
    7. VPD outside alert band -> the WIDER of the generic alert constants
                                 (COACH_VPD_COMFORT_*, config.h) and the
                                 device's per-phase band (M1,
                                 status_targetsForData()). Never the narrower:
                                 the per-phase band is a comfort range, this
                                 is an alert threshold (advisory_peBounds()).
    8. PPFD too high         -> same rule, ceiling side (COACH_PPFD_MAX_UMOL
                                 or a wider published band). NOT gated on
                                 lightsOn: the sun ignores the relay.
    9. PPFD too low while lights are on -> same rule, floor side
                                 (COACH_PPFD_LIGHTS_ON_MIN_UMOL)
   10. phase advance pending -> device waits for the user to confirm
   11. phase action          -> what to do at this stage (grow_shortAction on
                                 the PM side, already sized to one line and
                                 chosen by start method). Ranks BELOW the
                                 alerts: guidance stays true for weeks, alerts
                                 need action now. It takes the place of "all
                                 good" -- the moment nothing is wrong is
                                 exactly the moment the stage work can be done.
   12. otherwise             -> "all good" (no grow running)

  Deterministic and rule-based on purpose: VPD/PPFD are derived from the same
  per-phase bands as the STATUS profile (M1, 11.8.2026 — docs/kehitys/
  Fable_kehityspolku.md § M1) but only widened by them, so this stays a coarse
  "is this wildly off" check rather than a precise growing recommendation —
  the precise version is what the STATUS cards already show; no urgency curve
  or severity weighting exists yet (that is utility-model work, § M4+).
=====================================================================*/

#ifndef RESOLVE_ADVISORY_H
#define RESOLVE_ADVISORY_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "display_data.h"
#include "layout_def.h"
#include "config.h"
#include "status_targets.h"

enum AdvisoryLevel {
  ADVISORY_OK = 0,
  ADVISORY_INFO,
  ADVISORY_WARN,
  ADVISORY_FAULT,
};

struct AdvisoryResult {
  AdvisoryLevel level;
  char          message[64];
};

// M1 (11.8.2026, korjattu paivaistunnon katselmuksessa samana paivana):
// johda VPD/PPFD-rajat laitteen julkaisemasta vaihekohtaisesta kaistasta
// (status_targets.h), mutta VAIN LEVENTAEN — ei koskaan kaventaen.
//
// Miksi union eika korvaus: COACH_*-vakiot ja PE-mukavuuskaista EIVAT ole
// sama luku kahdessa paikassa, vaan kaksi eri kasitetta samasta suureesta.
//   COACH_VPD_COMFORT_* = 0.4-1.6 kPa   -> HALYTYSRAJA ("nyt on oikeasti vaarin")
//   vaiheen PE-kaista    = 0.8-1.2 kPa   -> MUKAVUUSALUE ("nyt ollaan optimissa")
// Jos halytys ottaisi mukavuuskaistan sellaisenaan, banneri varoittaisi heti
// kun kasvi poistuu optimista: VPD 1.3 ja PPFD 450 ovat taysin normaaleja
// lukemia jotka olisivat laukaisseet varoituksen. Banneri nayttaa yhden rivin
// kerrallaan, joten jatkuvasti paalla oleva varoitus syo juuri sen
// signaaliarvon jota varten se on olemassa — ja taman tiedoston oma
// yla-kommentti lupaa yha "coarse 'is this wildly off' check". Optimista
// poikkeamisen kertovat jo STATUS-kortit (status_classify, LOW/OK/HIGH).
//
// Union tayttaa silti Q2:n vaatimuksen (10.8.2026): COACH_* ei ole enaa
// ITSENAINEN lahde vaan ulkoraja, ja halytys LEVENEE automaattisesti jos
// jonkin vaiheen kaista on yleisrajaa valjempi. Kaventaminen — ja silloin
// tarvittava marginaali — on arvoarvostelma, ei mitattava luku: se on
// docs/yoajo/KYSYMYKSET.md Q9, ei taman funktion paatettavissa.
//
// COACH_AIR_TEMP_MAX_C pysyy itsenaisena tarkoituksella: GrowPhaseParamsissa
// ei ole ilmalampokaistaa lainkaan (vain lehtilampo — eri anturi ja eri
// suure). Se on Q8.
static void advisory_peBounds(const DisplayData* d, uint8_t fieldId,
                              float fallbackLo, float fallbackHi,
                              float* lo, float* hi) {
  *lo = fallbackLo;
  *hi = fallbackHi;
  int count = 0;
  const PeTarget* targets = status_targetsForData(d, &count);
  for (int i = 0; i < count; i++) {
    if (targets[i].field != fieldId) continue;
    if (targets[i].lo != PE_NO_LIMIT && targets[i].lo < *lo) *lo = targets[i].lo;
    if (targets[i].hi != PE_NO_LIMIT && targets[i].hi > *hi) *hi = targets[i].hi;
    return;
  }
}

static AdvisoryResult resolveAdvisory(const DisplayData* d) {
  AdvisoryResult r;
  r.level = ADVISORY_OK;

  if (!d->dataValid) {
    r.level = ADVISORY_INFO;
    snprintf(r.message, sizeof(r.message), TXT_ADV_WAITING);
    return r;
  }

  if (strcmp(d->deviceStateName, "FAULT") == 0) {
    r.level = ADVISORY_FAULT;
    if (d->deviceFaultMsg[0]) {
      snprintf(r.message, sizeof(r.message), TXT_ADV_FAULT, d->deviceFaultMsg);
    } else {
      snprintf(r.message, sizeof(r.message), TXT_ADV_FAULT_GENERIC);
    }
    return r;
  }

  // Above the water/VPD rows on purpose: during service the reservoir is
  // expected to be open and low, so "tarkista taytto" would be noise at exactly
  // the moment the operator is already doing it. What they need to see is that
  // the pump is locked and it is safe to keep working.
  if (strcmp(d->deviceStateName, "MAINTENANCE") == 0) {
    r.level = ADVISORY_INFO;
    snprintf(r.message, sizeof(r.message), TXT_ADV_MAINTENANCE);
    return r;
  }

  if (d->ebbFault) {
    r.level = ADVISORY_FAULT;
    snprintf(r.message, sizeof(r.message), TXT_ADV_WATER_LOW);
    return r;
  }

  if (!d->waterLevelOk) {
    r.level = ADVISORY_WARN;
    snprintf(r.message, sizeof(r.message), TXT_ADV_WATER_LOW);
    return r;
  }

  // Kuumuus ENNEN VPD:ta: ne esiintyvat lahes aina yhdessa (paahde kuivaa
  // ilman), mutta korjaus on eri. "Lisaa kosteutta" on vaara neuvo jos
  // perussyy on 44 asteen auringonpaahde — silloin kosteus haihtuu saman tien
  // ja ainoa toimiva teko on varjostus. Mitattu 29.7.2026 parvekkeella:
  // 43,7 C / 23,7 % RH / VPD 6,9 kPa yhta aikaa.
  if (d->envValid && d->airTemp > COACH_AIR_TEMP_MAX_C) {
    r.level = ADVISORY_WARN;
    snprintf(r.message, sizeof(r.message), TXT_ADV_HEAT_HIGH);
    return r;
  }

  if (d->vpdValid) {
    float vpdLo, vpdHi;
    advisory_peBounds(d, FIELD_VPD, COACH_VPD_COMFORT_MIN_KPA, COACH_VPD_COMFORT_MAX_KPA,
                      &vpdLo, &vpdHi);
    if (d->vpdKpa < vpdLo) {
      r.level = ADVISORY_WARN;
      snprintf(r.message, sizeof(r.message), TXT_ADV_VPD_LOW);
      return r;
    }
    if (d->vpdKpa > vpdHi) {
      r.level = ADVISORY_WARN;
      snprintf(r.message, sizeof(r.message), TXT_ADV_VPD_HIGH);
      return r;
    }
  }

  // Liika valo EI vaadi lightsOn-ehtoa, toisin kuin liian vahan valo. Aurinko
  // ei valita laitteen valorelesta: 29.7.2026 kasvit siirrettiin parvekkeelle
  // ja anturi mittasi 795 umol taimelle jonka tavoite on 100-200. Jos tama
  // sarjattaisiin lightsOn:iin, varoitus katoaisi juuri silloin kun lamppu
  // sammutetaan mutta aurinko paistaa.
  //
  // Saturaatio ei myoskaan estaa tata neuvoa (toisin kuin alla olevaa): katossa
  // oleva lukema on ALARAJA, ja alaraja voi todistaa ylityksen vaikkei se voi
  // todistaa alitusta. Sama paattely kuin status_targets.h:n PPFD-vartijassa.
  //
  // ppfdLo/ppfdHi lasketaan tahan kertaalleen (M1) ja kaytetaan molemmissa
  // alla olevissa ehdoissa — sama PE-kaista, kaksi suuntaa.
  float ppfdLo, ppfdHi;
  advisory_peBounds(d, FIELD_PPFD, COACH_PPFD_LIGHTS_ON_MIN_UMOL, COACH_PPFD_MAX_UMOL,
                    &ppfdLo, &ppfdHi);
  if (d->ppfdValid && d->ppfd > ppfdHi) {
    r.level = ADVISORY_WARN;
    snprintf(r.message, sizeof(r.message), TXT_ADV_LIGHT_HIGH);
    return r;
  }

  // !ppfdSaturated on pakollinen: katossa oleva lukema on ALARAJA, joten
  // "valoteho heikko" olisi taysin vaara neuvo juuri kirkkaimmassa valossa.
  // Sama luku voi alittaa kynnyksen vaikka todellinen valo olisi moninkertainen
  // (havaittu 29.7.2026: PPFD jumissa ~97:ssa, kynnys 100 -> naytto olisi
  // kehottanut lisaamaan valoa suorassa auringossa).
  if (d->lightsOn && d->ppfdValid && !d->ppfdSaturated &&
      d->ppfd < ppfdLo) {
    r.level = ADVISORY_WARN;
    snprintf(r.message, sizeof(r.message), TXT_ADV_LIGHT_LOW);
    return r;
  }

  // Laite ehdottaa vaihesiirtymaa ja odottaa kuittausta. Ylin ei-hairio-rivi:
  // se on ainoa kohta jossa laite odottaa kayttajalta paatosta.
  if (d->growActive && d->growPendingAdvance) {
    r.level = ADVISORY_INFO;
    snprintf(r.message, sizeof(r.message), TXT_ADV_PHASE_DONE);
    return r;
  }

  // Vaiheen toimintaohje "kaikki hyvin" -rivin TILALLA, ei sen ylapuolella.
  //
  // Ohje ei ole halytys: se on saman vaiheen neuvo joka patee viikkoja, kun
  // taas vesi/VPD/valo vaativat toimia nyt. Siksi se ei saa ohittaa niita.
  // Mutta juuri siina hetkessa kun mitaan halytettavaa ei ole, "Kaikki hyvin"
  // ei kerro kayttajalle mitaan hyodyllista - ja se on nimenomaan se hetki
  // jolloin han voisi tehda vaiheen tyot. Tama on koko COACH-profiilin lupaus
  // ("mita seuraavaksi") siina tyhjassa tilassa jossa se maksaa jotain.
  if (d->growActive && d->growAction[0] != '\0') {
    r.level = ADVISORY_OK;
    snprintf(r.message, sizeof(r.message), "%s", d->growAction);
    return r;
  }

  r.level = ADVISORY_OK;
  snprintf(r.message, sizeof(r.message), TXT_ADV_OK);
  return r;
}

// Turva-/tilakriittinen halytys STATUS-profiilille (status_view.h). Palauttaa
// VAIN ne resolveAdvisory-ketjun rivit jotka vaativat huomiota heti eivatka
// riipu sensori-mukavuudesta: FAULT, ebb-vesivika, vesi vahissa, huoltotila,
// odottaa vaihesiirron kuittausta. Jarjestys = sama prioriteetti kuin
// resolveAdvisoryssa (ensimmainen osuma voittaa).
//
// POIS jatetty tahallaan: VPD/PPFD-mukavuus (STATUS-paneelin PE-rivit hoitavat
// ne per-vaihe-tarkasti, status_targets.h — tarkemmin kuin COACH:n geneeriset
// kynnykset) ja vaiheohje (se on jo paneelin tilalause, PM:n growAction).
// ADVISORY_OK = ei kriittista halytysta -> kutsuja nayttaa normaalin tilalauseen.
//
// Jakaa TXT_ADV_*-sanamuodot resolveAdvisoryn kanssa: yksi totuus halytys-
// teksteille kummallekin profiilille. Tama on se piste jossa aiemmin COACH-only
// ollut halytyslogiikka tulee jaetuksi STATUS-profiilin kanssa (ei enaa kuollutta
// koodia joka renderoityy vain valinnaisessa profiilissa).
static AdvisoryResult resolveCriticalAlert(const DisplayData* d) {
  AdvisoryResult r;
  r.level = ADVISORY_OK;
  r.message[0] = '\0';

  // Ilman dataa ei kriittista halytysta: status_view nayttaa oman
  // "Odotetaan dataa" -oletuksensa, eivatka tila-/vesikentat ole viela padet.
  if (!d->dataValid) return r;

  if (strcmp(d->deviceStateName, "FAULT") == 0) {
    r.level = ADVISORY_FAULT;
    if (d->deviceFaultMsg[0]) {
      snprintf(r.message, sizeof(r.message), TXT_ADV_FAULT, d->deviceFaultMsg);
    } else {
      snprintf(r.message, sizeof(r.message), TXT_ADV_FAULT_GENERIC);
    }
    return r;
  }

  if (strcmp(d->deviceStateName, "MAINTENANCE") == 0) {
    r.level = ADVISORY_INFO;
    snprintf(r.message, sizeof(r.message), TXT_ADV_MAINTENANCE);
    return r;
  }

  if (d->ebbFault) {
    r.level = ADVISORY_FAULT;
    snprintf(r.message, sizeof(r.message), TXT_ADV_WATER_LOW);
    return r;
  }

  if (!d->waterLevelOk) {
    r.level = ADVISORY_WARN;
    snprintf(r.message, sizeof(r.message), TXT_ADV_WATER_LOW);
    return r;
  }

  if (d->growActive && d->growPendingAdvance) {
    r.level = ADVISORY_INFO;
    snprintf(r.message, sizeof(r.message), TXT_ADV_PHASE_DONE);
    return r;
  }

  return r;  // ADVISORY_OK — ei kriittista halytysta
}

#endif // RESOLVE_ADVISORY_H
