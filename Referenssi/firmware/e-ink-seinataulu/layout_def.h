/*=====================================================================
  layout_def.h - E-ink LAYOUT (the file you edit to tweak the screen)

  ★ TÄTÄ TIEDOSTOA EDITOIMALLA muutat näytön ulkoasun: tekstit, etäisyydet
    ja sen MITKÄ MITTARIT näkyvät korteilla / konteksti-rivillä.
    Tämä on pelkkää dataa — ei piirtologiikkaa. Arvon muotoilu ja validiteetti
    elävät resolveField():ssä (display_layout.h).

  ★ NÄET MUUTOKSEN ILMAN FLASHIA: aja  python scripts/eink_preview.py
    (lukee tämän saman tiedoston + hakee /api/state → renderöi preview.png).

  ★ KÄYTETTÄVISSÄ OLEVAT MUUTTUJAT (FieldId-tokenit, hyödyllisyysjärjestys):
    docs/arkisto/kehitys/eink-muuttujat.md

  Geometria viittaa config.h:n DISPLAY_WIDTH/HEIGHT-arvoihin → include config.h.
=====================================================================*/

#ifndef LAYOUT_DEF_H
#define LAYOUT_DEF_H

#include "config.h"   // DISPLAY_WIDTH (800), DISPLAY_HEIGHT (480)

// ═══════════════════════════════════════════════════════════════════
// 1. GEOMETRIA — säädä etäisyyksiä tästä (pikseleinä, 800x480)
//    eink_preview.py parsii nämä #define-rivit suoraan.
// ═══════════════════════════════════════════════════════════════════

#define MARGIN          20
#define DIVIDER_X       370     // Pystyviiva (~45% vasen kasvialue)

// Vasen sarake (kasvigrafiikka)
#define PLANT_X         MARGIN
#define PLANT_W         (DIVIDER_X - PLANT_X - 10)

// Oikea sarake (data)
#define DATA_X          (DIVIDER_X + 10)
#define DATA_W          (DISPLAY_WIDTH - DATA_X - MARGIN)

// Y-paikat (ylhäältä alas)
#define HEADER_Y        45      // Kello + päivämäärä
#define PLANT_NAME_Y    100
#define CARDS_Y         66      // PE-kortit heti kellon/päivämäärän alla
#define CARD_W          ((DATA_W - CARD_GAP) / 2)
#define CARD_H          110
#define CARD_GAP        20      // vaakaväli korttien välissä
#define CARD_ROW_GAP    15      // pystyväli korttirivien välissä
#define STATUS_Y        380     // "Toiminta"-rivi (tärkein elävä signaali)
#define CONTEXT_Y       408     // Konteksti-rivi (ilma/kosteus/korkeus/akku)
#define SYSTEM_Y        432     // Vaihe/valo/vesi-rivi
#define FOOTER_Y        465     // Käyntiaika + PlantMeister-leima

// ─── COACH-profiili — SAMA geometria ja SAMAT kortit kuin USER:lla (isot
//     24pt-kortit, iso kasvigrafiikka), yksi lisäys: neuvontabanneri joka
//     istuu korttiruudukon ja "Toiminta"-jakoviivan väliin jäävään tyhjään
//     tilaan (CARDS_Y + 2*CARD_H + CARD_ROW_GAP = 301 ... STATUS_Y-30 = 350,
//     49 px vapaana). Ei erillistä sarakejakoa, ei omia fontteja — katso
//     coach_view.h, joka piirtää muuten identtisesti display_render():n
//     kanssa. ──────────────────────────────────────────────────────────
#define COACH_ADVISORY_TOP   (CARDS_Y + 2 * CARD_H + CARD_ROW_GAP + 6)  // 307
#define COACH_ADVISORY_H     34                                         // pohja 341, jakoviiva 350

