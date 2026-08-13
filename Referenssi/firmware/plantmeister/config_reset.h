/*=====================================================================
  config_reset.h - Per-nakyman "Palauta oletukset" -kenttapalautus

  Suunnitelman lukittu vaatimus (web-ui-uudelleensuunnittelu.md §8.1):
  autotallennus (§3.6) poisti peruutusmahdollisuuden — muutosta ei voi
  enaa perua jattamalla Tallenna painamatta. Siksi jokaisella nakymalla
  on nappi joka palauttaa VAIN sen nakyman asetukset tehdasarvoihin.

  ── Yksi totuus oletusarvoille ───────────────────────────────────────
  §8.1: "Suunnittele niin ettei oletuksia kirjoiteta kahteen kertaan
  (firmware + JS)." Siksi tama EI sisalla yhtaan oletusarvoa: kutsuja
  rakentaa referenssin config_setDefaults():lla (config_defaults.h) ja
  tama kopioi pyydetyn kentan siita. JS tietaa vain AVAIMET (mitka
  kentat kuuluvat mihinkin nakymaan) — arvot tulevat aina laitteesta.

  ── Whitelist, ei blacklist ──────────────────────────────────────────
  Vain saannolliset saatoasetukset ovat palautettavia. Tuntematon avain
  palauttaa false -> reitti vastaa 400 eika tee mitaan. Nain seuraavat
  eivat voi ikina palautua "oletuksiin" tasta polusta:
    - wifi_* / admin_pin: salaisuudet (oletus = tyhja -> palautus olisi
      hiljainen verkosta-pudotus, sama vikaluokka kuin PR #212 korjasi)
    - plant_id / grow_*: kasvatuksen tila (Kasvatus-nakymalla on oma
      per-kasvi-palautus /api/plant/reset; kaynnissa olevaa kasvatusta
      ei nollata asetusnapista)
    - onboarding*: kayttoonoton tila (vain tehdasreset palauttaa)
  Tama ei ole FACTORY_RESET eika saa kasvaa sellaiseksi (§8.1).

  Pure + header-only -> natiivitestattava (test_config_reset).
=====================================================================*/

#ifndef CONFIG_RESET_H
#define CONFIG_RESET_H

#include <string.h>

#include "structs.h"

// Kopioi yhden nimetyn kentan defs->cfg. Palauttaa false jos avain ei
// ole palautettavien whitelistilla (kutsujan vastuu hylata koko pyynto).
// Avainnimet ovat samat kuin POST /api/config -rajapinnassa, jotta JS
// kayttaa yhta nimistoa molempiin suuntiin.
static inline bool config_resetField(DeviceConfig* cfg,
                                     const DeviceConfig* defs,
                                     const char* key) {
  if (!cfg || !defs || !key) return false;

  // Asetukset-nakyma
  if (strcmp(key, "light_on_hour") == 0)   { cfg->lightOnHour       = defs->lightOnHour;       return true; }
  if (strcmp(key, "lora_address") == 0)    { cfg->loraAddress       = defs->loraAddress;       return true; }
  if (strcmp(key, "lora_network_id") == 0) { cfg->loraNetworkId     = defs->loraNetworkId;     return true; }
  if (strcmp(key, "lora_target") == 0)     { cfg->loraTargetAddress = defs->loraTargetAddress; return true; }

  // Huolto-nakyma
  if (strcmp(key, "button_action") == 0)           { cfg->buttonAction           = defs->buttonAction;           return true; }
  if (strcmp(key, "ebb_flood_interval_min") == 0)  { cfg->ebbFloodIntervalMin    = defs->ebbFloodIntervalMin;    return true; }
  if (strcmp(key, "ebb_flood_duration_sec") == 0)  { cfg->ebbFloodDurationSec    = defs->ebbFloodDurationSec;    return true; }
  if (strcmp(key, "ebb_soak_duration_sec") == 0)   { cfg->ebbSoakDurationSec     = defs->ebbSoakDurationSec;     return true; }
  if (strcmp(key, "ebb_soak_pwm_pct") == 0)        { cfg->ebbSoakPwmPct          = defs->ebbSoakPwmPct;          return true; }
  if (strcmp(key, "ebb_drain_timeout_sec") == 0)   { cfg->ebbDrainTimeoutSec     = defs->ebbDrainTimeoutSec;     return true; }
  if (strcmp(key, "ebb_overflow_auto_clear") == 0) { cfg->ebbOverflowAutoClear   = defs->ebbOverflowAutoClear;   return true; }
  if (strcmp(key, "ebb_circulate_enabled") == 0)   { cfg->ebbCirculateEnabled    = defs->ebbCirculateEnabled;    return true; }
  if (strcmp(key, "ebb_circulate_interval_min") == 0) { cfg->ebbCirculateIntervalMin = defs->ebbCirculateIntervalMin; return true; }
  if (strcmp(key, "ebb_circulate_duration_sec") == 0) { cfg->ebbCirculateDurationSec = defs->ebbCirculateDurationSec; return true; }
  if (strcmp(key, "ebb_circulate_duty_pct") == 0)  { cfg->ebbCirculateDutyPct    = defs->ebbCirculateDutyPct;    return true; }

  // Kaikki muu (wifi_*, admin_pin, plant_id, grow_*, onboarding*, tuntematon)
  // on tarkoituksella palauttamattomissa tasta polusta.
  return false;
}

#endif // CONFIG_RESET_H
