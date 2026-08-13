/*=====================================================================
  status_targets.h - PE-tavoitemalli tilannenaytolle (SEURANTA)

  Puhdas, laitteistoriippumaton logiikka (natiivitestattava,
  test/test_status_targets): kasvuvaiheen PE-tavoitteet (tavoite vs. nykyarvo)
  + luokittelu LOW/OK/HIGH + valmennusviesti per mitta ja suunta.

  Jako HOIDAN (laite saataa itse) vs AUTA (kayttaja toimii) tulee pe-ohjausmalli.md
  §3:sta: laite voi ajaa valoa PAALLE/POIS aikataulun mukaan ja tuuletinta/tulvaa,
  muttei sulje PPFD- tai DLI-silmukkaa (lampun korkeus ja valoaika ovat kasin
  saadettavia V1:ssa - kasvatus-kasikirjoitus-basilika.md §0.2). `deviceActs` koodaa
  taman: true=HOIDAN, false=AUTA. DLI ja PPFD olivat aiemmin virheellisesti HOIDAN
  ("saadan automaattisesti") - korjattu 22.7.2026 kayttajan rautahavainnon jalkeen
  (teksti lupasi jotain mita laite ei tee).

  Tavoitearvot olivat basilikan per-vaihe-oletuksia (pe-ohjausmalli.md §4,
  hortikulttuuriviite) — nyt PM:n /api/state:sta, ks. M1-huomautus alla.
  status_classify pysyi samana koko siirron ajan.

  ── SKANDIT: naissa viesteissa kirjoitetaan OIKEAA UTF-8:aa ('a'), EI \xE4 ──
  Kaikki tama teksti piirretaan status_view.h:n status_u8*()-funktioilla, jotka
  ajavat sen utf8ToLatin1():n lapi piirtorajalla. Valmis Latin-1-tavu (\xE4)
  nayttaisi siella katkenneelta 3-tavuiselta UTF-8-sekvenssilta ja merkki
  KATOAISI HILJAA (utf8_latin1.h:86-88). Vastakkainen saanto patee
  layout_def.h:n § 2 -teksteihin, jotka menevat display.print():iin raakana —
  ne kirjoitetaan \xE4-tavuina. Saanto maaraytyy PIIRTOKUTSUSTA, ei tiedostosta;
  koko taulukko: docs/ohjeet/eink_operointi.md "Skandit".

  Historia: nama olivat ASCII:na 19.7.-27.7.2026, koska silloin luultiin ettei
  GxEPD2 piirra 0xE4-tavua. Se kumottiin raudalla 21.7.2026 (GDEY075T7) mutta
  vain layout_def.h korjattiin — ASCII jai tanne kahdeksi viikoksi. Konventio on
  nyt testattu mekaanisesti (test_layout_def_tables), jotta se ei toistu.

  M1 (11.8.2026, docs/kehitys/Fable_kehityspolku.md § M1): per-vaihe PE_TARGETS_*-
  taulukot POISTETTU tasta. Ne olivat kasin yllapidetty kaksoiskappale
  PlantMeisterin GrowPhaseParamsista (structs.h, plant_lookup.h kPePhaseBands) —
  ja ehtivat jo ajautua eri lukemiin ennen tata siirtoa. Nyt aktiivisen vaiheen
  kaistat tulevat PM:n /api/state growing.targets[] -kentasta (data_fetch_parse.h
  parsii DisplayData.peTargetsiin), ja status_targetsForData() lukee ne sielta.
  PE_TARGETS_FALLBACK jaa ainoaksi paikalliseksi taulukoksi — kaytossa vain kun
  laite ei julkaissut kaistoja (vanha firmware ilman tata kenttaa, tai ei
  aktiivista kasvatusvaihetta), sama tilanne jossa vanha status_targetsForPhase()
  palautti PE_TARGETS_VEGETATIVE:n tuntemattomalle vaihenimelle.
=====================================================================*/

#ifndef STATUS_TARGETS_H
#define STATUS_TARGETS_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "display_data.h"
#include "layout_def.h"   // FieldId (FIELD_VPD, ...), TXT_STATUS_TARGET_*

// PE_NO_LIMIT: display_data.h (jaettu sentinel PM:n /api/state-kontraktin
// kanssa, ks. sen tiedoston kommentti).

enum PeStatus { PE_NODATA = 0, PE_LOW, PE_OK, PE_HIGH };

struct PeTarget {
  uint8_t field;       // FieldId
  float   lo;          // PE_NO_LIMIT = ei ala-rajaa
  float   hi;          // PE_NO_LIMIT = ei yla-rajaa
  bool    deviceActs;  // true = laite saataa (HOIDAN), false = kayttaja (AUTA)
};