// ─── ASKEL-LAATIKKO (COACH) — iso ohjeteksti kun growing.step_count > 0.
//     Korvaa 2x2-kortit JA advisory-bannerin (kayttajan valinta 16.7.2026:
//     "tekstilaatikko saa tayttaa kaiken tilan minka mittarit veivat").
//     Muu sivu (otsake, Toiminta, konteksti, footer, kasvigrafiikka) sailyy.
//     Piirto: coach_view.h coach_drawStepBox; rivitys text_wrap.h. ─────────
#define STEP_BOX_Y        CARDS_Y                          // 66
#define STEP_BOX_H        (STATUS_Y - 30 - STEP_BOX_Y - 4) // pohja jakoviivan ylle (280 px)
#define STEP_BOX_PAD      14      // sisareunus joka suuntaan
#define STEP_HEADER_H     34      // "Askel 1/3" + ajastin -rivi laatikon sisalla
#define STEP_FOOTER_H     26      // kuittausohje laatikon alareunassa
#define STEP_LINE_GAP     3       // lisapikselit fontin yAdvancen paalle
// Ajastimen naytettava tarkkuus: pyoristys ylospain lahimpaan 10 min:iin.
// Karkea tahallaan — naytto paivittyy 10 min valein tuotannossa, ja
// minuutintarkka luku renderoisi paneelin joka syklilla (render-on-change
// hajautuu naytettavasta merkkijonosta, ks. coach_view.h).
#define STEP_TIMER_GRANULARITY_MIN  10

// ═══════════════════════════════════════════════════════════════════
// 2. TEKSTIT — vaihda sanamuotoja tästä. %d/%s ovat pakollisia muotoilijoita.
//
//    SKANDIT: kirjoita SUORINA Latin-1-tavuina — ä=\xE4  ö=\xF6  å=\xE5
//    Ä=\xC4  Ö=\xD6  Å=\xC5. Esim. "k\xE4ynniss\xE4" = "käynnissä".
//    Lisää selkokielinen sana kommenttiin rivin perään.
//
//    MIKSI \x-tavu eikä UTF-8-"ä": nämä literaalit menevät SUORAAN
//    display.print():iin eivätkä kulje data_fetch_parse.h:n UTF-8 -> Latin-1
//    -muunnoksen läpi (se koskee vain /api/state-dataa). GxEPD2 piirtää
//    Latin-1-tavun oikein — todistettu raudalla 21.7.2026 (GDEY075T7). \xE4
//    ON jo valmis Latin-1-tavu -> ei muunnosta, ei tuplamuunnosriskiä.
//    UTF-8-"ä" olisi kaksi tavua (0xC3 0xA4) -> piirtyisi "Ã¤".
//
//    Muunnosraja pysyy yhdessä paikassa (data_fetch_parse.h) API-datalle.
//    ÄLÄ lisää muunnosta tänne: jo-muunnettu Latin-1-tavu + toinen muunnos
//    = merkki katoaa (0xE4 näyttää katkenneelta UTF-8-sekvenssiltä).
// ═══════════════════════════════════════════════════════════════════

#define TXT_WORDMARK        "PlantMeister"     // oikean alakulman leima
#define TXT_SD_ADVISORY     "SD-kortti: ei tallenna"  // korvaa leiman kun ENABLE_SD_LOGGING mutta !sdlog_isReady()
#define TXT_WAITING         "Odotetaan dataa gatewaylta..."
#define TXT_SHOWROOM_PLANT  "Basilika"          // näytetään kun dataa ei vielä ole

