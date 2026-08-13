/*=====================================================================
  plant_lookup.h - In-memory plant defaults and lookup helpers

  Extracted from plant_database.h so lookup logic can be unit-tested
  without LittleFS/ArduinoJson dependencies.
=====================================================================*/

#ifndef PLANT_LOOKUP_H
#define PLANT_LOOKUP_H

#include "config.h"
#include "structs.h"

#include <string.h>

static PlantConfig g_plants[MAX_PLANTS];
static int g_plantCount = 0;

// PE-mukavuuskaistat per GrowPhaseType (M1, 11.8.2026). Ennen tama taulukko
// asui vain e-inkin puolella (status_targets.h PE_TARGETS_*) — tama on sama
// data, siirrettyna sinne missa vaiheen muutkin arvot (dli/ppfd/co2 target,
// vpd min/max) jo elivat. Sama kaikilla kasveilla, koska laji-kohtaista
// viritysta ei ole koskaan tehty (docs/kehitys/pe-ohjausmalli.md § 4) — siksi
// yksi taulukko GrowPhaseType:n mukaan riittaa eika addPhase()-kutsuja
// (32 kpl) tarvitse koskea. CUSTOM -> VEGETATIVE, sama fallback kuin e-inkin
// status_targetsForPhase() kaytti tuntemattomalle vaihenimelle.
struct PePhaseBand {
  float dliMin, dliMax;
  float ppfdMin, ppfdMax;
  float co2Min, co2Max;
};
static const PePhaseBand kPePhaseBands[5] = {
  /* GROW_PHASE_ROOTING    */ { 4.0f,  8.0f,  80.0f, 150.0f, 350.0f, 1500.0f },
  /* GROW_PHASE_SEEDLING   */ { 6.0f, 10.0f, 100.0f, 200.0f, 350.0f, 1500.0f },
  /* GROW_PHASE_VEGETATIVE */ {12.0f, 17.0f, 200.0f, 400.0f, 400.0f,  800.0f },
  /* GROW_PHASE_HARVEST    */ {12.0f, 15.0f, 200.0f, 350.0f, 350.0f, 1500.0f },
  /* GROW_PHASE_CUSTOM     */ {12.0f, 17.0f, 200.0f, 400.0f, 400.0f,  800.0f },  // = VEGETATIVE
};

