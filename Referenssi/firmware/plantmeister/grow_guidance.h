/*=====================================================================
  grow_guidance.h - Guided growing guidance text helpers

  Kaksi tekstia samasta vaiheesta, koska kanavilla on eri rajoitteet:

    grow_resolvePhaseGuidance()  - PITKA (<= 96 merkkia). Kasviprofiilin oma
        teksti (plants.json, kayttajan muokattavissa), fallback
        portal_growInstructionText(). Portaaliin, jossa tilaa on vapaasti.

    grow_shortAction()           - LYHYT (<= 44 MERKKIA, koodipistetta ei
        tavua). E-inkin neuvontabanneriin, joka on yksi rivi FreeSansBold12pt:lla
        (~45 merkkia leveys DATA_W:ssa) ja 34 px korkea = ei toista rivia.

  Miksi ei yhta tekstia molempiin: banneria EI saa kutistaa mahduttamaan
  pitkaa tekstia. Fonttikoon pienennys kokeiltiin ja hylattiin rautatestissa
  10.7.2026 - "fontit kutistuivat lukukelvottomiksi" (coach_view.h header).
  Seinataululta luetaan huoneen toiselta puolelta; se on nayton koko tarkoitus.
  Lyhyt teksti on siis rautatestattu rajoite, ei mielipide.

  Skandit: nama tekstit menevat /api/state:n kautta e-inkille, joka muuntaa
  ne UTF-8:sta Latin-1:ksi parse-vaiheessa (utf8_latin1.h) — a/o TOIMIVAT ja
  kaikki talla riveilla oleva teksti on kirjoitettu oikealla suomella (21.7.2026
  Fable-kasikirjoituksen kirjoitussaanto). Yksi totuus koko muunnosketjusta:
  docs/ohjeet/eink_operointi.md "Skandit". ALA kayta en-dashia tai typografisia
  lainausmerkkeja (ne eivat ole Latin-1:ssa) — ASCII-viiva '-' ja suorat
  lainausmerkit. Rajat: banneri <= 44 koodipistetta, guidance[96] <= 95 tavua.
=====================================================================*/

#ifndef GROW_GUIDANCE_H
#define GROW_GUIDANCE_H

#include "config.h"
#include "structs.h"

#include <string.h>

inline const char* grow_resolvePhaseGuidance(const GrowPhaseParams* phase,
                                             const char* fallbackInstruction) {
  if (phase && phase->guidance[0] != '\0') {
    return phase->guidance;
  }
  return fallbackInstruction ? fallbackInstruction : "";
}

// Lyhyt "tee tama nyt" -toiminto vaiheelle. Riippuu aloitustavasta, koska
// sama vaihe tarkoittaa eri tekemista sen mukaan mista kayttaja lahti:
// juurtumisvaihe on pistokkaan leikkaamista (startMethod 0) mutta siemenella
// kivivillan valmistelua (startMethod 1). Ilman tata erottelua siemenen
// kylvanyt kayttaja saa pistokasohjeet - juuri se virhe jonka takia
// aloitustapa ylipaataan kysytaan.
//
// startMethod: 0 = pistokas, 1 = siemen, 2 = kaupan taimi (structs.h).
// Lahde: docs/ALOITUSOPAS.md Osa B.
inline const char* grow_shortAction(GrowPhaseType type, uint8_t startMethod) {
  switch (type) {
    case GROW_PHASE_ROOTING:
      // Siemenpolulla juurtumisvaihetta ei normaalisti valita (aloitusvaihe on
      // SEEDLING), mutta kayttaja voi vaihtaa vaihetta kasin - alaa neuvo
      // pistokkaan leikkaamista sille joka kylvi siemenen.
      return (startMethod == 1)
        ? "Pidä kivivilla kosteana, odota juuria"
        : "Leikkaa varsi solmun alta, pidä kosteana";

    case GROW_PHASE_SEEDLING:
      if (startMethod == 1) {
        // Aloitusoppaan Osa B vaiheet 1-3 tiivistettyna: kostea + lammin +
        // karsivallisyys. "Ala kaivele" on oppaan oma korostus.
        return "Pidä kannen alla lämmössä, älä kaivele";
      }
      return (startMethod == 2)
        ? "Totuta taimi valoon, mieto ravinne"
        : "Mieto ravinne, vältä liikakastelua";

    case GROW_PHASE_VEGETATIVE:
      return "Seuraa lehtien väriä ja vedenkulutusta";

    case GROW_PHASE_HARVEST:
      return "Korjaa latvat, jätä kaksi solmua";

    case GROW_PHASE_CUSTOM:
    default:
      return "Seuraa kasvia päivittäin";
  }
}