// "Toiminta"-rivi (formatActivity)
#define TXT_ACT_LABEL       "Toiminta: "
#define TXT_ACT_FAULT       "Vesivika!"
#define TXT_ACT_CIRCULATE   "Kierto k\xE4ynniss\xE4"    // Kierto käynnissä
#define TXT_ACT_FLOOD       "Tulvitus k\xE4ynniss\xE4"  // Tulvitus käynnissä
#define TXT_ACT_SOAK        "Liotus k\xE4ynniss\xE4"    // Liotus käynnissä
#define TXT_ACT_DRAIN       "Allas tyhjenee"
#define TXT_ACT_IDLE        "Valmiustila"       // ei kasvatusta käynnissä
#define TXT_ACT_FLOOD_SOON  "Tulva pian"
#define TXT_ACT_NEXT_FLOOD  "Seuraava tulva ~%d min"
#define TXT_ACT_RESTING     "Lep\xE4\xE4"             // kasvaa mutta ei tulvitusta aikataulussa
#define TXT_ACT_GROWING     "Kasvatus k\xE4ynniss\xE4" // NFT/oletus: jatkuva, ei tulva-aikataulua

// Vaihe/valo/vesi-rivi (SYSTEM_Y)
#define TXT_SYS_GROWING     "Vaihe: %s (%d pv)   Valo: %s   Vesi: %s"
#define TXT_SYS_IDLE        "Valo: %s   Vesi: %s"
// Askel kaynnissa: vaihe kehystetaan aloitustavan mukaan eika naytata "N pv"
// (paivalaskuri on harhaanjohtava kesken idatyksen/juurtumisen).
#define TXT_SYS_STEP          "Vaihe: %s   Valo: %s   Vesi: %s"
#define TXT_PHASE_GERMINATING "Id\xE4tys k\xE4ynniss\xE4"
#define TXT_PHASE_ROOTING     "Juurtuminen k\xE4ynniss\xE4"
#define TXT_ON              "ON"
#define TXT_OFF             "OFF"
#define TXT_WATER_OK        "OK"
#define TXT_WATER_LOW       "MATALA"

// Käyntiaika-footer (FOOTER_Y) — yksikön lyhenteet
#define TXT_UPTIME_PREFIX   "K\xE4ynniss\xE4: "

// Neuvontabanneri (COACH-profiili, resolve_advisory.h) — yksi lause,
// prioriteettijärjestyksessä: FAULT > vesi > kuumuus > VPD > valo > OK.
// Kynnysarvot (COACH_VPD_COMFORT_*_KPA, COACH_PPFD_*_UMOL, COACH_AIR_TEMP_MAX_C)
// ovat config.h:ssa.
//
// Jokainen teksti nimeää TOIMENPITEEN, ei pelkkää oiretta. "Ilma liian kuiva"
// yksinään ei kerro käyttäjälle mitä tehdä (havainto 29.7.2026) — banneri on
// yhden rivin mittainen juuri siksi että siihen mahtuu vain tärkein teko.
#define TXT_ADV_FAULT        "VIKA: %s"
#define TXT_ADV_FAULT_GENERIC "VIKA - tarkista laite"
#define TXT_ADV_WATER_LOW    "Vesi matalalla - tarkista t\xE4ytt\xF6"
#define TXT_ADV_MAINTENANCE  "HUOLTOTILA - pumppu lukittu, jatka rauhassa"
#define TXT_ADV_VPD_LOW      "Ilma kostea - tuuleta tai lis\xE4\xE4 ilmavirtaa"
#define TXT_ADV_VPD_HIGH     "Ilma kuiva - suihkuta vett\xE4 ja varjosta"
#define TXT_ADV_HEAT_HIGH    "Liian kuuma - varjosta ja tuuleta"
#define TXT_ADV_LIGHT_LOW    "Valo heikko - tarkista lamppu"
#define TXT_ADV_LIGHT_HIGH   "Valoa liikaa - varjosta harsolla"
#define TXT_ADV_OK           "Kaikki hyvin - kasvi voi hyvin"
#define TXT_ADV_PHASE_DONE   "Vaihe valmis - kuittaa napista"
#define TXT_ADV_WAITING      "Odotetaan dataa..."