// Ainoa jaljella oleva paikallinen taulukko (M1, ks. tiedoston yla-kommentti):
// kasvuvaiheen "Kasvuvaihe"-luvut sellaisenaan, kaytossa VAIN kun laite ei
// julkaissut kaistoja lainkaan. Jarjestys sopii korttinakymaan: kortit
// ottavat 4 ensimmaista kelvollista (VPD, DLI, CO2, LEAF); PPFD jaa vain
// valmennusriveille (pe-ohjausmalli §3.1).
static const PeTarget PE_TARGETS_FALLBACK[] = {
  { FIELD_VPD,        0.8f,  1.2f,  false },
  { FIELD_DLI,       12.0f, 17.0f,  false },
  { FIELD_CO2,      400.0f, 800.0f, false },
  { FIELD_LEAF_TEMP, PE_NO_LIMIT, 28.0f, false },
  { FIELD_PPFD,     200.0f, 400.0f, false },
};
#define PE_TARGET_FALLBACK_COUNT 5

// Wire-avain (PM:n /api/state growing.targets[].field, wifi_portal_api_state.h)
// -> FieldId (layout_def.h). 0xFF = tuntematon avain (esim. tulevan firmwaren
// uusi mitta jota tama versio ei viela tunne — ohitetaan, ei kaadeta).
static uint8_t status_fieldIdForKey(const char* key) {
  if (!key) return 0xFF;
  if (strcmp(key, "vpd")  == 0) return FIELD_VPD;
  if (strcmp(key, "dli")  == 0) return FIELD_DLI;
  if (strcmp(key, "co2")  == 0) return FIELD_CO2;
  if (strcmp(key, "leaf") == 0) return FIELD_LEAF_TEMP;
  if (strcmp(key, "ppfd") == 0) return FIELD_PPFD;
  return 0xFF;
}

// Aktiivisen vaiheen PE-tavoitteet: PM:n /api/state:sta (DisplayData.peTargets,
// data_fetch_parse.h) muunnettuna FieldId-muotoon, tai PE_TARGETS_FALLBACK jos
// laite ei julkaissut mitaan (vanha firmware, tai ei aktiivista vaihetta).
// Palauttaa osoittimen funktion staattiseen puskuriin — yksisaikeinen
// firmware, turvallinen, virkistyy joka kutsulla (sama malli kuin vanha
// status_targetsForPhase() palautti osoittimen const-taulukkoon).
static const PeTarget* status_targetsForData(const DisplayData* d, int* count) {
  static PeTarget buf[DISPLAY_PE_TARGET_MAX];
  if (!d || d->peTargetCount == 0) {
    *count = PE_TARGET_FALLBACK_COUNT;
    return PE_TARGETS_FALLBACK;
  }
  int n = 0;
  for (int i = 0; i < d->peTargetCount && i < DISPLAY_PE_TARGET_MAX; i++) {
    uint8_t fid = status_fieldIdForKey(d->peTargets[i].field);
    if (fid == 0xFF) continue;
    buf[n].field      = fid;
    buf[n].lo         = d->peTargets[i].lo;
    buf[n].hi         = d->peTargets[i].hi;
    buf[n].deviceActs = d->peTargets[i].deviceActs;
    n++;
  }
  if (n == 0) {
    *count = PE_TARGET_FALLBACK_COUNT;
    return PE_TARGETS_FALLBACK;
  }
  *count = n;
  return buf;
}

// Lue mitan numeerinen nykyarvo DisplayDatasta. false jos anturi ei validi
// (peilaa resolve_field.h:n validiteettiehtoja naille kentille).
static bool status_fieldValue(uint8_t field, const DisplayData* d, float* out) {
  switch (field) {
    case FIELD_VPD:       if (!d->vpdValid) return false; *out = d->vpdKpa; return true;
    case FIELD_DLI:       if (!(d->ppfdValid || d->dli > 0.0f)) return false; *out = d->dli; return true;
    case FIELD_PPFD:      if (!d->ppfdValid) return false; *out = d->ppfd; return true;
    case FIELD_CO2:       if (!d->co2Valid) return false; *out = (float)d->co2Ppm; return true;
    case FIELD_LEAF_TEMP: if (!d->leafTempValid) return false; *out = d->leafTempC; return true;
    default: return false;
  }
}