// ── Vaihe- JA paivakohtainen valmennusrivi (Fable-kasikirjoitus §3.2/3.4/3.6) ─
//
// grow_shortAction() ylla on kasvi- ja paivaneutraali: se palauttaa saman
// VEGETATIVE-rivin koko 21 vrk:n ajan. Fable-kasikirjoitus haluaa rivin
// vaihtuvan vaiheen edetessa ("lehdet tummanvihreat?" paivina 0-6 -> "nipista
// karki" paivina 7-15 -> "kohti korjuuta" paivina 16-21). Tama datataulukko
// avaimennetaan (kasvi, vaihe, paivakynnys)-kolmikolla ja haku valitsee
// korkeimman kynnyksen joka on viela <= vaihepaiva (growElapsedDays, nollautuu
// vaihevaihdossa - scheduler.h). Sama data-vetoinen kuvio kuin grow_steps.h:n
// rekisteri: uusi kasvi = taulukkorivit, ei koodia.
//
// Vain basilika on taytetty; muut kasvit putoavat grow_shortAction-switchiin.
// Skandit sallittu: kulkevat /api/state:n kautta UTF-8->Latin-1-muunnoksen lapi
// (tama tiedoston alun kommentti), joten kirjoitetaan oikealla suomella (Fable
// §2 kirjoitussaanto). '-' ei em-dashia (ei Latin-1:ssa).
//
// PITUUS: e-inkin STATUS-tilalause on ~418px leveä (paneeli 450 - 2*16 pad) ja
// piirretaan FreeSansBold12pt8b:lla. Se on ~30-33 merkkia; pidempi rivi ei enaa
// mahdu 12pt:hen. status_view.h status_l1TilaLine kutistaa ylivuotavan rivin
// automaattisesti 9pt:hen (ei koskaan valu ruudun yli), mutta yhtenaisen
// 12pt-ulkoasun vuoksi pyri <= ~33 merkkiin / <= 418px. (Vanha "<= 44 merkkia"
// -raja oletti kapeamman fontin ja aiheutti ylivuodon 22.7.2026 taimirivilla.)
struct GrowActionWindow {
  const char* plantId;    // kasvi-tarkka (ei "" -jokeria: switch on geneerinen)
  uint8_t     phase;      // GrowPhaseType
  uint16_t    dayFrom;    // alaraja (inclusive), vaihepaiva growElapsedDays
  const char* text;
};

static const GrowActionWindow GROW_ACTION_WINDOWS[] = {
  // Basilika, taimivaihe (SEEDLING) — SEURANTA idatysaskelten jalkeen (Fable §3.2).
  // Kolme ikkunaa: ensin ORIENTAATIO (mika juuri muuttui: valo syttyi, mutta
  // NFT-vesikiertoa EI viela — se alkaa vasta kasvuvaiheen askel C2:ssa), sitten
  // vahvistuminen, sitten lahesty siirtoa. Kayttajan rautahavainto 22.7.2026:
  // siirtyma pimeasta idatyksesta SEURANTAAN oli liian abrupti — ensimmainen
  // rivi ("Taimi vahvistuu valossa...") ei kertonut mika tilanteessa muuttui.
  { "basil", GROW_PHASE_SEEDLING,   0, "Taimi valossa, ei vielä vesikiertoa" },
  { "basil", GROW_PHASE_SEEDLING,   2, "Taimi vahvistuu, odota juuria" },
  { "basil", GROW_PHASE_SEEDLING,   9, "Tarkista juuret - valmis siirtoon?" },
  // Basilika, kasvuvaihe (VEGETATIVE) (Fable §3.4)
  { "basil", GROW_PHASE_VEGETATIVE,  0, "Kasvu käynnissä - lehdet tummanvihreät?" },
  { "basil", GROW_PHASE_VEGETATIVE,  7, "6-8 lehteä? Nipistä kärki, kasvi haarautuu" },
  { "basil", GROW_PHASE_VEGETATIVE, 16, "Latvat kasvavat kohti korjuuta" },
  // Basilika, sadonkorjuu (HARVEST) — jatkuva sato (Fable §3.6)
  { "basil", GROW_PHASE_HARVEST,   0, "Korjaa säännöllisesti - kasvi haarautuu" },
  { "basil", GROW_PHASE_HARVEST,  45, "Kasvi vanhenee - harkitse uutta siementä" },
};
#define GROW_ACTION_WINDOW_COUNT \
  ((int)(sizeof(GROW_ACTION_WINDOWS) / sizeof(GROW_ACTION_WINDOWS[0])))

// Palauta kasvikohtainen valmennusrivi, tai NULL jos kasville/vaiheelle ei ole
// taulukkorivia (kutsuja putoaa grow_shortActioniin). Valitsee korkeimman
// dayFrom-kynnyksen joka on viela <= elapsedDays.
inline const char* grow_actionWindowText(const char* plantId, uint8_t phase,
                                         uint16_t elapsedDays) {
  if (!plantId || plantId[0] == '\0') return NULL;
  const char* best    = NULL;
  int         bestDay = -1;
  for (int i = 0; i < GROW_ACTION_WINDOW_COUNT; i++) {
    const GrowActionWindow* w = &GROW_ACTION_WINDOWS[i];
    if (w->phase != phase) continue;
    if (strcmp(w->plantId, plantId) != 0) continue;
    if ((int)w->dayFrom <= (int)elapsedDays && (int)w->dayFrom > bestDay) {
      bestDay = (int)w->dayFrom;
      best    = w->text;
    }
  }
  return best;
}

// Kasvitietoinen valmennusrivi: taulukko ensin, sitten geneerinen switch.
// Tama on se mita /api/state tarjoilee e-inkin banneriin (growing.action).
inline const char* grow_shortActionFor(const char* plantId, GrowPhaseType type,
                                       uint8_t startMethod, uint16_t elapsedDays) {
  const char* w = grow_actionWindowText(plantId, (uint8_t)type, elapsedDays);
  if (w) return w;
  return grow_shortAction(type, startMethod);
}

#endif // GROW_GUIDANCE_H