// Askel-laatikko (COACH, coach_view.h coach_drawStepBox). HUOM: nama ovat
// paikallisia literaaleja (ilman skandeja) — itse OHJETEKSTI tulee PM:lta
// (grow_steps.h) ja siina skandit toimivat (utf8_latin1.h parse-rajalla).
#define TXT_STEP_HEADER       "Askel %d/%d"
#define TXT_STEP_TIMER_LEFT   "~%d min j\xE4ljell\xE4"      // TIMER: laskee alas
#define TXT_STEP_TIMER_SOON   "alle %d min"           // TIMER: viimeinen pykala
#define TXT_STEP_ELAPSED_DAYS "kulunut %d vrk"        // BUTTON+kynnys: laskee ylos
#define TXT_STEP_ELAPSED_HOURS "kulunut %d h"
#define TXT_STEP_ACK_HINT     "Kuittaa painamalla nappia"
#define TXT_STEP_TIMER_HINT   "Laite etenee itse ajastimella"

// ═══════════════════════════════════════════════════════════════════
// 3. FieldId — sirottele näitä korteille / kontekstiin (taulukot alla).
//    Uusi token: lisää tähän + case resolveField():iin + FIELD_MAP py:hyn.
//    Katalogi: docs/arkisto/kehitys/eink-muuttujat.md
// ═══════════════════════════════════════════════════════════════════

enum FieldId {
  FIELD_NONE = 0,
  // Tier 1 — PE-ydin
  FIELD_VPD,
  FIELD_LEAF_DELTA,
  FIELD_LEAF_TEMP,
  FIELD_DLI,
  FIELD_PPFD,
  // Anturin OMA lukema ilman geometriakerrointa. Kun kerroin != 1.0, tama ja
  // FIELD_PPFD eroavat — nakyva erotus on ainoa tapa huomata etta kerroin on
  // vaarin ilman CLI:ta (PROJEKTIN_TILA §4 TODO "raakalukema e-inkiin").
  FIELD_PPFD_SENSOR,
  FIELD_CO2,
  // Tier 2 — ympäristö
  FIELD_AIR_TEMP,
  FIELD_AIR_RH,
  FIELD_WATER_TEMP,
  FIELD_HEIGHT,
  FIELD_WATER_LEVEL,
  FIELD_TDS,
  FIELD_BATTERY,
  // Tier 3/4/5 — toiminta / ohjelma / järjestelmä
  FIELD_EBB_STATE,
  FIELD_NEXT_FLOOD,
  FIELD_LIGHTS,
  FIELD_PUMP,
  FIELD_PLANT_NAME,
  FIELD_PHASE_NAME,
  FIELD_GROW_DAYS,
  FIELD_UPTIME,
  // Tier 6 — virrankulutus (INA228, 12 V vakio-oletus)
  FIELD_POWER_W,       // hetkellinen teho = 12 V * virta (W)
  FIELD_AVG_POWER_W,   // keskiteho bootista = energia/aika (W) — akun purkunopeus
  FIELD_ENERGY_WH,     // kertynyt energia bootista = 12 V * charge (Wh) — integraali
};

// ═══════════════════════════════════════════════════════════════════
// 4. KORTIT — 2x2 ruudukko. Vaihda kortin mittari muuttamalla `field`,
//    vaihda otsikko muuttamalla `label`. Järjestys: ylävasen, yläoikea,
//    alavasen, alaoikea. (Pidä 4 korttia ruudukon vuoksi.)
// ═══════════════════════════════════════════════════════════════════

struct EinkCardSpec {
  uint8_t     field;   // FieldId
  const char* label;   // kortin alaotsikko
};

static const EinkCardSpec EINK_CARDS[] = {
  { FIELD_VPD,        "VPD" },
  { FIELD_LEAF_DELTA, "Lehti-ero" },
  { FIELD_DLI,        "DLI" },
  { FIELD_WATER_TEMP, "Vesi" },
};
#define EINK_CARD_COUNT ((int)(sizeof(EINK_CARDS) / sizeof(EINK_CARDS[0])))

