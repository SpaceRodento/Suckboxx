/*=====================================================================
  grow_steps.h - Kasvatuksen askeleet vaiheen sisalla

  ★ TATA TIEDOSTOA EDITOIMALLA muutat sen mita laite neuvoo kayttajalle.
    Tekstit ja ajastimet ovat dataa; logiikka on muualla (grow_step_fsm.h).
    Muutos vaatii flashin — tekstit ovat firmwaressa, EIVAT plants.jsonissa.
    Se on tahallista: plants.json poistetaan tehdasresetissa, ja se yliajaa
    firmwaren oletukset (factory_reset.h) -> korjatut tekstit eivat koskaan
    tulisi laitteelle jos ne asuisivat siella.

  ── Miksi askeleita tarvitaan ────────────────────────────────────────
  Kasvuvaihe (GrowPhaseParams.guidance) on YKSI teksti per vaihe. Se
  riittaa kun neuvo on "seuraa lehtien varia viikkoja". Se ei riita
  siemenesta aloittamiseen, jossa ALOITUSOPAS.md Osa B on nelja erillista
  tekoa muutaman tunnin sisalla: liota -> valuta -> kylva -> idata.
  Yksi teksti joutuisi tiivistamaan ne lauseeksi, ja tarkein tieto
  (liotusvesi on pH 5,5, EI ravinnetta) katoaisi.

  ── Kuka paattaa etta askel on valmis ────────────────────────────────
  Taman kentan (advanceOn) takia struct on olemassa. Kaikki ajastimet
  EIVAT ole samanlaisia:

    Liotus 30 min  -> KELLO on totuus. Laite tietaa milloin se on ohi.
                      Napin vaatiminen olisi turhaa tyota.
    Idatys 5-10 vrk -> KELLO on arvaus. Sadan prosentin haitari; ajastin
                      joka etenisi itsestaan viidessa vuorokaudessa
                      VALEHTELISI. Totuus on "nakyyko sirkkalehtia", ja
                      sen nakee vain ihminen.

  Sama kentta ratkaisee myos ajastimen suunnan:
    TIMER  -> laskee ALAS  ("noin 20 min jaljella")
    BUTTON -> laskee YLOS  ("kulunut 3 vrk")   [kun timerMin > 0]

  ── Ajastin ja sahkokatko ────────────────────────────────────────────
  Askelkello persistoituu growclock.json:iin ja kayttaytyy kuten muutkin
  kasvukellot (grow_clock_math.h): katkon KESTOA ei voi tietaa ilman
  RTC:ta, joten sita ei lasketa mukaan. Liotukselle tama on teknisesti
  vaara suuntaan — kivivilla liottuu katkonkin ajan — mutta seuraus on
  vaaraton: kuutiot vain liottuivat pidempaan, ja seuraava askel on
  kuitenkin valuttaa ne. ALOITUSOPAS sallii 30-60 min. Emme teeskentele
  tarkkuutta jota ilman RTC:ta ei ole.

  ── Skandit ──────────────────────────────────────────────────────────
  Naissa teksteissa KAYTETAAN a/o-umlauteja: e-inkin fontit kattavat
  0x20-0xFF ja UTF-8 -> Latin-1 -muunnos tehdaan piirtorajapinnalla.
  MUTTA Latin-1:sta puuttuvat typografiset merkit — kayta ASCII-viivaa
  '-' (EI en-dashia) ja suoria lainausmerkkeja. Aste-merkki ° (0xB0) on
  Latin-1:ssa ja toimii.

  Lahde: docs/ALOITUSOPAS.md Osa B.
=====================================================================*/

#ifndef GROW_STEPS_H
#define GROW_STEPS_H

#include "config.h"
#include "structs.h"   // GrowPhaseType

#include <stdint.h>
#include <string.h>

// Kuka paattaa etta askel on valmis. Ks. otsikon perustelu.
enum GrowStepAdvance : uint8_t {
  STEP_ADVANCE_TIMER  = 0,   // kello: timerMin tayttyy -> seuraava askel itsestaan
  STEP_ADVANCE_BUTTON = 1,   // ihminen: nappia painetaan
};

struct GrowStep {
  const char* text;       // ohje askeleen alussa
  const char* textLate;   // NULL = teksti ei muutu. Muuten: vaihtuu kun timerMin tayttyy.
                          // Vain BUTTON-askeleilla mielekas (TIMER-askel vaihtuu jo pois).
  uint32_t    timerMin;   // 0 = ei ajastinta. uint32 eika uint16: 16-bit kattaisi
                          // vain 45 vrk, ja hiljainen ylivuoto pitkassa vaiheessa
                          // olisi juuri sellainen ansa jota ei kannata jattaa.
  uint8_t     advanceOn;  // GrowStepAdvance
};