// Built-in defaults (used if no plants.json on filesystem)
static void plants_buildDefaults(PlantConfig* arr, int* countOut) {
  int& count = *countOut;
  count = 0;

  auto add = [arr, &count](const char* id, const char* name, int height, int light,
                int waterMl, int waterH) {
    if (count >= MAX_PLANTS) return;
    PlantConfig& p = arr[count++];
    memset(&p, 0, sizeof(p));
    strncpy(p.id, id, sizeof(p.id) - 1);
    strncpy(p.name, name, sizeof(p.name) - 1);
    p.maxHeightMm        = height;
    p.lightHours         = light;
    p.waterMlPerDose     = waterMl;
    p.waterIntervalHours = waterH;
    p.phaseCount         = 0;
  };

  auto addPhase = [](PlantConfig& p, GrowPhaseType type, const char* label,
                     const char* guidance, uint16_t days, uint8_t light,
                     uint16_t floodMin, uint16_t floodSec, uint8_t dli,
                     uint16_t ppfd, float vpdMin, float vpdMax,
                     uint16_t co2, float leafMax) {
    if (p.phaseCount >= GROW_PHASE_MAX) return;
    GrowPhaseParams& gp = p.phases[p.phaseCount++];
    gp.type             = type;
    strncpy(gp.label, label, sizeof(gp.label) - 1);
    gp.label[sizeof(gp.label) - 1] = '\0';
    strncpy(gp.guidance, guidance ? guidance : "", sizeof(gp.guidance) - 1);
    gp.guidance[sizeof(gp.guidance) - 1] = '\0';
    gp.durationDays     = days;
    gp.lightHours       = light;
    gp.floodIntervalMin = floodMin;
    gp.floodDurationSec = floodSec;
    gp.dliTargetMol   = dli;
    gp.ppfdTargetUmol = ppfd;
    gp.vpdMinKpa      = vpdMin;
    gp.vpdMaxKpa      = vpdMax;
    gp.co2TargetPpm   = co2;
    gp.leafTempMaxC   = leafMax;

    const PePhaseBand& band = kPePhaseBands[(type <= GROW_PHASE_CUSTOM) ? type : GROW_PHASE_VEGETATIVE];
    gp.dliMinMol   = band.dliMin;   gp.dliMaxMol   = band.dliMax;
    gp.ppfdMinUmol = band.ppfdMin;  gp.ppfdMaxUmol = band.ppfdMax;
    gp.co2MinPpm   = band.co2Min;   gp.co2MaxPpm   = band.co2Max;
  };

  add("basil",    "Basilika",     400, 14, 25, 8);
  {
    PlantConfig& basil = arr[count - 1];
    addPhase(basil, GROW_PHASE_ROOTING, "Juurtuminen",
             "Leikkaa 10-15 cm varsi solmun alta. Pidä alusta kosteana ja tarkista juuret päivittäin.",
             7, 16, 0, 0, 6, 120, 0.4f, 0.8f, 400, 25.0f);
    // Teksti on tarkoituksella aloitustapa-neutraali: taimivaihe on siemenella
    // itamista (aloitustapa 1 alkaa taalta) mutta pistokkaalla juurten
    // vahvistumista. Molemmat tarvitsevat saman asian — kostea ja lammin — ja
    // molemmilla siirtyman merkki on sama: juuret nakyvat kuution pohjasta.
    // Aiempi teksti ("Juuret yli 2 cm: kaynnista tulva") oletti juuret jo
    // olevaksi ja oli siksi vaara juuri sille joka oli kylvanyt siemenen.
    addPhase(basil, GROW_PHASE_SEEDLING, "Taimivaihe",
             "Pidä kostea ja lämmin (20-25 C). Kun juuret näkyvät kuution pohjasta, valmis siirtoon.",
             14, 16, 360, 30, 8, 150, 0.4f, 0.8f, 400, 26.0f);
    addPhase(basil, GROW_PHASE_VEGETATIVE, "Kasvuvaihe",
             "Täysi kasvu: pidä ravinnevirtaus tasaisena ja seuraa lehtien väri sekä veden kulutus.",
             21, 16, 180, 45, 14, 300, 0.8f, 1.2f, 600, 28.0f);
    addPhase(basil, GROW_PHASE_HARVEST, "Sadonkorjuu",
             "Leikkaa latvat kahden solmun yläpuolelta. Toista sadonkorjuu 5-7 päivän välein.",
             0, 14, 180, 45, 13, 275, 1.0f, 1.4f, 400, 28.0f);
  }

  add("parsley",  "Persilja", 300, 12, 20, 12);
  {
    PlantConfig& parsley = arr[count - 1];
    addPhase(parsley, GROW_PHASE_ROOTING, "Juurtuminen",
             "Siemenet itävät hitaasti. Pidä alusta kosteana ja vältä kovaa tulvakastelua.",
             10, 14, 0, 0, 6, 120, 0.4f, 0.8f, 400, 25.0f);
    addPhase(parsley, GROW_PHASE_SEEDLING, "Taimivaihe",
             "Kun ensilehdet avautuvat, aloita mieto ravinne ja kevyt tulvakierto.",
             14, 14, 360, 30, 8, 150, 0.4f, 0.8f, 400, 26.0f);
    addPhase(parsley, GROW_PHASE_VEGETATIVE, "Kasvuvaihe",
             "Persilja kasvaa tasaisesti. Pidä valojakso pitkänä ja tarkkaile ravinnetasoa.",
             28, 14, 240, 40, 14, 300, 0.8f, 1.2f, 600, 28.0f);
    addPhase(parsley, GROW_PHASE_HARVEST, "Sadonkorjuu",
             "Leikkaa ulommat varret ensin. Jätä keskusta kasvamaan jatkuvaa satoa varten.",
             0, 12, 240, 40, 13, 275, 1.0f, 1.4f, 400, 28.0f);
  }

  add("dill", "Tilli", 500, 14, 20, 8);
  {
    PlantConfig& dill = arr[count - 1];
    addPhase(dill, GROW_PHASE_ROOTING, "Juurtuminen",
             "Tillin siemenet itävät nopeasti. Pidä pinta kosteana ja ilma kiertävänä.",
             7, 14, 0, 0, 6, 120, 0.4f, 0.8f, 400, 25.0f);
    addPhase(dill, GROW_PHASE_SEEDLING, "Taimivaihe",
             "Aloita lyhyt tulva kun juuret sitovat alustan. Lisää mieto ravinne.",
             10, 14, 360, 25, 8, 150, 0.4f, 0.8f, 400, 26.0f);
    addPhase(dill, GROW_PHASE_VEGETATIVE, "Kasvuvaihe",
             "Tilli venyy valossa. Pidä valojakso pitkänä ja estä alustan kuivuminen.",
             24, 14, 240, 35, 14, 300, 0.8f, 1.2f, 600, 28.0f);
    addPhase(dill, GROW_PHASE_HARVEST, "Sadonkorjuu",
             "Korjaa tilli nuorena. Leikkaa latvat usein, jotta kasvu pysyy rehevänä.",
             0, 12, 240, 35, 13, 275, 1.0f, 1.4f, 400, 28.0f);
  }

  add("mint", "Minttu", 350, 14, 30, 6);
  {
    PlantConfig& mint = arr[count - 1];
    addPhase(mint, GROW_PHASE_ROOTING, "Juurtuminen",
             "Minttu juurtuu helposti pistokkaasta. Pidä varsi kosteana ilman jatkuvaa tulvaa.",
             7, 14, 0, 0, 6, 120, 0.4f, 0.8f, 400, 25.0f);
    addPhase(mint, GROW_PHASE_SEEDLING, "Taimivaihe",
             "Kun juuria on hyvin, käynnistä tulvakierto ja lisää kevyt ravinne.",
             14, 14, 360, 30, 8, 150, 0.4f, 0.8f, 400, 26.0f);
    addPhase(mint, GROW_PHASE_VEGETATIVE, "Kasvuvaihe",
             "Minttu kuluttaa vettä nopeasti. Tarkista taso päivittäin ja pidä ravinne vakaana.",
             28, 14, 180, 40, 14, 300, 0.8f, 1.2f, 600, 28.0f);
    addPhase(mint, GROW_PHASE_HARVEST, "Sadonkorjuu",
             "Leikkaa pitkät versot takaisin. Jätä 2-3 lehtisolmua uutta kasvua varten.",
             0, 12, 180, 40, 13, 275, 1.0f, 1.4f, 400, 28.0f);
  }

  add("chives", "Ruohosipuli", 300, 12, 15, 12);
  {
    PlantConfig& chives = arr[count - 1];
    addPhase(chives, GROW_PHASE_ROOTING, "Juurtuminen",
             "Ruohosipuli itää rauhallisesti. Pidä alusta kosteana ja lämpö tasaisena.",
             10, 13, 0, 0, 6, 120, 0.4f, 0.8f, 400, 25.0f);
    addPhase(chives, GROW_PHASE_SEEDLING, "Taimivaihe",
             "Aloita mieto tulva vasta kun juuret kiinnittyvät kunnolla alustaan.",
             14, 13, 420, 25, 8, 150, 0.4f, 0.8f, 400, 26.0f);
    addPhase(chives, GROW_PHASE_VEGETATIVE, "Kasvuvaihe",
             "Kasvuvaiheessa ruohosipuli pitää viileähköstä valosta ja tasaisesta vedestä.",
             24, 13, 240, 35, 14, 300, 0.8f, 1.2f, 600, 28.0f);
    addPhase(chives, GROW_PHASE_HARVEST, "Sadonkorjuu",
             "Leikkaa varret 3-5 cm korkeudelta. Anna kasvin palautua ennen seuraavaa leikkuuta.",
             0, 12, 240, 35, 13, 275, 1.0f, 1.4f, 400, 28.0f);
  }

  add("cilantro", "Korianteri", 300, 12, 20, 8);
  {
    PlantConfig& cilantro = arr[count - 1];
    addPhase(cilantro, GROW_PHASE_ROOTING, "Juurtuminen",
             "Korianteri itää tasakosteassa alustassa. Vältä ylikastelua kasvun alussa.",
             7, 13, 0, 0, 6, 120, 0.4f, 0.8f, 400, 25.0f);
    addPhase(cilantro, GROW_PHASE_SEEDLING, "Taimivaihe",
             "Kun juuret vahvistuvat, aloita varovainen tulvakierto ja mieto ravinne.",
             10, 13, 360, 25, 8, 150, 0.4f, 0.8f, 400, 26.0f);
    addPhase(cilantro, GROW_PHASE_VEGETATIVE, "Kasvuvaihe",
             "Pidä lämpö maltillisena. Liian kuuma voi kiihdyttää kukintaa ennen aikojaan.",
             21, 13, 240, 35, 14, 300, 0.8f, 1.2f, 600, 28.0f);
    addPhase(cilantro, GROW_PHASE_HARVEST, "Sadonkorjuu",
             "Korjaa lehdet vaiheittain yläosasta. Poista kukkavarret mahdollisimman aikaisin.",
             0, 12, 240, 35, 13, 275, 1.0f, 1.4f, 400, 28.0f);
  }

  add("lettuce",  "Salaatti", 250, 12, 20, 10);
  {
    PlantConfig& lettuce = arr[count - 1];
    addPhase(lettuce, GROW_PHASE_ROOTING, "Juurtuminen",
             "Salaatti itää viileässä. Pidä alusta tasakosteana ilman tulvaa.",
             7, 12, 0, 0, 6, 120, 0.4f, 0.8f, 400, 25.0f);
    addPhase(lettuce, GROW_PHASE_SEEDLING, "Taimivaihe",
             "Kun juuret kiinnittyvät, aloita kevyt tulva ja mieto ravinne.",
             10, 12, 360, 30, 8, 150, 0.4f, 0.8f, 400, 26.0f);
    addPhase(lettuce, GROW_PHASE_VEGETATIVE, "Kasvuvaihe",
             "Salaatti kasvaa nopeasti viileässä valossa. Pidä taso vakaana.",
             21, 12, 240, 40, 14, 300, 0.8f, 1.2f, 600, 28.0f);
    addPhase(lettuce, GROW_PHASE_HARVEST, "Sadonkorjuu",
             "Korjaa ulommat lehdet ensin tai leikkaa koko ruusuke. Pidä viileänä.",
             0, 12, 240, 40, 13, 275, 1.0f, 1.4f, 400, 28.0f);
  }

  // Testikasvi: vapaat parametrit — kayttaja muokkaa portaalista (Kasvi-tabi).
  // Yksi GROW_PHASE_CUSTOM-vaihe oletusarvoilla; kaikki kentat muokattavissa
  // portaalista POST /api/plant ja POST /api/plant/phase -reiteilla.
  add("testikasvi", "Testikasvi", 400, 16, 25, 8);
  {
    PlantConfig& tp = arr[count - 1];
    addPhase(tp, GROW_PHASE_CUSTOM, "Testivaihe",
             "Muokkaa kaikkia asetuksia portaalista: Kasvi > Testikasvi > Muokkaa vaihetta.",
             14, 16, 120, 45, 12, 250, 0.8f, 1.2f, 500, 28.0f);
  }

}

