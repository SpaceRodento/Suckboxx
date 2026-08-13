/*=====================================================================
  onboarding_view.h - Kayttoonotto-ruutu (onboarding overlay)

  Kun PlantMeister raportoi /api/state device.onboarding == true,
  tama nakymma ohittaa KAIKKI view-profiilit (USER/DEV/RAW_TABLE/COACH)
  ja nayttaa ummikolle askel-askeleelta-ohjeen:

    (a) ei kotiverkkoa:  "Yhdista WiFi: PlantMeister -> 192.168.4.1"
    (b) STA yhdistetty:  "Avaa selaimessa: http://<sta_ip>"
        (kayttoonotto kesken, mutta puhelin loytaa laitteen kotiverkosta)

  Kaytto: view_profiles.h kutsuu onboarding_view_active() ennen profiilin
  dispatchia. Render-on-change toimii: contentHash muuttuu kun staIp
  ilmestyy tai onboarding valmistuu -> paneeli paivittyy heti.

  Suunnittelu: docs/kehitys/kayttoonotto-onboarding-kartoitus.md §4.1/§5.2.
  Skandit SUORINA Latin-1-tavuina (ä=\xE4 ö=\xF6 å=\xE5): nama ovat e-inkin
  OMIA literaaleja, jotka menevat suoraan display.print():iin eivatka kulje
  data_fetch_parse.h:n UTF-8 -> Latin-1 -muunnoksen lapi. Latin-1-tavu on jo
  valmis -> GxEPD2 piirtaa sen oikein (todistettu raudalla 21.7.2026), ei
  muunnosta eika tuplamuunnosriskia. ALA lisaa muunnosta tanne (layout_def.h § 2).
=====================================================================*/

#ifndef ONBOARDING_VIEW_H
#define ONBOARDING_VIEW_H

#include "display_data.h"
#include "display_layout.h"   // display, fonts, drawCentered

static bool onboarding_view_active(const DisplayData* d) {
  // Vain kun data on oikeasti haettu PM:lta. Jos PM ei vastaa lainkaan,
  // emme voi tietaa onko se tuore vai vain pois paalta -> normaali
  // fallback/showroom hoitaa sen (ks. kartoitus §7 avoin kysymys 1).
  return d && d->dataValid && d->onboarding;
}

static uint32_t onboarding_view_contentHash(const DisplayData* d) {
  // FNV-1a: nakyva sisalto = ruutuvariantti (a/b) + staIp-teksti.
  uint32_t h = 2166136261u;
  const char* tag = "ONBOARDING";
  while (*tag) { h ^= (uint8_t)(*tag++); h *= 16777619u; }
  const char* s = d->staIp;
  while (*s) { h ^= (uint8_t)(*s++); h *= 16777619u; }
  return h;
}

static void onboarding_view_render(const DisplayData* d) {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    drawCentered(&FreeSansBold24pt8b, "PlantMeister", 110);
    display.drawLine(120, 140, 680, 140, GxEPD_BLACK);

    if (d->staIp[0] != '\0') {
      // (b) Kotiverkko yhdistetty, kayttoonotto viela kesken.
      drawCentered(&FreeSansBold18pt8b, "Yhdistetty kotiverkkoon", 210);
      char buf[48];
      snprintf(buf, sizeof(buf), "Avaa selaimessa:  http://%s", d->staIp);
      drawCentered(&FreeSansBold12pt8b, buf, 265);
      drawCentered(&FreeSans9pt8b, "Jatka k\xE4ytt\xF6\xF6nottoa puhelimella tai koneella", 310);
    } else {
      // (a) Tuore laite: ohjaa AP:hen.
      drawCentered(&FreeSansBold18pt8b, "Otetaan laite k\xE4ytt\xF6\xF6n", 200);
      drawCentered(&FreeSansBold12pt8b, "1. Yhdist\xE4 puhelin WiFi-verkkoon: PlantMeister", 255);
      drawCentered(&FreeSansBold12pt8b, "2. Avaa selaimessa: 192.168.4.1", 295);
      drawCentered(&FreeSans9pt8b, "Asetussivu avautuu automaattisesti", 335);
    }

    drawCentered(&FreeSans9pt8b,
                 "Laite ei aloita kasvatusta itsest\xE4\xE4n - kasvatus alkaa Aloita-toiminnosta",
                 420);
  } while (display.nextPage());
  display.hibernate();
}

#endif // ONBOARDING_VIEW_H