// Aloitustapa-jokeri (grow_stepsForIn). Aloitustavat ovat 0/1/2 (structs.h),
// joten 0xFF ei tormaa mihinkaan oikeaan arvoon. Kasvuvaihe ja sadonkorjuu
// ovat aloitustavasta RIIPPUMATTOMIA — siemen/pistokas on jo takana — joten
// yksi askellista palvelee kaikkia ilman kolmea identtista kopiota. Tarkka
// aloitustapa-osuma voittaa yha jokerin (haun precedenssipisteytys).
#define STEP_METHOD_ANY 0xFF

// ═══════════════════════════════════════════════════════════════════
// TEKSTIT — omina vakioinaan jotta sama askel voidaan jakaa useamman
// kasvin listalle ilman etta teksti kopioituu (yksi totuus per ohje).
// ═══════════════════════════════════════════════════════════════════

// Askel 1 (Valmistele). Tarvikkeiden kokoaminen ja pH 5,5 -veden valmistus
// ENNEN 30 min liotusajastinta: ajastin juoksee joka tapauksessa, joten
// puuttuvan pH-liuoksen huomaaminen kesken liotuksen olisi huono hetki.
#define TXT_STEP_PREP \
  "Kokoa tarvikkeet: kivivillakuutiot, basilikan siemenet ja läpinäkyvä " \
  "kansi tai muovipussi kuvuksi. Sekoita noin litra vettä ja säädä pH 5,5:een " \
  "sitruunahapolla tai pH-alaslaskuliuoksella.\n\n" \
  "Kun vesi on valmis, paina nappia - seuraava askel ajastaa 30 min liotuksen."

// Askel 2 (Liota). Tarkein yksittäinen tieto koko putkessa: liotusvesi on
// pH 5,5 VETTÄ, ei ravinneliuosta. Kivivilla on emäksistä sellaisenaan, ja
// juuri tämä on se jonka aloittelija saa väärin.
#define TXT_STEP_SOAK \
  "Liota kivivillakuutiot pH 5,5 vedessä noin 30 minuuttia.\n\n" \
  "Kivivilla on emäksistä sellaisenaan (pH 7-8) eivätkä juuret viihdy siinä. " \
  "Säädä vesi sitruunahapolla tai pH-down-liuoksella. Älä käytä ravinneliuosta vielä."

// Askel 3. Valutus ja kylvö yhdessä: molemmat tapahtuvat tiskialtaalla
// yhdellä kertaa, joten erillinen kuittaus välissä olisi keinotekoinen.
#define TXT_STEP_SOW \
  "Valuta ylimääräinen vesi pois. Kuution pitää olla kostea, ei lilluva - " \
  "liika vesi tukahduttaa idun hapenpuutteeseen. Älä purista kuutiota, " \
  "se rikkoo ilmarakenteen.\n\n" \
  "Pudota sitten 1-2 siementä kuution reikään, noin 0,5 cm syvyyteen, " \
  "ja peitä kevyesti kivivillan murulla."

// Askel 4, alkuteksti: odotusvaihe. "Älä kaivele" on oppaan oma korostus.
// Kuittausehto ("paina kun näet ensilehdet") sanotaan jo tässä lyhyesti,
// jottei käyttäjän tarvitse arvata mitä nappi tekee ennen kuin myöhäisteksti
// (TXT_STEP_GERMINATE_LATE, ~5 vrk) sen toistaa laajemmin.
#define TXT_STEP_GERMINATE \
  "Pidä kuutiot kannen alla pimeässä, 20-25 °C.\n\n" \
  "Kivivilla ei saa kuivua eikä olla likomärkä. Älä kaivele siementä - pidä " \
  "olosuhteet tasaisina ja anna taimen itää rauhassa.\n\n" \
  "Basilika itää yleensä 5-10 vuorokaudessa. Paina nappia vasta kun näet " \
  "ensilehdet (sirkkalehdet) - valo syttyy silloin heti."