static void plants_loadDefaults() {
  plants_buildDefaults(g_plants, &g_plantCount);
  DEBUG_PRINTF("[INFO]  Plants: loaded %d built-in defaults\n", g_plantCount);
}

inline int plants_getCount() { return g_plantCount; }

inline PlantConfig* plants_getById(const char* id) {
  for (int i = 0; i < g_plantCount; i++) {
    if (strcmp(g_plants[i].id, id) == 0) {
      return &g_plants[i];
    }
  }
  return NULL;
}

inline PlantConfig* plants_getByIndex(int idx) {
  if (idx < 0 || idx >= g_plantCount) return NULL;
  return &g_plants[idx];
}

// Jaettu raaputuspuskuri built-in-oletuksille. Kaksi funktiota tarvitsee
// saman taulukon: plants_fillMissingBands() (boot, plants_load-migraatio) ja
// plants_resetToDefaultById() (portaali). Oma static kummallekin maksaisi
// 2 x 16.9 KB pysyvaa .bss:aa — PlantConfig on 1084 B x MAX_PLANTS 16
// (mitattu 11.8.2026), eli lahes 10 % laitteen vapaasta heapista (178 kB)
// kahdesta harvoin kutsutusta funktiosta. Yksi puskuri riittaa: molemmat
// vain LUKEVAT taulukkoa, ja firmware on yksisaikeinen.
static PlantConfig g_defaultsScratch[MAX_PLANTS];
static int         g_defaultsScratchCount = 0;

