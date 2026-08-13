/*=====================================================================
  sd_logger_format.h - CSV row/filename formatting for the SD logger (M2)

  Pure logic, no Arduino/SD/SPI dependency, extracted so it is unit-testable
  the same way as data_fetch_parse.h and resolve_field.h (see
  test/test_sd_logger/). sd_logger.h wraps this with the actual hardware
  I/O (SD.begin, File, presence/EN probing).

  Column set matches scripts/sensor_log.py's COLUMNS as closely as the
  panel's DisplayData allows. NOT available on the panel (documented in
  the M2 PR, not silently dropped):
    - the 10 AS7341 spectral channels (spec_f1_415nm..spec_nir) — the
      /api/state inclusion filter (data_fetch_parse.h kStateSensorKeys)
      does not request sensors.light_spectrum.* to keep the fetch's JSON
      filter/doc comfortably under its RAM budget on the panel's MCU.
    - power_bus_v, power_mw, power_energy_wh — present in the /api/state
      contract and even in kStateSensorKeys, but data_fetch_parse.h's
      populateFromJson() never reads them into DisplayData (only
      power_current_ma/power_charge_mah/power_valid are). Extending
      DisplayData is a separate, small change — left out here on purpose
      to keep this PR to "log what the panel already has".

  Timestamps are never faked (dokumentaatio.md): time_source says which
  kind the row got. uptime_ms is always present so a row logged before
  NTP sync is still orderable within its own boot.
=====================================================================*/

#ifndef SD_LOGGER_FORMAT_H
#define SD_LOGGER_FORMAT_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "display_data.h"

#ifndef SD_LOG_SCHEMA_VERSION
#define SD_LOG_SCHEMA_VERSION 1
#endif

#define SD_LOG_CSV_HEADER \
  "timestamp_iso,time_source,uptime_ms,device_state," \
  "air_temp_c,air_humidity,vpd_kpa,co2_ppm,leaf_temp_c,water_temp_c," \
  "plant_height_mm,ppfd,dli,tds_ppm,battery_v,battery_pct," \
  "power_current_ma,power_charge_mah,lights_on,pump_running"

// Tiedostonimi ISO-paivamaarasta — lajittuu kronologisesti (dokumentaatio.md).
static inline void sdlog_buildFilename(char* out, size_t outN,
                                       int year, int month, int day) {
  snprintf(out, outN, "/sensors_%04d-%02d-%02d.csv", year, month, day);
}

// Ensimmainen rivi uudessa tiedostossa: skeemaversio metatietona. Alkaa
// '#':lla jotta CSV-parseri joka tukee comment='#':aa (esim. pandas)
// ohittaa sen automaattisesti; Excel/naiivi lukija nakee sen tavallisena
// ensimmaisena rivina, ei riko tiedostoa.
static inline void sdlog_metaLine(char* out, size_t outN) {
  snprintf(out, outN, "# plantmeister_sd_log schema_version=%d", SD_LOG_SCHEMA_VERSION);
}

static inline void sdlog_headerLine(char* out, size_t outN) {
  snprintf(out, outN, "%s", SD_LOG_CSV_HEADER);
}

// Tyhja kentta jos valid=false — ei koskaan arvattua/vanhentunutta lukemaa
// piiloteta numeroksi (sama periaate kuin sensor_log.py:n _dig()-tyhjennys).
static inline void sdlog_fmtFloatGated(char* buf, size_t n, float v, bool valid, int decimals) {
  if (!valid) { buf[0] = '\0'; return; }
  char fmt[8];
  snprintf(fmt, sizeof(fmt), "%%.%df", decimals);
  snprintf(buf, n, fmt, (double)v);
}

static inline void sdlog_fmtIntGated(char* buf, size_t n, int v, bool valid) {
  if (!valid) { buf[0] = '\0'; return; }
  snprintf(buf, n, "%d", v);
}

// Rakentaa yhden CSV-rivin SD_LOG_CSV_HEADER:n sarakejarjestyksessa.
//   ntpValid     - true jos isoTimestamp on oikea kalenteriaika (NTP)
//   isoTimestamp - "" kun ntpValid=false (ALA feikkaa - dokumentaatio.md)
//   uptimeMs     - aina saatavilla, jarjestaa rivit myos ilman NTP:ta
static inline void sdlog_buildRow(const DisplayData* d,
                                  bool ntpValid, const char* isoTimestamp,
                                  uint32_t uptimeMs,
                                  char* out, size_t outN) {
  char airTemp[16], airHum[16], vpd[16], co2[16], leafTemp[16],
       height[16], ppfd[16], powerMa[16], powerMah[16];

  sdlog_fmtFloatGated(airTemp,  sizeof(airTemp),  d->airTemp,       d->envValid,      1);
  sdlog_fmtFloatGated(airHum,   sizeof(airHum),   d->airHumidity,   d->envValid,      1);
  sdlog_fmtFloatGated(vpd,      sizeof(vpd),      d->vpdKpa,        d->vpdValid,      2);
  sdlog_fmtIntGated(  co2,      sizeof(co2),      d->co2Ppm,        d->co2Valid);
  sdlog_fmtFloatGated(leafTemp, sizeof(leafTemp), d->leafTempC,     d->leafTempValid, 1);
  sdlog_fmtIntGated(  height,   sizeof(height),   d->plantHeightMm, d->heightValid);
  sdlog_fmtFloatGated(ppfd,     sizeof(ppfd),     d->ppfd,          d->ppfdValid,     1);
  sdlog_fmtFloatGated(powerMa,  sizeof(powerMa),  d->powerCurrentMa,d->powerValid,    1);
  sdlog_fmtFloatGated(powerMah, sizeof(powerMah), d->powerChargeMah,d->powerValid,    2);

  snprintf(out, outN,
    "%s,%s,%lu,%s,"
    "%s,%s,%s,%s,%s,%.2f,"
    "%s,%s,%.3f,%d,%.2f,%d,"
    "%s,%s,%d,%d",
    isoTimestamp, ntpValid ? "ntp" : "uptime", (unsigned long)uptimeMs, d->deviceStateName,
    airTemp, airHum, vpd, co2, leafTemp, d->waterTemp,
    height, ppfd, d->dli, d->tdsPpm, d->batteryVoltage, d->batteryPercent,
    powerMa, powerMah, d->lightsOn ? 1 : 0, d->pumpRunning ? 1 : 0);
}

#endif // SD_LOGGER_FORMAT_H