// Askel 4, myöhäinen teksti: vaihtuu kun arvion alaraja (5 vrk) täyttyy.
// Ajastin ei etene tästä itsestään — se vain vaihtaa tekstin odotuksesta
// tarkkailuun. Kuittausehto sanotaan tässä suoraan, koska se on askeleen
// tärkein tieto eikä mahtuisi yhden rivin banneriin.
#define TXT_STEP_GERMINATE_LATE \
  "Näinä päivinä sirkkalehdet ilmestyvät. Tarkkaile kuutioita päivittäin, " \
  "mutta älä häiritse kasvia liikaa.\n\n" \
  "Paina nappia kun näet ensilehdet - valo syttyy heti, tai taimi venyy ja kaatuu."

// ── Basilika, kasvuvaihe (VEGETATIVE) — Fable-kasikirjoitus §3.3 ─────
// Kolme BUTTON-askelta: siirto kasvukourulle -> laimea ravinne + kierto ->
// lampun korkeus. EC/pH-arvot ovat basilikan taimelle mitoitettuja (mS/cm,
// pH 5,5-6,0). Voittavat geneerisen STEPS_GENERIC_VEGETATIVE-fallbackin.

// Askel 1/3: siirto kasvukourulle. Kaynnistysehto = juuret nakyvat kuution
// pohjasta (sama havainto kuin taimivaiheen paatos).
#define TXT_BASIL_VEG_TRANSPLANT \
  "Juuret näkyvät kuution pohjasta - basilika on valmis siirtymään " \
  "kasvukourulle.\n\n" \
  "Aseta kivivillakuutio yläkannen reikään niin että kuution pohja koskettaa " \
  "kuitumattoa. Matto imee ravinnevettä kuutioon ennen kuin juuret ehtivät " \
  "levitä itse matolle.\n\n" \
  "Paina nappia kun taimi on paikallaan."

// Askel 2/3: laimea ravinne + jatkuva kierto. Tuore taimi ei kesta vahvaa
// liuosta — juuret palavat ennen kuin ehtivat ottaa ravinnetta vastaan.
#define TXT_BASIL_VEG_NUTRIENT \
  "Täytä säiliö ja sekoita LAIMEA ravinneliuos: sähkönjohtavuus 0,8-1,2 mS/cm, " \
  "happamuus pH 5,5-6,0. Tuore taimi ei kestä vahvaa liuosta - juuret palavat " \
  "ennen kuin ehtivät ottaa mitään vastaan.\n\n" \
  "Käynnistä kierto: ohut, jatkuva ravinnevirtaus jonka kuitumatto levittää " \
  "tasaisesti koko juuristolle.\n\n" \
  "Paina nappia kun ravinne kiertää."

// Askel 3/3: lampun korkeus. Laite hoitaa valojakson; kayttaja saataa vain
// korkeuden. Kiintea 30 cm -aloitus toimii sellaisenaan (elava lamppuvihje
// = Fable §4.2, eri PR). "Seuraa sita rivia" viittaa siihen tulevaan riviin.
#define TXT_BASIL_VEG_LIGHT \
  "Laite pitää valojakson yllä itse - sinun tehtäväsi on vain lampun korkeus.\n\n" \
  "Aloita noin 30 cm taimen yläpuolelta. Ensimmäisen kasvuviikon aikana e-ink " \
  "kertoo tarvitseeko lamppua nostaa vai laskea - seuraa sitä riviä, ei numeroa.\n\n" \
  "Paina nappia kun lamppu on paikallaan."

// ── Basilika, sadonkorjuu (HARVEST) — Fable-kasikirjoitus §3.5 ───────
// Kaksi askelta: ensisato -> toistuva ratooning. Toisen askeleen textLate
// (5 vrk) vaihtaa "toivu" -> "seuraava sato valmis". Kuittaus paattaa sarjan
// pysyvasti — e-ink jaa SEURANTAAN loppukaudeksi (Fable §3.6).

// Askel 1/2: ensisato. Leikkaustekniikka (kaksi lehtiparia, kork. kolmasosa)
// saa kasvin haaroittumaan -> sato kasvaa joka korjuulla.
#define TXT_BASIL_HARVEST_FIRST \
  "Basilika on korjuukelpoinen. Leikkaa latvat aina kahden lehtiparin " \
  "yläpuolelta terävillä saksilla - älä koskaan revi.\n\n" \
  "Älä korjaa yli kolmasosaa kasvista kerralla: kasvi tarvitsee jäljelle " \
  "jääviä lehtiä, jotka tuottavat sille energiaa seuraavaan kierrokseen.\n\n" \
  "Paina nappia kun olet korjannut."