// Rakenna oletukset kerran, palauta sen jalkeen valmis taulukko. Cachetus on
// turvallista koska plants_buildDefaults() kirjoittaa pelkkia compile-time-
// vakioita eika lue mitaan muuttuvaa tilaa — sama kutsu tuottaa aina saman
// tuloksen. Tama poistaa myos turhan uudelleenrakennuksen: ennen tata
// plants_fillMissingBands() rakensi koko 16 kasvin taulukon UUDELLEEN
// jokaiselle ladatulle kasville (16 kertaa bootissa).
static PlantConfig* plants_defaultsScratch(int* countOut) {
  if (g_defaultsScratchCount == 0) {
    plants_buildDefaults(g_defaultsScratch, &g_defaultsScratchCount);
  }
  *countOut = g_defaultsScratchCount;
  return g_defaultsScratch;
}

// M1 migration (11.8.2026): a plants.json written before this change has
// phase entries but no dli_min_mol/dli_max_mol/ppfd_min_umol/ppfd_max_umol/
// co2_min_ppm/co2_max_ppm keys (plant_database.h). ArduinoJson's `| 0.0f`
// leaves those at 0, which would classify every reading as PE_HIGH forever —
// so plants_load() calls this on every loaded plant to backfill from the
// matching built-in (id, phase.type). Every built-in id/type pair this
// device ships has an entry in kPePhaseBands, so the lookup always succeeds
// for a plant that predates this field; an id with no built-in match (should
// not exist — plants are fixed IDs, see wifi_portal_routes_post.h) is left at
// 0 and visible as a bug rather than silently patched with an invented number.
static void plants_fillMissingBands(PlantConfig* p) {
  int defCount = 0;
  PlantConfig* defs = plants_defaultsScratch(&defCount);
  for (uint8_t i = 0; i < p->phaseCount; i++) {
    GrowPhaseParams& gp = p->phases[i];
    if (gp.dliMinMol != 0.0f || gp.dliMaxMol != 0.0f) continue;  // already present
    for (int d = 0; d < defCount; d++) {
      if (strcmp(defs[d].id, p->id) != 0) continue;
      for (uint8_t j = 0; j < defs[d].phaseCount; j++) {
        if (defs[d].phases[j].type != gp.type) continue;
        gp.dliMinMol   = defs[d].phases[j].dliMinMol;
        gp.dliMaxMol   = defs[d].phases[j].dliMaxMol;
        gp.ppfdMinUmol = defs[d].phases[j].ppfdMinUmol;
        gp.ppfdMaxUmol = defs[d].phases[j].ppfdMaxUmol;
        gp.co2MinPpm   = defs[d].phases[j].co2MinPpm;
        gp.co2MaxPpm   = defs[d].phases[j].co2MaxPpm;
        break;
      }
      break;
    }
  }
}

// Reset one plant's parameters AND phases to its built-in default. Rebuilds the
// defaults into a scratch buffer and copies the matching entry over the live
// plant. Returns false if `id` is not a built-in plant (a user-added custom
// plant has no factory default to restore). Caller persists via plants_save().
static bool plants_resetToDefaultById(const char* id) {
  int n = 0;
  PlantConfig* defs = plants_defaultsScratch(&n);
  for (int i = 0; i < n; i++) {
    if (strcmp(defs[i].id, id) == 0) {
      PlantConfig* dst = plants_getById(id);
      if (!dst) return false;
      *dst = defs[i];
      return true;
    }
  }
  return false;
}

#endif // PLANT_LOOKUP_H
