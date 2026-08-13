/*=====================================================================
  onboarding.h - Kayttoonoton askeltila ja valmistumisportti

  Yksi totuus sille, milloin kayttoonoton saa merkita valmiiksi.

  ── Miksi tama on olemassa ───────────────────────────────────────────
  Portaalin kayttoonottobanneri listasi neljä askelta, mutta sen ainoa
  nappi ("Valmis — ota laite kayttoon") oli visuaalisesti dominoivin
  elementti eika validoinut mitaan: painallus kuittasi kayttoonoton
  tehdyksi vaikka yhtaan askelta ei ollut tehty, banneri katosi
  pysyvasti (palaa vain tehdasresetissa) ja laite jai IDLE:en ilman
  seuraavaa toimintoa. Nappi oli portin sijaan pakoluukku — todettu
  kayttajan ensikaytossa 16.7.2026 ("ei edennyt enaa loogisesti").

  Portti on tassa eika selaimessa, koska disabloitu nappi on
  kaytettavyytta — API on totuus. /api/onboarding/complete kysyy
  onboarding_canComplete():lta, joten UI:n ohittava kutsu saa saman
  vastauksen kuin nappia painava kayttaja.

  ── Miksi askeltilaa ei voi paatella olemassa olevasta configista ────
  currentPlantId on aina "basil" ja growStartMethod aina jokin arvo
  (config_defaults.h) — oletusta ei voi erottaa kayttajan valinnasta.
  Siksi valinnat kirjataan bitteina sita mukaa kun POST-reitit ottavat
  ne vastaan.

  ── Miksi PLANT ja METHOD ovat erilliset bitit ───────────────────────
  Aloitustapa on se kentta jonka vaara arvo tuottaa vaarat ohjeet:
  siemenen kylvanyt sai basilikan pistokasohjeen ("Leikkaa 10-15 cm
  varsi solmun alta"), koska nappi ylikirjoitti valinnan nollalla
  (kayttoonotto-onboarding-kartoitus.md §7 kohta 2). Pelkan kasvin
  valinta ei siksi saa kuitata askelta 2 valmiiksi. Bannerissa ne
  nakyvat yhtena askeleena, joka on ✓ vasta kun molemmat ovat.

  ── Miksi WIFI ja PIN eivat ole pakollisia ───────────────────────────
  AP-only offline on ensiluokkainen tila ja PIN on valinnainen
  (kartoitus §4.3). Ne kirjataan vain jotta banneri voi nayttaa ✓:n.

  Pure + header-only -> natiivitestattava ilman Arduino-runtimea
  (test/test_onboarding_gate).
=====================================================================*/

#ifndef ONBOARDING_H
#define ONBOARDING_H

#include <stdint.h>

// Kayttoonoton askeleet. Persistoidaan DeviceConfig.onboardingSteps
// -bittimaskina (structs.h), jotta banneri sailyy oikeana yli rebootin.
#define ONBOARD_STEP_WIFI    0x01   // Kotiverkko asetettu (valinnainen)
#define ONBOARD_STEP_PLANT   0x02   // Kasvi valittu (pakollinen)
#define ONBOARD_STEP_METHOD  0x04   // Aloitustapa valittu (pakollinen)
#define ONBOARD_STEP_PIN     0x08   // Hallinta-PIN asetettu (valinnainen)

// Askeleet joita ilman kayttoonottoa ei saa merkita valmiiksi.
#define ONBOARD_REQUIRED_MASK (ONBOARD_STEP_PLANT | ONBOARD_STEP_METHOD)

// Saako kayttoonoton merkita valmiiksi tassa tilassa?
static inline bool onboarding_canComplete(uint8_t steps) {
  return (steps & ONBOARD_REQUIRED_MASK) == ONBOARD_REQUIRED_MASK;
}

// Pakolliset askeleet jotka viela puuttuvat. Palautetaan API-vastauksessa
// ja naytetaan napin alla, jotta "en voi painaa Valmis" kertoo aina myos
// MIKSI — juuri sen puutteen takia alkuperainen banneri hammensi.
static inline uint8_t onboarding_missingRequired(uint8_t steps) {
  return (uint8_t)(ONBOARD_REQUIRED_MASK & ~steps);
}

#endif // ONBOARDING_H