// Askel 2/2, alkuteksti: toipuminen ja kukinnan esto. Kukinta pysayttaa
// lehtituotannon — pystyva kukkavarsi katkaistaan heti.
#define TXT_BASIL_HARVEST_REPEAT \
  "Anna kasvin toipua. Leikkauskohdan alle jääneestä solmusta puhkeaa pian " \
  "kaksi uutta versoa yhden tilalle - näin sato kasvaa joka kierroksella, " \
  "ei vähene.\n\n" \
  "Pidä valo ja ravinne ennallaan. Jos kasvi alkaa työntää pystyä keskivartta " \
  "kukkanupulla, katkaise se heti - kukinta pysäyttää lehtituotannon."

// Askel 2/2, myohainen teksti (kynnys 5 vrk): seuraava sato valmis.
#define TXT_BASIL_HARVEST_REPEAT_LATE \
  "Uudet versot ovat kasvaneet - seuraava sato on todennäköisesti valmis.\n\n" \
  "Toista sama tekniikka: leikkaa aina kahden lehtiparin yläpuolelta, älä yli " \
  "kolmasosaa kerralla.\n\n" \
  "Paina nappia jokaisen korjuun jälkeen."

// ── Yleiset (kasvista riippumattomat) tekstit ───────────────────────
// Nama ovat fallback kaikille kasveille joilla ei ole omaa tarkkaa
// listaa. Sanamuoto on lajineutraali: EI basilikan itamispaivia, vaan
// yleinen "odota itamista/juuria". ALOITUSOPAS.md:n sävy.

// Siemenaloitus, yleinen — askel 1: liotus (sama pH 5,5 -periaate kuin
// basilikalla; tama on koko putken tarkein yksittainen tieto).
#define TXT_GEN_SOAK \
  "Liota kivivillakuutiot pH 5,5 vedessä noin 30 minuuttia.\n\n" \
  "Kivivilla on emäksistä sellaisenaan (pH 7-8) eivätkä juuret viihdy siinä. " \
  "Säädä vesi sitruunahapolla tai pH-down-liuoksella. Älä käytä ravinneliuosta vielä."

// Siemenaloitus, yleinen — askel 2: valutus ja kylvo.
#define TXT_GEN_SOW \
  "Valuta ylimääräinen vesi pois. Kuution pitää olla kostea, ei lilluva - " \
  "liika vesi tukahduttaa idun hapenpuutteeseen. Älä purista kuutiota.\n\n" \
  "Pudota siemenet kuution reikään, noin 0,5 cm syvyyteen, ja peitä kevyesti."

// Siemenaloitus, yleinen — askel 3, alkuteksti: odota itamista. Kuittausehto
// mukana alusta asti (sama periaate kuin basilikan TXT_STEP_GERMINATE).
#define TXT_GEN_GERMINATE \
  "Pidä kuutiot kannen alla pimeässä, 20-25 °C.\n\n" \
  "Kivivilla ei saa kuivua eikä olla likomärkä. Älä kaivele siementä - pidä " \
  "olosuhteet tasaisina ja anna taimen itää rauhassa.\n\n" \
  "Paina nappia vasta kun näet ensilehdet (sirkkalehdet)."

// Siemenaloitus, yleinen — askel 3, myohainen teksti (kynnys ~5 vrk).
#define TXT_GEN_GERMINATE_LATE \
  "Näinä päivinä sirkkalehdet voivat ilmestyä. Tarkkaile kuutioita " \
  "päivittäin, mutta älä häiritse kasvia liikaa.\n\n" \
  "Paina nappia kun näet ensilehdet - silloin taimi tarvitsee valoa heti, " \
  "tai se venyy ja kaatuu."

// Pistokasaloitus, yleinen — askel 1: leikkaa pistokas.
#define TXT_GEN_CUT \
  "Leikkaa terve, 8-12 cm pistokas emokasvin kasvavasta versosta. " \
  "Poista alimmat lehdet niin että 2-3 lehteä jää latvaan.\n\n" \
  "Kasta leikkuupää juurrutusaineeseen (hormoni- tai pajuvesi) juuri ennen istutusta."

// Pistokasaloitus, yleinen — askel 2: aseta kivivillaan.
#define TXT_GEN_STICK \
  "Aseta pistokas kosteaan kivivillakuutioon niin että leikkuupää on " \
  "kunnolla kiinni.\n\n" \
  "Kivivilla kostea, ei lilluva. Pidä kuvun alla korkeassa kosteudessa - " \
  "juureton pistokas haihduttaa lehdistä eikä voi vielä juoda juurilla."