static PeStatus status_classify(float v, const PeTarget* t) {
  if (t->lo != PE_NO_LIMIT && v < t->lo) return PE_LOW;
  if (t->hi != PE_NO_LIMIT && v > t->hi) return PE_HIGH;
  return PE_OK;
}

// Yhdistetty: lue + luokittele. Palauttaa PE_NODATA jos anturi ei validi.
static PeStatus status_evalTarget(const PeTarget* t, const DisplayData* d, float* value) {
  float v;
  if (!status_fieldValue(t->field, d, &v)) return PE_NODATA;
  if (value) *value = v;
  // Saturoitunut PPFD on alaraja: sita ei saa luokitella PE_LOW:ksi, koska
  // todellinen arvo voi olla moninkertainen. Alaraja voi silti todistaa
  // ylityksen, joten PE_HIGH kelpaa — se on paatelma jonka alaraja kantaa.
  if (t->field == FIELD_PPFD && d->ppfdSaturated) {
    const PeStatus s = status_classify(v, t);
    return (s == PE_LOW) ? PE_NODATA : s;
  }
  return status_classify(v, t);
}

// Valmennusviesti mitalle + suunnalle. otsikko = lyhyt tila, keho = ohje.
// deviceActs-riveilla (HOIDAN) keho on 1. persoonassa ("nostan lamppua"),
// AUTA-riveilla imperatiivi kayttajalle. Skandit kirjoitetaan UTF-8:na
// (headerin saanto) — kommentit sen sijaan ASCII:na kuten muualla tassa
// tiedostossa. PE_OK/PE_NODATA -> tyhjat (ei rivia).
static void status_coachMessage(uint8_t field, PeStatus st,
                                const char** title, const char** body) {
  *title = "";
  *body = "";
  switch (field) {
    case FIELD_VPD:
      if (st == PE_HIGH)     { *title = "Ilma kuiva";  *body = "suihkuta vettä ja varjosta paahteelta, tuuletin avustaa."; }
      else if (st == PE_LOW) { *title = "Ilma kostea"; *body = "tuuleta tai lisää ilmavirtaa, tuuletin avustaa."; }
      return;
    case FIELD_CO2:
      if (st == PE_LOW)       { *title = "CO2 matala"; *body = "tuuleta huone tai lisää kasveja."; }
      else if (st == PE_HIGH) { *title = "CO2 korkea"; *body = "tuuleta huone."; }
      return;
    case FIELD_LEAF_TEMP:
      if (st == PE_HIGH) { *title = "Lehti kuuma"; *body = "varjosta latvusto, tuuletin jäähdyttää."; }
      return;
    // Sanamuoto on ENNUSTE, ei nykytilan toteamus: status_evalTargetTimeAware
    // projisoi vuorokauden lopun nykytahdista (dli_expectation.h), joten
    // "jaa vajaaksi" / "kertyy liikaa" puhuu paivan lopputuloksesta. Vanha
    // "Valosumma matala" luki kertymaa sellaisenaan ja syytti aamua siita
    // ettei se ole ilta. Ei myoskaan nimea lamppua — sama syy kuin PPFD:ssa.
    case FIELD_DLI:
      if (st == PE_LOW)       { *title = "Valoa jää vajaaksi";  *body = "lisää valoaikaa tai siirrä valoisampaan."; }
      else if (st == PE_HIGH) { *title = "Valoa kertyy liikaa"; *body = "varjosta harsolla tai siirrä puolivarjoon."; }
      return;
    // Sanamuoto EI saa nimetä lamppua: laite ei ohjaa valonlähdettä eikä tiedä
    // kumpi se on. 29.7.2026 kasvit olivat parvekkeella suorassa auringossa ja
    // näyttö neuvoi "nosta lamppua kauemmas" — toimenpide jota ei ollut
    // olemassa. Varjostus (harso, kupu) toimii molemmilla valonlähteillä.
    case FIELD_PPFD:
      if (st == PE_LOW)       { *title = "Valoteho heikko";   *body = "laske lamppua lähemmäs tai siirrä valoisampaan."; }
      else if (st == PE_HIGH) { *title = "Valoteho voimakas"; *body = "varjosta harsolla tai siirrä pois suorasta valosta."; }
      return;
    default:
      return;
  }
}