// ═══════════════════════════════════════════════════════════════════
// 5. KONTEKSTI-RIVI — yksi rivi "label value unit" -paloja (CONTEXT_Y).
//    Sirottele tähän mitä haluat; tyhjä label = ei etuliitettä.
//    Invalidi kenttä jätetään riviltä pois automaattisesti.
// ═══════════════════════════════════════════════════════════════════

struct EinkContextSpec {
  uint8_t     field;        // FieldId
  const char* label;        // lyhyt etuliite (tyhjä = pelkkä arvo)
  // Yksikön ohitus: nullptr = resolveField():n oma yksikkö, "" = ei yksikköä.
  // Rivi on yksi ainoa rivi kiinteässä leveydessä, joten yksikön pudottaminen
  // on ainoa tapa mahduttaa lisää mittareita silloin kun etiketti jo kertoo
  // yksikön ("Valo 320" — PPFD on aina umol). Jätä pois = nykykäytös säilyy.
  const char* unitOverride;
};

static const EinkContextSpec EINK_CONTEXT[] = {
  { FIELD_AIR_TEMP,     "Ilma" },
  { FIELD_AIR_RH,       "RH" },
  { FIELD_HEIGHT,       "Korkeus" },
  // Akkua ei ole vielä → näytetään keskiteho (akun purkunopeuden suunta).
  // Vaihdettavissa: FIELD_POWER_W (hetkellinen) tai FIELD_ENERGY_WH (integraali).
  { FIELD_AVG_POWER_W,  "Teho" },
};
#define EINK_CONTEXT_COUNT ((int)(sizeof(EINK_CONTEXT) / sizeof(EINK_CONTEXT[0])))

// ═══════════════════════════════════════════════════════════════════
// 6. STATUS-PROFIILI (tilannenaytto / SEURANTA) — status_view.h
//
//    Uusi paanakyma (pe-ohjausmalli.md V1): anturikortit PUOLEEN tilaan
//    yhdelle riville, ja niiden alle iso rajattu VALMENNUSPANEELI
//    (lihavoitu paavaihe + tilalause + HOIDAN/AUTA-rivit tavoite-vs-nyky).
//    Oma geometria (data-sarake ~50 px leveampi kuin USER:lla) — ei kosketa
//    USER/COACH-vakioihin. eink_preview.py --profile status peilaa taman.
//
//    ★ SKANDIT TÄSSÄ OSIOSSA: OIKEAA UTF-8:aa ("ä"), EI \xE4 — päinvastoin
//      kuin § 2:ssa. Syy on piirtokutsu: status_view.h ajaa nämä
//      status_u8At/Right/Centered/WrapPrint-funktioiden kautta, jotka tekevät
//      utf8ToLatin1()-muunnoksen piirtorajalla. Valmis \xE4-tavu näyttäisi
//      siellä katkenneelta UTF-8-sekvenssiltä ja merkki KATOAISI HILJAA.
//
//      Poikkeus samassa tiedostossa: TXT_UPTIME_PREFIX (§ 2) päätyy myös
//      STATUS-näytölle, mutta se printataan RAAKANA (status_view.h footer) →
//      se pysyy \xE4-muodossa. Sääntö määräytyy piirtokutsusta, ei osiosta.
//      TXT_STATUS_TAG_* printataan raakana (badge) ja TXT_STATUS_TARGET_* on
//      ASCII-chromea — molemmat toimivat kummallakin polulla, koska niissä ei
//      ole skandeja. Mekaaninen vahti molemmille: test_layout_def_tables.
// ═══════════════════════════════════════════════════════════════════

#define STATUS_DIVIDER_X    320                                  // USER:lla 370; data +50 px
#define STATUS_DATA_X       (STATUS_DIVIDER_X + 10)              // 330
#define STATUS_DATA_W       (DISPLAY_WIDTH - STATUS_DATA_X - MARGIN)  // 450
#define STATUS_PLANT_X      MARGIN                               // 20
#define STATUS_PLANT_W      (STATUS_DIVIDER_X - MARGIN - 10)     // 290