// Pistokasaloitus, yleinen — askel 3, alkuteksti: odota juuria. Kuittausehto
// mukana alusta asti (sama periaate kuin idatysaskeleissa).
#define TXT_GEN_ROOT \
  "Pidä pistokas kuvun alla, kirkkaassa mutta epäsuorassa valossa, 20-25 °C.\n\n" \
  "Kivivilla pysyy kosteana. Juuret ilmestyvät yleensä 7-14 vuorokaudessa - " \
  "älä vedä pistokasta tarkistaaksesi, se katkaisee alkavat juuret.\n\n" \
  "Paina nappia vasta kun näet juuria tai uutta lehtikasvua."

// Pistokasaloitus, yleinen — askel 3, myohainen teksti (kynnys ~7 vrk).
#define TXT_GEN_ROOT_LATE \
  "Näinä päivinä ensimmäiset juuret ilmestyvät kuution reunaan. " \
  "Uusi kasvu latvassa on paras merkki juurtumisesta.\n\n" \
  "Paina nappia kun näet juuria tai uutta lehtikasvua - silloin pistokas " \
  "on juurtunut ja voi siirtyä normaaliin kasvatukseen."

// ── Kasvuvaihe (VEGETATIVE), yleinen — aloitustavasta riippumaton ─────
// Lahde: ALOITUSOPAS.md Osa B vaiheet 4-7 (siirto NFT -> ravinne -> valo ->
// seuranta). PE-tavoitteet (PPFD/VPD) ovat kasviprofiilista (plant_lookup.h),
// tekstit viittaavat niihin lajineutraalisti jotta sama lista kelpaa kaikille.

// Askel 1: siirto NFT-tasolle. Kaynnistysehto on sama kuin taimivaiheen
// paatos — juuret nakyvat kuution pohjasta (ALOITUSOPAS Vaihe 4).
#define TXT_GEN_VEG_TRANSPLANT \
  "Juuret näkyvät kuution pohjasta - taimi on valmis NFT-tasolle.\n\n" \
  "Aseta kivivillakuutio yläkannen reikään, suoraan tai verkkoruukussa. " \
  "Varmista että ruukun pohja koskettaa kuitumattoa - matto kastelee juuret " \
  "ennen kuin ne leviävät matolle.\n\n" \
  "Paina nappia kun taimi on paikallaan."

// Askel 2: laimea ravinne + NFT-kierto (Vaihe 5). EC/pH ovat oppaan yleis-
// arvoja taimelle; kasvikohtaiset arvot lukitaan kun kasvatustestit tuottavat
// dataa (ALOITUSOPAS "Mita opas ei viela kata").
#define TXT_GEN_VEG_NUTRIENT \
  "Täytä säiliö ja sekoita LAIMEA ravinneliuos: EC 0,8-1,2, pH 5,5-6,0. " \
  "Taimi ei kestä vahvaa liuosta - juuret palavat.\n\n" \
  "Käynnistä NFT-kierto: ohut jatkuva kalvo, jonka kuitumatto levittää " \
  "tasaisesti.\n\n" \
  "Paina nappia kun ravinne kiertää."

// Askel 3: lamppu korkeudelle + valojakso (Vaihe 6). PPFD-tavoite ~300 on
// lehtiyrttien haarukka (profiilin ppfdTargetUmol); korkeussaato manuaali.
#define TXT_GEN_VEG_LIGHT \
  "Aja lamppu oikealle korkeudelle ja aseta valojakso (14-16 h/vrk).\n\n" \
  "Tavoite: PPFD noin 300 taimen latvassa. Liian lähellä polttaa, liian " \
  "kaukana taimi venyy. Seuraa PPFD-lukua e-inkistä ja säädä korkeutta.\n\n" \
  "Paina nappia kun valo on päällä."

// Askel 4: vakiinnuta ja seuraa (Vaihe 7). Pitka BUTTON-askel — kello (14 vrk)
// vaihtaa vain tekstin "kasva" -> "korjaa valmis", ei etene itsestaan.
#define TXT_GEN_VEG_GROW \
  "Kasvuvaihe on käynnissä. Anna kasvin juurtua ja tuuheutua.\n\n" \
  "Seuraa e-inkistä: VPD 0,8-1,2 kPa, lehtilämpö alle 28 °C, PPFD noin 300. " \
  "Nosta ravinnetta vähitellen kun kasvi vahvistuu.\n\n" \
  "Latvo kun kasvissa on 6-8 lehteä - se haaroittaa ja tuuheuttaa. " \
  "Paina nappia kun siirryt sadonkorjuuseen."

