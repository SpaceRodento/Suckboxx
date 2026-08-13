/*=====================================================================
  view_profiles.h - E-ink display profile selector

  Selects which renderer the e-ink seinataulu uses for the active view.
  Profile is chosen at compile time via EINK_VIEW_PROFILE in config.h.

  Profiles:
    EINK_VIEW_USER          - tuotantonakyma (display_layout.h)
    EINK_VIEW_DEV_DASHBOARD - tiivis diagnostiikka (dev_view.h)
    EINK_VIEW_RAW_TABLE     - taulukkomuotoinen raakadata (dev_table_view.h)
    EINK_VIEW_COACH         - kasvivalmentaja: neuvontabanneri + 6 PE-korttia (coach_view.h)

  Adding a new profile: bump the enum, add a case to view_profile_render,
  add the header include, and document the layout in the renderer's
  header. No changes needed in .ino.

  Switching to COACH for a rautasessio: set EINK_VIEW_PROFILE 3 in config.h,
  flash USB (docs/ohjeet/eink_operointi.md § 2 + § 11), boot log confirms
  "View profile: COACH".
=====================================================================*/

#ifndef VIEW_PROFILES_H
#define VIEW_PROFILES_H

#include "config.h"
#include "data_fetch.h"

#define EINK_VIEW_USER          0
#define EINK_VIEW_DEV_DASHBOARD 1
#define EINK_VIEW_RAW_TABLE     2
#define EINK_VIEW_COACH         3
#define EINK_VIEW_STATUS        4

// EINK_VIEW_PROFILE is the source of truth. config.h always sets it.
// The legacy ENABLE_EINK_DEV_VIEW flag remains in config.h for back-compat
// but is no longer consulted here — to switch profiles, change
// EINK_VIEW_PROFILE in config.h.
#ifndef EINK_VIEW_PROFILE
  #define EINK_VIEW_PROFILE EINK_VIEW_USER
#endif

// Renderer headers — each profile owns a single render entry point.
#include "display_layout.h"
#include "dev_view.h"
#include "dev_table_view.h"
#include "coach_view.h"
#include "status_view.h"      // tilannenaytto (SEURANTA) — pe-ohjausmalli V1
#include "onboarding_view.h"  // overlay: overrides every profile while PM onboards

static const char* view_profile_name() {
  switch (EINK_VIEW_PROFILE) {
    case EINK_VIEW_USER:          return "USER";
    case EINK_VIEW_DEV_DASHBOARD: return "DEV_DASHBOARD";
    case EINK_VIEW_RAW_TABLE:     return "RAW_TABLE";
    case EINK_VIEW_COACH:         return "COACH";
    case EINK_VIEW_STATUS:        return "STATUS";
    default:                      return "UNKNOWN";
  }
}

static void view_profile_render(const DisplayData* d) {
  // Kayttoonotto kesken (PM device.onboarding) → ohita profiili ja nayta
  // liity-AP / avaa-osoite -ohje. Poistuu itsestaan kun onboarding valmistuu.
  if (onboarding_view_active(d)) {
    onboarding_view_render(d);
    return;
  }
  switch (EINK_VIEW_PROFILE) {
    case EINK_VIEW_DEV_DASHBOARD:
      dev_view_render(d);
      break;
    case EINK_VIEW_RAW_TABLE:
      dev_table_view_render(d);
      break;
    case EINK_VIEW_COACH:
      coach_view_render(d);
      break;
    case EINK_VIEW_STATUS:
      status_view_render(d);
      break;
    case EINK_VIEW_USER:
    default:
      display_render(d);
      break;
  }
}

// Change signal for render-on-change (.ino decides skip vs render).
// USER and COACH (production-quality profiles) hash only their visible
// content. The dev profiles show live diagnostics (uptime, fetch duration)
// that change every cycle, so they return an ever-changing value → always
// render, keeping diagnostics fresh.
static uint32_t view_profile_contentHash(const DisplayData* d) {
  if (onboarding_view_active(d)) return onboarding_view_contentHash(d);
#if EINK_VIEW_PROFILE == EINK_VIEW_USER
  return display_contentHash(d);
#elif EINK_VIEW_PROFILE == EINK_VIEW_COACH
  return coach_view_contentHash(d);
#elif EINK_VIEW_PROFILE == EINK_VIEW_STATUS
  return status_view_contentHash(d);
#else
  (void)d;
  static uint32_t tick = 0;
  return ++tick;
#endif
}

#endif // VIEW_PROFILES_H
