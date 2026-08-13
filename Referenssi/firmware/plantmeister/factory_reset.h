/*=====================================================================
  factory_reset.h - Mitka tiedostot tehdasreset poistaa (yksi totuus)

  Nollaukseen on KAKSI sisaankayntia, ja molemmat kutsuvat tata:

    1. /api/command {"cmd":"FACTORY_RESET","value":"CONFIRM"}
       -> command_handler.h. Vaatii verkon + autentikoinnin.

    2. Nappi pohjassa kaynnistyksessa (FACTORY_RESET_HOLD_MS)
       -> factory_reset_gesture.h. Toimii ilman verkkoa ja ilman salasanaa,
          eli myos silloin kun reitin 1 esteet ovat juuri se syy nollata.

  Tiedostolista elaa vain taalla. Se on turvatietoa — se kertoo mita
  SAILYTETAAN — eika sellaisesta saa olla kahta kopiota jotka voivat
  ajautua erilleen.

  ── Jakoperuste ──────────────────────────────────────────────────────
  EI "omistajan data vs. laitteen data", vaan konkreettisempi:

      Syntyyko se itsestaan uudelleen?      -> nollaa
      Vaatiiko se ihmisen mittalaitteineen? -> sailyta

      config.json      -> config_setDefaults()   (automaattinen) -> POIS
      plants.json      -> plants_loadDefaults()  (automaattinen) -> POIS
      growclock.json   -> alkaa nollasta         (automaattinen) -> POIS
      calibration.json -> EI SYNNY ITSESTAAN                     -> JAA

  Kalibrointi on ainoa jota kone ei voi tuottaa: pumpMlPerSec mitataan
  mittalasilla, motorStepsUp/Down ajamalla kelkka paatyihin, powerShuntOhms
  lukemalla levylle juotettu vastus. Niiden pyyhkiminen ei palauta laitetta
  "tehdastilaan" vaan tekee siita toimintakelvottoman kunnes ihminen ajaa
  wizardit uudelleen — ja moottorin rajojen menetys on lisaksi vaarallista,
  koska hissilla ei ole homingia: rajat ovat ainoa tieto siita missa paadyt
  ovat. Kalibroinnin nollaus on erillinen tarkoituksellinen tyokalu
  (wizardit / pio run -t erase), ei taman sivuvaikutus.

  ── Miksi poisto eika "nollattu"-lippu ───────────────────────────────
  config_load() putoaa config_setDefaults()-oletuksiin kun config.json
  puuttuu, ja niissa onboardingComplete on jo false (config_defaults.h).
  Poisto ajaa siis TASMALLEEN saman polun kuin aidosti tuore laite — ei
  rinnakkaista "melkein tuore" -tilaa jota pitaisi yllapitaa erikseen.
  Sivuhyoty: resetin testaaminen testaa oikean ensiboot-polun.

  ── plants.json on poistettava ───────────────────────────────────────
  Se yliajaa firmwaren sisaanrakennetut oletukset. Jos se jaa, firmwareen
  korjatut ohjetekstit eivat koskaan tule laitteelle — todettu kantapaan
  kautta 15.7.2026, jolloin korjattu basilikateksti ei nakynyt resetin
  jalkeenkaan koska vanha plants.json eli yli nollauksen.
=====================================================================*/

#ifndef FACTORY_RESET_H
#define FACTORY_RESET_H

#include "config.h"
#include <LittleFS.h>

struct FactoryResetResult {
  bool configRemoved;
  bool growClockRemoved;
  bool plantsRemoved;
};

// Poista uudelleensyntyvat tiedostot. Kalibrointiin ei kosketa.
//
// Palauttaa per-tiedosto tiedon siita loytyiko se: "ei ollut" on normaali
// tulos (esim. growclock puuttuu jos kasvatusta ei ole koskaan aloitettu),
// ei virhe. Kutsuja logaa; tama ei logaa itse, jotta funktio pysyy
// natiivitestattavana ilman DEBUG-makroja.
inline FactoryResetResult factory_reset_wipe() {
  FactoryResetResult r;
  r.configRemoved    = LittleFS.remove(PATH_DEVICE_CONFIG);
  r.growClockRemoved = LittleFS.remove(PATH_GROW_CLOCK);
  r.plantsRemoved    = LittleFS.remove(PATH_PLANTS_DB);
  // PATH_CALIBRATION: tarkoituksella koskematta. Ks. otsikon jakoperuste.
  return r;
}

#endif // FACTORY_RESET_H