#define TXT_GEN_VEG_GROW_LATE \
  "Kasvi on tuuhea ja täysikasvuinen. Ensimmäiset sadonkorjuukelpoiset " \
  "latvat ovat valmiit.\n\n" \
  "Paina nappia kun aloitat sadonkorjuun - tai anna kasvun jatkua ja " \
  "korjaa myöhemmin."

// ── Sadonkorjuu (HARVEST), yleinen — aloitustavasta riippumaton ───────
// Lahde: grow_guidance.h HARVEST-ohje + ALOITUSOPAS-savy. Leikkuutekniikka
// (kaksi solmua, korkeintaan kolmasosa) saa kasvin haaroittumaan -> ratooning.

#define TXT_GEN_HARVEST_FIRST \
  "Kasvi on sadonkorjuukelpoinen. Leikkaa latvat kahden lehtiparin " \
  "yläpuolelta - älä korjaa yli kolmasosaa kerralla.\n\n" \
  "Leikkaus solmun yläpuolelta saa kasvin haaroittumaan: kaksi uutta versoa " \
  "jokaisen leikkauksen tilalle. Näin sato kasvaa joka korjuulla.\n\n" \
  "Paina nappia kun olet korjannut."

#define TXT_GEN_HARVEST_REPEAT \
  "Anna kasvin toipua ja haaroittua. Uudet versot kasvavat 5-7 " \
  "vuorokaudessa.\n\n" \
  "Pidä olosuhteet ennallaan: valo, ravinne, VPD. Sato jatkuu viikkoja " \
  "samasta kasvista.\n\n" \
  "Paina nappia kun korjaat seuraavan sadon."

#define TXT_GEN_HARVEST_REPEAT_LATE \
  "Uudet versot ovat kasvaneet - seuraava sato on valmis.\n\n" \
  "Leikkaa taas kahden lehtiparin yläpuolelta. Toista niin kauan kuin kasvi " \
  "tuottaa. Paina nappia jokaisen korjuun jälkeen."

// ═══════════════════════════════════════════════════════════════════
// ASKELLISTAT
// ═══════════════════════════════════════════════════════════════════

// Basilika, taimivaihe, aloitus siemenesta (Fable §3.1: valmistele -> liota
// -> kylva -> idata). Valmistele-askel (BUTTON) lisatty ennen liotusta:
// tarvikkeet ja pH-vesi kasaan ennen kuin 30 min ajastin lahtee.
static const GrowStep STEPS_BASIL_SEEDLING_SEED[] = {
  { TXT_STEP_PREP,      NULL,                    0UL,               STEP_ADVANCE_BUTTON },
  { TXT_STEP_SOAK,      NULL,                    30UL,              STEP_ADVANCE_TIMER  },
  { TXT_STEP_SOW,       NULL,                    0UL,               STEP_ADVANCE_BUTTON },
  { TXT_STEP_GERMINATE, TXT_STEP_GERMINATE_LATE, 5UL * 24UL * 60UL, STEP_ADVANCE_BUTTON },
};

// Basilika, kasvuvaihe (Fable §3.3). Kolme BUTTON-askelta, aloitustavasta
// riippumaton (siemen/pistokas jo takana) — rekisteroidaan STEP_METHOD_ANY.
static const GrowStep STEPS_BASIL_VEGETATIVE[] = {
  { TXT_BASIL_VEG_TRANSPLANT, NULL, 0UL, STEP_ADVANCE_BUTTON },
  { TXT_BASIL_VEG_NUTRIENT,   NULL, 0UL, STEP_ADVANCE_BUTTON },
  { TXT_BASIL_VEG_LIGHT,      NULL, 0UL, STEP_ADVANCE_BUTTON },
};

// Basilika, sadonkorjuu (Fable §3.5). Toinen askel toistuu (ratooning):
// textLate (5 vrk) vaihtaa "toivu" -> "seuraava sato valmis". Kuittaus
// paattaa sarjan pysyvasti.
static const GrowStep STEPS_BASIL_HARVEST[] = {
  { TXT_BASIL_HARVEST_FIRST,  NULL,                          0UL,               STEP_ADVANCE_BUTTON },
  { TXT_BASIL_HARVEST_REPEAT, TXT_BASIL_HARVEST_REPEAT_LATE, 5UL * 24UL * 60UL, STEP_ADVANCE_BUTTON },
};

