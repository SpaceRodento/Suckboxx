/*=====================================================================
  resolve_field.h - resolveField(): FieldId -> formatted value + unit.

  Pure logic extracted from display_layout.h so it is unit-testable
  without GxEPD2/Adafruit_GFX (see test/test_resolve_field/). This is the
  bridge between the editable layout (layout_def.h EINK_CARDS/EINK_CONTEXT)
  and live data (DisplayData): ADD A CASE HERE when you add a FieldId, and
  mirror its JSON path in scripts/eink_preview.py FIELD_MAP so the preview
  reads it too.

  Returns false when the source is invalid/absent -> caller shows "--" or
  skips the field. display_layout.h includes this header and calls
  resolveField() from display_render()/display_contentHash(); it has no
  dependency on the GxEPD2 `display` object itself.
=====================================================================*/

#ifndef RESOLVE_FIELD_H
#define RESOLVE_FIELD_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#include "display_data.h"
#include "layout_def.h"

static bool resolveField(uint8_t field, const DisplayData* d,
                         char* val, size_t valN, char* unit, size_t unitN) {
  unit[0] = '\0';
  switch (field) {
    // VPD kahdella desimaalilla: tavoiteikkuna on vain 0.4 kPa leveä (esim.
    // taimivaihe 0.4-0.8), joten yksi desimaali pyöristää arvon tavoitteen
    // rajalle ja näyttö näyttää itsensä kanssa ristiriitaiselta — 0.86 kPa
    // piirtyi "0.8":na tavoitteen "tav 0.4-0.8 kPa" viereen ja silti
    // poikkeamana (rautahavainto 27.7.2026). Luokittelu on aina tehty raa'asta
    // arvosta, joten kyse oli vain esitystarkkuudesta.
    case FIELD_VPD:
      if (!d->vpdValid) return false;
      snprintf(val, valN, "%.2f", d->vpdKpa); snprintf(unit, unitN, "kPa"); return true;
    case FIELD_LEAF_DELTA:
      if (!d->leafTempValid) return false;
      snprintf(val, valN, "%+.1f", d->leafTempC - d->airTemp); snprintf(unit, unitN, "C"); return true;
    case FIELD_LEAF_TEMP:
      if (!d->leafTempValid) return false;
      snprintf(val, valN, "%.1f", d->leafTempC); snprintf(unit, unitN, "C"); return true;
    case FIELD_DLI:
      if (!(d->ppfdValid || d->dli > 0.0f)) return false;
      snprintf(val, valN, "%.1f", d->dli); snprintf(unit, unitN, "mol"); return true;
    // PPFD mukautuvalla tarkkuudella: kasvatustasoilla (100-400 umol) desimaali
    // on kohinaa, mutta pimeässä koko lukema mahtuu desimaalien puolelle —
    // 0.25 umol piirtyi "0":na, mikä näyttää rikkinäiseltä anturilta vaikka
    // lamppu on vain pois päältä (rautahavainto 27.7.2026). Alle 10 umol
    // näytetään siis yhdellä desimaalilla, sen yli pyöristettynä.
    // Saturoitunut naytte merkitaan ">"-etuliitteella: anturin mittausalue on
    // taynna, joten luku on alaraja. Ilman merkintaa naytto piirtaisi tasanteen
    // faktana ja se luetaan "valo lakkasi kirkastumasta" (havaittu 29.7.2026:
    // f5 tasan 50000, PPFD jumissa ~97:ssa auringon yha kirkastuessa).
    case FIELD_PPFD: {
      if (!d->ppfdValid) return false;
      const char* prefix = d->ppfdSaturated ? ">" : "";
      if (d->ppfd < 10.0f) snprintf(val, valN, "%s%.1f", prefix, d->ppfd);
      else                 snprintf(val, valN, "%s%.0f", prefix, d->ppfd);
      snprintf(unit, unitN, "umol"); return true;
    }
    // Anturin oma lukema (ei geometriakerrointa). Sama tarkkuussaanto kuin
    // FIELD_PPFD, jotta arvot ovat suoraan vertailukelpoisia rinnakkain.
    case FIELD_PPFD_SENSOR:
      if (!d->ppfdValid) return false;
      if (d->ppfdSensor < 10.0f) snprintf(val, valN, "%.1f", d->ppfdSensor);
      else                       snprintf(val, valN, "%.0f", d->ppfdSensor);
      snprintf(unit, unitN, "umol"); return true;
    case FIELD_CO2:
      if (!d->co2Valid) return false;
      snprintf(val, valN, "%d", d->co2Ppm); snprintf(unit, unitN, "ppm"); return true;
    case FIELD_AIR_TEMP:
      if (!d->envValid) return false;
      snprintf(val, valN, "%.1f", d->airTemp); snprintf(unit, unitN, "C"); return true;
    case FIELD_AIR_RH:
      if (!d->envValid) return false;
      snprintf(val, valN, "%.0f", d->airHumidity); snprintf(unit, unitN, "%%"); return true;
    case FIELD_WATER_TEMP:
      if (!(d->waterTemp > 0.5f && d->waterTemp < 60.0f)) return false;
      snprintf(val, valN, "%.1f", d->waterTemp); snprintf(unit, unitN, "C"); return true;
    case FIELD_HEIGHT:
      if (!d->heightValid) return false;
      snprintf(val, valN, "%d", d->plantHeightMm); snprintf(unit, unitN, "mm"); return true;
    case FIELD_WATER_LEVEL:
      snprintf(val, valN, "%s", d->waterLevelOk ? TXT_WATER_OK : TXT_WATER_LOW); return true;
    case FIELD_TDS:
      snprintf(val, valN, "%d", d->tdsPpm); snprintf(unit, unitN, "ppm"); return true;
    case FIELD_BATTERY:
      snprintf(val, valN, "%d", d->batteryPercent); snprintf(unit, unitN, "%%"); return true;
    case FIELD_EBB_STATE:
      snprintf(val, valN, "%s", d->ebbState); return true;
    case FIELD_NEXT_FLOOD:
      if (d->ebbNextCycleSec < 0) return false;
      snprintf(val, valN, "%d", d->ebbNextCycleSec / 60); snprintf(unit, unitN, "min"); return true;
    case FIELD_LIGHTS:
      snprintf(val, valN, "%s", d->lightsOn ? TXT_ON : TXT_OFF); return true;
    case FIELD_PUMP:
      snprintf(val, valN, "%s", d->pumpRunning ? TXT_ON : TXT_OFF); return true;
    case FIELD_PLANT_NAME:
      if (!d->plantName[0]) return false;
      snprintf(val, valN, "%s", d->plantName); return true;
    case FIELD_PHASE_NAME:
      if (!d->growActive || !d->phaseName[0]) return false;
      snprintf(val, valN, "%s", d->phaseName); return true;
    case FIELD_GROW_DAYS:
      if (!d->growActive) return false;
      snprintf(val, valN, "%d", d->growElapsedDays); snprintf(unit, unitN, "pv"); return true;
    case FIELD_UPTIME:
      snprintf(val, valN, "%lu", (unsigned long)(d->devUptimeS / 60UL));
      snprintf(unit, unitN, "min"); return true;
    // ── Virrankulutus (INA228, 12 V vakio) ────────────────────────────
    case FIELD_POWER_W:  // hetkellinen: 12 V * virta
      if (!d->powerValid) return false;
      snprintf(val, valN, "%.1f", 12.0f * d->powerCurrentMa / 1000.0f);
      snprintf(unit, unitN, "W"); return true;
    // Keskiteho = 12 V * varaus / aika. Kelpaa VAIN jos varauslaskuri ja uptime
    // mittaavat samaa ajanjaksoa — INA228:n varausrekisteri EI nollaudu ESP32:n
    // SW-resetissa (siru pysyy virrassa), joten flashin jalkeen uptime on
    // sekunteja mutta varaus tunteja: 3376 mAh / 0.86 h nayttti 47 W kun
    // todellinen kulutus oli 0.68 W (kayttajan havainto 28.7.2026).
    //
    // Ristiintarkistus hetkellista tehoa vasten: keskiteho ei voi olla
    // moninkertainen hetkelliseen nahden vakaassa kuormassa. Jos on, laskuri
    // on desynkassa -> piilota luku sen sijaan etta nayttaisi vaaraa.
    case FIELD_AVG_POWER_W: {
      if (!d->powerValid || d->devUptimeS == 0) return false;
      const float avgW = (12.0f * d->powerChargeMah / 1000.0f)
                         / ((float)d->devUptimeS / 3600.0f);
      const float instantW = 12.0f * d->powerCurrentMa / 1000.0f;
      if (instantW > 0.01f && avgW > instantW * POWER_AVG_SANITY_RATIO) return false;
      snprintf(val, valN, "%.1f", avgW);
      snprintf(unit, unitN, "W"); return true;
    }
    case FIELD_ENERGY_WH:  // integraali bootista: 12 V * charge
      if (!d->powerValid) return false;
      snprintf(val, valN, "%.2f", 12.0f * d->powerChargeMah / 1000.0f);
      snprintf(unit, unitN, "Wh"); return true;
    default:
      return false;
  }
}

#endif // RESOLVE_FIELD_H