#define STATUS_HEADER_Y     45      // kello + paivamaara (sama kuin USER)
#define STATUS_CARDS_Y      66      // korttirivi heti kellon alla
#define STATUS_CARD_H       84      // yksi rivi (USER:lla 2x2 = 235 px; tama ~puolet)
#define STATUS_CARD_GAP     10      // vaakavali (4 korttia)
#define STATUS_CARD_W       ((STATUS_DATA_W - 3 * STATUS_CARD_GAP) / 4)  // ~105
#define STATUS_PANEL_Y      (STATUS_CARDS_Y + STATUS_CARD_H + 12)  // 162
// Paneelin pohja nostettu 430 -> 408 (28.7.2026), jotta konteksti mahtuu
// KAHDELLE riville yhden sijaan — vain siten jokaiselle luvulle mahtuu yksikko
// (ks. STATUS_CONTEXT alla). Paneeli menetti 22 px eli yhden valmennusrivin
// verran tilaa; se kestaa sen, koska rivit ovat harvoin taynna (vain
// tavoitteen ulkopuoliset mitat piirtyvat).
#define STATUS_PANEL_BOTTOM 408                                  // paneelin pohja (konteksti alle)
#define STATUS_PANEL_H      (STATUS_PANEL_BOTTOM - STATUS_PANEL_Y)  // 246
#define STATUS_PANEL_PAD    16      // paneelin sisareunus
#define STATUS_CONTEXT_Y    424     // konteksti rivi 1 (ilma/RH/valo/korkeus/teho)
#define STATUS_CONTEXT_LINE_H 18    // rivivali -> rivi 2 on y 442, viivan (448) ylapuolella
#define STATUS_FOOTER_Y     468     // kayntiaika + leima

// KERTATEKO (aktiivinen askel) tayttaa korttirivin JA paneelin yhdella
// laatikolla (grow_step_box.h). Sama datasarake kuin muu STATUS-nakyma.
#define STATUS_STEP_BOX_Y   STATUS_CARDS_Y                       // 66
#define STATUS_STEP_BOX_H   (STATUS_PANEL_BOTTOM - STATUS_STEP_BOX_Y)  // 364

// Chrome-tekstit (ASCII). Valmennusviestit ovat status_targets.h:ssa.
#define TXT_STATUS_TAG_DEVICE   "HOIDAN"   // laite saataa itse
#define TXT_STATUS_TAG_USER     "AUTA"     // kayttaja toimii
// Tavoiteikkunan etuliitteet valmennusrivin oikeassa reunassa ("tav 0.4-0.8
// kPa"). Koostaa status_targets.h status_formatTarget(); piirto status_view.h.
#define TXT_STATUS_TARGET_PREF  "tav "     // alue: "tav lo-hi yksikko"
#define TXT_STATUS_TARGET_MAXPREF "tav <"  // pelkka katto (esim. lehtilampo)
#define TXT_STATUS_ALL_OK       "Kaikki tavoitteet kunnossa - ei toimenpiteitä."
#define TXT_STATUS_SUB_DEFAULT  "Kasvaa tasaisesti."   // tilalause kun PM ei anna actionia
// growing.button_next_phase == true: opastus on kayty loppuun, lyhyt nappi-
// painallus siirtaa vaiheen (display_status_text.h status_selectPanelSentenceKind,
// resepti 3.8.2026 rautahavainnolle jossa nappi toimi mutta paneeli ei kertonut
// sita). Mitattu FreeSansBold12pt8b-fontin xAdvance-summalla (sama algoritmi
// kuin eink_measureLatin1): 341 px, mahtuu STATUS-paneelin 418 px:n riville
// yhdelle riville — wrap-varmistus (status_u8WrapPrint, maxLines 2) jos jokin
// PM-versio joskus pidentaa tata.
#define TXT_STATUS_BUTTON_HINT  "Paina nappia: seuraava vaihe"
#define TXT_STATUS_PROGRESS     "päivä %d / %d"         // vaiheen edistyminen (päivä X / Y)
#define TXT_STATUS_PROGRESS_OPEN "päivä %d"             // kun kokonaiskesto tuntematon