// Rekisteri: (kasvi, vaihe, aloitustapa) -> askellista.
//
// plantId "" = kaikki kasvit. Haku suosii tarkkaa kasvi-osumaa ja putoaa
// sitten yleiseen. Jos osumaa ei ole lainkaan, askeleita EI ole ja laite
// kayttaytyy kuten ennenkin: yksi vaiheohje (grow_guidance.h). Askeleet
// ovat siis lisa, eivat korvaaja — 7 muuta kasvia eivat riko.
struct GrowStepList {
  const char*     plantId;
  uint8_t         phase;        // GrowPhaseType
  uint8_t         startMethod;  // 0 pistokas / 1 siemen / 2 kaupan taimi
  const GrowStep* steps;
  uint8_t         count;
};

// Yleinen siemenaloitus, taimivaihe (plantId "" = kaikki kasvit). Fallback
// jonka basilikan tarkka lista voittaa; muut kasvit saavat idatysaskeleet.
static const GrowStep STEPS_GENERIC_SEEDLING_SEED[] = {
  { TXT_GEN_SOAK,      NULL,                 30UL,              STEP_ADVANCE_TIMER  },
  { TXT_GEN_SOW,       NULL,                 0UL,               STEP_ADVANCE_BUTTON },
  { TXT_GEN_GERMINATE, TXT_GEN_GERMINATE_LATE, 5UL * 24UL * 60UL, STEP_ADVANCE_BUTTON },
};

// Yleinen pistokasaloitus, juurtumisvaihe (plantId "" = kaikki kasvit).
static const GrowStep STEPS_GENERIC_ROOTING_CUTTING[] = {
  { TXT_GEN_CUT,   NULL,          0UL,               STEP_ADVANCE_BUTTON },
  { TXT_GEN_STICK, NULL,          0UL,               STEP_ADVANCE_BUTTON },
  { TXT_GEN_ROOT,  TXT_GEN_ROOT_LATE, 7UL * 24UL * 60UL, STEP_ADVANCE_BUTTON },
};

// Yleinen kasvuvaihe (plantId "" + STEP_METHOD_ANY = kaikki kasvit ja
// aloitustavat). Neljas askel on pitka BUTTON: kello vaihtaa vain tekstin
// (14 vrk), kuittaus paattaa sarjan ja e-ink palaa mittarikortteihin.
static const GrowStep STEPS_GENERIC_VEGETATIVE[] = {
  { TXT_GEN_VEG_TRANSPLANT, NULL,                  0UL,                 STEP_ADVANCE_BUTTON },
  { TXT_GEN_VEG_NUTRIENT,   NULL,                  0UL,                 STEP_ADVANCE_BUTTON },
  { TXT_GEN_VEG_LIGHT,      NULL,                  0UL,                 STEP_ADVANCE_BUTTON },
  { TXT_GEN_VEG_GROW,       TXT_GEN_VEG_GROW_LATE, 14UL * 24UL * 60UL,  STEP_ADVANCE_BUTTON },
};

// Yleinen sadonkorjuu (plantId "" + STEP_METHOD_ANY). Toinen askel toistuu
// (ratooning): kello (5 vrk) vaihtaa tekstin "toivu" -> "seuraava sato valmis".
static const GrowStep STEPS_GENERIC_HARVEST[] = {
  { TXT_GEN_HARVEST_FIRST,  NULL,                        0UL,             STEP_ADVANCE_BUTTON },
  { TXT_GEN_HARVEST_REPEAT, TXT_GEN_HARVEST_REPEAT_LATE, 5UL * 24UL * 60UL, STEP_ADVANCE_BUTTON },
};