// Tavoiteikkuna luettavaksi merkkijonoksi: "tav 0.4-0.8 kPa" / "tav <26 C".
// Piirretaan valmennusrivin oikeaan reunaan (status_view.h), jotta kortin
// nykyarvo ja tavoite ovat vertailukelpoisia — ilman tata neuvo "Ilma kuiva"
// ei kerro MIHIN pyritaan (kayttajan havainto 27.7.2026: RH 61 % nayttaa
// arkijarjella kostealta, vaikka VPD 1.03 kPa on taimelle liian kuiva).
// Yksikot ja desimaalit peilaavat resolve_field.h:ta tarkoituksella.
// Kirjoittaa aina nollatermioidun merkkijonon; tuntematon kentta -> "".
static void status_formatTarget(const PeTarget* t, char* out, size_t n) {
  if (!out || n == 0) return;
  out[0] = '\0';
  if (!t) return;

  const char* unit;
  int decimals;
  switch (t->field) {
    case FIELD_VPD:       unit = "kPa";  decimals = 1; break;
    case FIELD_DLI:       unit = "mol";  decimals = 0; break;
    case FIELD_CO2:       unit = "ppm";  decimals = 0; break;
    case FIELD_PPFD:      unit = "umol"; decimals = 0; break;
    case FIELD_LEAF_TEMP: unit = "C";    decimals = 0; break;
    default: return;
  }

  // Pelkka katto (esim. lehtilampo) -> "tav <28 C"; muuten alue "tav lo-hi".
  if (t->lo == PE_NO_LIMIT && t->hi != PE_NO_LIMIT) {
    snprintf(out, n, "%s%.*f %s", TXT_STATUS_TARGET_MAXPREF, decimals, (double)t->hi, unit);
  } else if (t->lo != PE_NO_LIMIT && t->hi != PE_NO_LIMIT) {
    snprintf(out, n, "%s%.*f-%.*f %s", TXT_STATUS_TARGET_PREF,
             decimals, (double)t->lo, decimals, (double)t->hi, unit);
  } else if (t->lo != PE_NO_LIMIT) {
    snprintf(out, n, "%s%.*f %s", TXT_STATUS_TARGET_PREF, decimals, (double)t->lo, unit);
  }
}

// DLI (vuorokausisumma) ja PPFD (hetkellinen teho) kertovat molemmat samasta
// oireesta - liian vahan tai liikaa valoa. Molempien nayttaminen samaan suuntaan
// poikkeavina toistaisi saman neuvon kahdesti (kayttajan rautahavainto
// 22.7.2026: "valosumma matala" -viesti kahdesti samalla ruudulla), joten
// toinen piilotetaan.
//
// KUMPI PIILOTETAAN: PPFD. Aiemmin (19.7.-28.7.2026) piilotettiin DLI, koska
// PPFD on valittomampi signaali. Se oli oikea paatos saadon kannalta mutta
// vaara nayton kannalta: korttirivilla nakyy DLI (mol), joten PPFD-rivin
// tavoite ("tav 100-200 umol") luettiin DLI-kortin tavoitteeksi ja kortin luku
// 0.2 nayttti olevan 500x pielessa (kayttajan havainto 28.7.2026). Neuvon
// yksikon TAYTYY olla sama kuin sen kortin, jota kayttaja katsoo. DLI:n oma
// neuvo ("pidenna valoaikaa tai laske lamppua") kattaa saman toimenpiteen.
//
// PPFD nakyy edelleen numerona konteksti-rivilla yksikkoineen, joten tieto ei
// katoa - vain paallekkainen neuvo.
static inline bool status_ppfdRedundantWithDli(PeStatus ppfdStatus, PeStatus dliStatus) {
  if (ppfdStatus == PE_NODATA || ppfdStatus == PE_OK) return false;
  return ppfdStatus == dliStatus;
}

// Vastakkaiset suunnat: PPFD liian korkea mutta DLI liian matala. Tama on
// TOSI ja tavallinen tilanne (kirkas mutta lyhyt paiva, tai kesken paivaa
// katsottu vuorokausisumma), mutta neuvot ovat toimintoina ristiriitaiset:
// "varjosta harsolla" ja "laske lamppua" samalla ruudulla. Havaittu
// 29.7.2026: PPFD 795 umol taimelle (tav 100-200) ja DLI 4,4 mol (tav 6-10)
// klo 13, eli summa oli vasta kertymassa.
//
// DLI-rivi piilotetaan, ei PPFD-rivia: liika valoteho voi vahingoittaa kasvia
// tunneissa, kun taas vajaa vuorokausisumma korjaantuu itsestaan paivan
// mittaan. Kiireellisempi ja peruuttamattomampi voittaa.
static inline bool status_dliContradictsPpfd(PeStatus dliStatus, PeStatus ppfdStatus) {
  return dliStatus == PE_LOW && ppfdStatus == PE_HIGH;
}

#endif // STATUS_TARGETS_H