// Tilannenayton kortit — 4 PE-ydinmittaria yhdella rivilla. Tavoite kullekin
// tulee status_targets.h:n vaihekohtaisesta taulusta (ei tassa). Pida 4
// (STATUS_CARD_W on mitoitettu neljalle). Labelit ASCII.
static const EinkCardSpec STATUS_CARDS[] = {
  { FIELD_VPD,       "VPD" },
  { FIELD_DLI,       "DLI" },
  { FIELD_CO2,       "CO2" },
  { FIELD_LEAF_TEMP, "Lehti" },
};
#define STATUS_CARD_COUNT ((int)(sizeof(STATUS_CARDS) / sizeof(STATUS_CARDS[0])))

// Tilannenayton alarivit — omansa, jotta USER/COACH:n EINK_CONTEXT ei muutu.
//
// KAKSI RIVIA (28.7.2026). Aiemmin tama oli yksi rivi, johon viisi palaa mahtui
// vain yksikoita karsimalla: PPFD nakyi muodossa "Valo 4.2" ilman umol-yksikkoa,
// ja rivi mittasi 424/450 px eli oli yhden kentan paassa hiljaisesta
// ylivuodosta. Lukema ilman yksikkoa on tulkinnanvarainen — varsinkin kun
// ylhaalla oleva kortti nayttaa DLI:ta eri yksikossa (kayttajan havainto
// 28.7.2026: "Valoteho heikko, tavoite 100-200 umol" luettiin DLI-kortin
// tavoitteeksi). Nyt palat pakataan kahdelle riville
// (status_packContextLines, display_status_text.h) ja jokainen luku saa
// yksikkonsa takaisin. Pakkaaja mittaa jokaisen palan ennen sijoitusta ja
// pudottaa sen mika ei mahdu, joten uuden kentan lisays ei riko layoutia
// hiljaa. Paneelin pohja nostettiin 430 -> 408 jotta toinen rivi mahtuu.
//
// Etikettisaanto: etiketti VAIN kun yksikko ei yksin kerro mika arvo on.
// "C" = ilman lampo, "W" = teho, "mm" = korkeus -> ei etikettia tarvita.
// "%" voisi olla akku ja "umol" hukkuisi ilman kontekstia -> ne saavat etiketin.
static const EinkContextSpec STATUS_CONTEXT[] = {
  { FIELD_AIR_TEMP,    "",        nullptr },  // "22.7 C" — yksikko tunnistaa kentan
  { FIELD_AIR_RH,      "Kosteus", nullptr },  // tavallinen huoneilmankosteus, kuten kotimittarissa
  { FIELD_PPFD,        "Valo",    nullptr },  // "Valo 120 umol" — latvan taso (geometria mukana)
  { FIELD_PPFD_SENSOR, "Anturi",  nullptr },  // sama anturin paikassa — erotus paljastaa vaaran geometriakertoimen
  { FIELD_CO2,         "CO2",     nullptr },  // "CO2 463 ppm"
  { FIELD_WATER_TEMP,  "Vesi",    nullptr },  // DS18B20 — ei nay missaan muualla naytolla
  { FIELD_HEIGHT,      "Kork",    nullptr },
  { FIELD_AVG_POWER_W, "",        nullptr },  // "0.7 W"
};
#define STATUS_CONTEXT_COUNT ((int)(sizeof(STATUS_CONTEXT) / sizeof(STATUS_CONTEXT[0])))

#endif // LAYOUT_DEF_H