static const GrowStepList GROW_STEP_LISTS[] = {
  { "basil", GROW_PHASE_SEEDLING, 1, STEPS_BASIL_SEEDLING_SEED,
    (uint8_t)(sizeof(STEPS_BASIL_SEEDLING_SEED) / sizeof(STEPS_BASIL_SEEDLING_SEED[0])) },
  // Basilikan tarkat kasvuvaihe + sadonkorjuu (Fable §3.3/3.5), aloitustavasta
  // riippumattomat (STEP_METHOD_ANY) — voittavat geneerisen fallbackin.
  { "basil", GROW_PHASE_VEGETATIVE, STEP_METHOD_ANY, STEPS_BASIL_VEGETATIVE,
    (uint8_t)(sizeof(STEPS_BASIL_VEGETATIVE) / sizeof(STEPS_BASIL_VEGETATIVE[0])) },
  { "basil", GROW_PHASE_HARVEST, STEP_METHOD_ANY, STEPS_BASIL_HARVEST,
    (uint8_t)(sizeof(STEPS_BASIL_HARVEST) / sizeof(STEPS_BASIL_HARVEST[0])) },
  // Yleiset fallbackit (plantId "") — tarkka kasvi-osuma voittaa aina.
  { "", GROW_PHASE_SEEDLING, 1, STEPS_GENERIC_SEEDLING_SEED,
    (uint8_t)(sizeof(STEPS_GENERIC_SEEDLING_SEED) / sizeof(STEPS_GENERIC_SEEDLING_SEED[0])) },
  { "", GROW_PHASE_ROOTING, 0, STEPS_GENERIC_ROOTING_CUTTING,
    (uint8_t)(sizeof(STEPS_GENERIC_ROOTING_CUTTING) / sizeof(STEPS_GENERIC_ROOTING_CUTTING[0])) },
  // Taso B: kasvuvaihe + sadonkorjuu — samat kaikille aloitustavoille
  // (STEP_METHOD_ANY), koska siemen/pistokas-ero on jo takana.
  { "", GROW_PHASE_VEGETATIVE, STEP_METHOD_ANY, STEPS_GENERIC_VEGETATIVE,
    (uint8_t)(sizeof(STEPS_GENERIC_VEGETATIVE) / sizeof(STEPS_GENERIC_VEGETATIVE[0])) },
  { "", GROW_PHASE_HARVEST, STEP_METHOD_ANY, STEPS_GENERIC_HARVEST,
    (uint8_t)(sizeof(STEPS_GENERIC_HARVEST) / sizeof(STEPS_GENERIC_HARVEST[0])) },
};
#define GROW_STEP_LIST_COUNT ((int)(sizeof(GROW_STEP_LISTS) / sizeof(GROW_STEP_LISTS[0])))

// Haku annetusta taulukosta — taulukko parametrina jotta natiivitesti voi
// syottaa omat listansa (fallback-precedenssi testataan ilman etta oikeaa
// rekisteria tarvitsee muokata).
inline const GrowStep* grow_stepsForIn(const GrowStepList* lists, int listCount,
                                       const char* plantId,
                                       uint8_t phase,
                                       uint8_t startMethod,
                                       uint8_t* countOut) {
  // Precedenssi kahdella akselilla: tarkka kasvi > yleinen (""), ja tarkka
  // aloitustapa > jokeri (STEP_METHOD_ANY). Pisteytys valitsee parhaan:
  //   kasvi-tarkka (2) + tapa-tarkka (1) = 3  (paras)
  //   kasvi-tarkka (2) + tapa-jokeri     = 2
  //   yleinen        + tapa-tarkka (1)   = 1
  //   yleinen        + tapa-jokeri       = 0
  // Kun jokereita ei ole, tama on identtinen aiemman "tarkka voittaa
  // yleisen" -logiikan kanssa — tason A rivit kayttaytyvat ennallaan.
  const GrowStep* best      = NULL;
  uint8_t         bestCount = 0;
  int             bestScore = -1;

  for (int i = 0; i < listCount; i++) {
    const GrowStepList* L = &lists[i];
    if (L->phase != phase) continue;

    bool methodExact = (L->startMethod == startMethod);
    bool methodAny   = (L->startMethod == STEP_METHOD_ANY);
    if (!methodExact && !methodAny) continue;   // vaara aloitustapa

    bool plantExact   = (L->plantId[0] != '\0') && plantId &&
                        strcmp(L->plantId, plantId) == 0;
    bool plantGeneric = (L->plantId[0] == '\0');
    if (!plantExact && !plantGeneric) continue; // vaara kasvi

    int score = (plantExact ? 2 : 0) + (methodExact ? 1 : 0);
    if (score > bestScore) {
      bestScore = score;
      best      = L->steps;
      bestCount = L->count;
    }
  }

  if (countOut) *countOut = best ? bestCount : 0;
  return best;
}

// Palauta askellista annetulle yhdistelmalle, tai NULL jos askeleita ei ole
// (silloin kutsuja kayttaa vaiheen omaa yhta ohjetta).
inline const GrowStep* grow_stepsFor(const char* plantId,
                                     uint8_t phase,
                                     uint8_t startMethod,
                                     uint8_t* countOut) {
  return grow_stepsForIn(GROW_STEP_LISTS, GROW_STEP_LIST_COUNT,
                         plantId, phase, startMethod, countOut);
}

#endif // GROW_STEPS_H
