/*=====================================================================
  schema_migration.h - Schema version constants and migration helpers

  Adds explicit schema_version tracking to LittleFS JSON stores so a
  future field rename / type change can run a one-time migration on boot
  instead of silently dropping unknown keys or — worse — misinterpreting
  legacy values.

  Versioning scheme: monotonically increasing uint8_t, starting at 1.
    SCHEMA_VERSION_*_CURRENT  = version this firmware writes
    SCHEMA_VERSION_*_MIN      = minimum readable version (older = wipe to defaults)

  Migration policy decisions (REL-1, 2026-05-15):
    - Missing "schema_version" key == version 0 (pre-versioning).
      v0 -> v1 is a no-op: existing fields keep their semantics.
    - Version > CURRENT (forward incompatibility) -> log warn, load with
      defaults to avoid acting on data this firmware can't interpret.
    - Version < MIN (long-obsolete) -> wipe to defaults.
=====================================================================*/

#ifndef SCHEMA_MIGRATION_H
#define SCHEMA_MIGRATION_H

#include <stdint.h>

// ── Current schema versions ─────────────────────────────────────
// Bump these when adding/renaming/removing a persistent field.
// v4 (18.7.2026): GrowPhaseParams — PE-tavoitteet (dli/ppfd/vpd/co2/lehti)
// korvasivat arvaus-parametrit (tempMin/Max, tdsTargetPpm). Vanha config
// tayttyy per-kasvi-defaulteilla, ei migratoi vanhoja lukuja (ne olivat
// arvauksia). docs/kehitys/pe-ohjausmalli.md §6.
// v5 (18.7.2026): DeviceConfig.devMode (bool). Additiivinen eteenpain —
// v4-tiedosto lataa devMode=false (config_store.h `| false`) ja rewrite
// v5:ksi (SCHEMA_NEEDS_MIGRATE). OTA-rollback: v4-firmware nakee v5-
// tiedoston forward-incompatible-tapauksena -> SCHEMA_RESET_DEFAULTS
// (sama politiikka kuin v3->v4-bumpissa yllä; devMode ei ole niin
// arvokas etta rollback-sailytys vaatisi bumpin valttamista).
#define SCHEMA_VERSION_CONFIG_CURRENT       5
#define SCHEMA_VERSION_CONFIG_MIN           1

// v3: added INA228 power config + cal factor
// v4 (20.7.2026): added ppfd_calibration_factor. Field existed in
// CalibrationData since the AS7341 rollout but was never round-tripped
// through calibration.json - PPFD/DLI stayed stuck at factor=1.0 (relative)
// because there was nowhere to persist a wizard-computed value.
// 28.7.2026: ppfd_geometry_factor added WITHOUT a version bump — deliberate.
// The field is purely additive and its default (1.0) is the identity, so both
// directions are safe without one: a v4 file loads geometry=1.0 via the `|`
// fallback, and an older firmware reading a file that contains the key simply
// ignores it. Bumping to v5 would have made an OTA rollback destructive —
// v4 firmware treats a v5 file as forward-incompatible (SCHEMA_RESET_DEFAULTS)
// and would wipe EVERY calibration, including meter-derived ones (PPFD factor,
// INA228 shunt correction). Losing just the geometry factor is the cheaper
// failure. Bump only when a change cannot be expressed as "new key + safe
// default" — e.g. a field whose meaning or unit changed.
#define SCHEMA_VERSION_CALIBRATION_CURRENT  4
#define SCHEMA_VERSION_CALIBRATION_MIN      1

#define SCHEMA_VERSION_PLANTS_CURRENT       1
#define SCHEMA_VERSION_PLANTS_MIN           1

// ── Decision returned by schema_decide() ────────────────────────
enum SchemaDecision {
  SCHEMA_OK_AS_IS       = 0,  // version matches CURRENT — load normally
  SCHEMA_NEEDS_MIGRATE  = 1,  // version < CURRENT but >= MIN — read + rewrite
  SCHEMA_RESET_DEFAULTS = 2   // version below MIN or above CURRENT — drop file
};

// Decide what to do with a loaded schema_version.
//
// fileVersion: value read from JSON, or 0 when key was missing.
// minVersion / currentVersion: per-file constants from this header.
//
// Returns the decision; callers apply it after the JSON has been parsed.
inline SchemaDecision schema_decide(uint8_t fileVersion,
                                    uint8_t minVersion,
                                    uint8_t currentVersion) {
  // Forward-incompatible: this firmware is older than the file. Don't risk
  // acting on fields whose meaning may have changed — reset to defaults
  // and let the user re-save through the portal.
  if (fileVersion > currentVersion) return SCHEMA_RESET_DEFAULTS;

  // Pre-versioning files read as 0. Treat 0 the same as v1 since v0 -> v1
  // is structurally identical — only the schema_version key was added.
  // This keeps a clean upgrade path for existing devices.
  if (fileVersion == 0 && minVersion <= 1) return SCHEMA_NEEDS_MIGRATE;

  if (fileVersion < minVersion) return SCHEMA_RESET_DEFAULTS;
  if (fileVersion < currentVersion) return SCHEMA_NEEDS_MIGRATE;

  return SCHEMA_OK_AS_IS;
}

#endif // SCHEMA_MIGRATION_H
