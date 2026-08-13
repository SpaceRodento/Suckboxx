/*=====================================================================
  test_status_targets - status_targets.h (PE-tavoitemalli tilannenaytolle)

  Kattaa puhtaan logiikan jonka status_view.h piirtaa: vaihekohtaisten
  tavoitteiden valinta, LOW/OK/HIGH-luokittelu (myos pelkka katto,
  PE_NO_LIMIT), nykyarvon luku DisplayDatasta validiteetteineen, ja
  HOIDAN/AUTA-jako. Nama ovat ne paatokset joista valmennuspaneelin rivit
  syntyvat — regressio nakyisi vaarana neuvona seinataululla.

  <stdint.h> ennen status_targets.h:ta: natiivitestissa ei ole .ino-wrapperia
  joka toisi Arduino.h:n uint8_t:n (sama kuin test_layout_def_tables).
=====================================================================*/

#include <stdint.h>
#include <string.h>
#include <unity.h>

#include "status_targets.h"

void setUp() {}
void tearDown() {}

// ── Aktiivisen vaiheen tavoitejoukon valinta DisplayDatasta (M1) ────────

// Laite julkaisi kaistat -> ne kaytetaan sellaisenaan, FieldId-muunnettuna.
void test_device_targets_used_when_present() {
  DisplayData d = {};
  d.peTargetCount = 2;
  strncpy(d.peTargets[0].field, "vpd", sizeof(d.peTargets[0].field));
  d.peTargets[0].lo = 0.4f; d.peTargets[0].hi = 0.8f;
  strncpy(d.peTargets[1].field, "dli", sizeof(d.peTargets[1].field));
  d.peTargets[1].lo = 6.0f; d.peTargets[1].hi = 10.0f;

  int n = 0;
  const PeTarget* t = status_targetsForData(&d, &n);
  TEST_ASSERT_EQUAL_INT(2, n);
  TEST_ASSERT_EQUAL_INT(FIELD_VPD, t[0].field);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.4f, t[0].lo);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.8f, t[0].hi);
  TEST_ASSERT_EQUAL_INT(FIELD_DLI, t[1].field);
}

// Laite ei julkaissut mitaan (vanha firmware, tai ei aktiivista vaihetta)
// -> fallback-taulukko, sama joka ennen M1:ta oli "Kasvuvaihe"-oletus.
void test_no_device_targets_falls_back() {
  DisplayData d = {};
  d.peTargetCount = 0;
  int n = 0;
  const PeTarget* t = status_targetsForData(&d, &n);
  TEST_ASSERT_EQUAL_INT(PE_TARGET_FALLBACK_COUNT, n);
  TEST_ASSERT_EQUAL_PTR(PE_TARGETS_FALLBACK, t);
}

void test_null_display_data_falls_back() {
  int n = 0;
  const PeTarget* t = status_targetsForData(nullptr, &n);
  TEST_ASSERT_EQUAL_PTR(PE_TARGETS_FALLBACK, t);
  TEST_ASSERT_EQUAL_INT(PE_TARGET_FALLBACK_COUNT, n);
}

// Tuntematon field-avain (esim. tulevan firmwaren uusi mitta) ohitetaan,
// ei kaadeta eika lasketa mukaan count:iin.
void test_unknown_field_key_is_skipped() {
  DisplayData d = {};
  d.peTargetCount = 2;
  strncpy(d.peTargets[0].field, "vpd", sizeof(d.peTargets[0].field));
  d.peTargets[0].lo = 0.4f; d.peTargets[0].hi = 0.8f;
  strncpy(d.peTargets[1].field, "n2o", sizeof(d.peTargets[1].field));  // tuntematon
  d.peTargets[1].lo = 1.0f; d.peTargets[1].hi = 2.0f;

  int n = 0;
  const PeTarget* t = status_targetsForData(&d, &n);
  TEST_ASSERT_EQUAL_INT(1, n);
  TEST_ASSERT_EQUAL_INT(FIELD_VPD, t[0].field);
}

// Kaikki avaimet tuntemattomia -> fallback, ei tyhja taulukko.
void test_all_unknown_field_keys_falls_back() {
  DisplayData d = {};
  d.peTargetCount = 1;
  strncpy(d.peTargets[0].field, "n2o", sizeof(d.peTargets[0].field));
  int n = 0;
  const PeTarget* t = status_targetsForData(&d, &n);
  TEST_ASSERT_EQUAL_PTR(PE_TARGETS_FALLBACK, t);
  TEST_ASSERT_EQUAL_INT(PE_TARGET_FALLBACK_COUNT, n);
}

// ── Luokittelu LOW/OK/HIGH ─────────────────────────────────────────────

void test_classify_low_ok_high() {
  PeTarget t = { FIELD_VPD, 0.8f, 1.2f, false };
  TEST_ASSERT_EQUAL_INT(PE_LOW,  status_classify(0.5f, &t));
  TEST_ASSERT_EQUAL_INT(PE_OK,   status_classify(1.0f, &t));
  TEST_ASSERT_EQUAL_INT(PE_HIGH, status_classify(1.5f, &t));
  // Rajat kuuluvat OK-alueeseen (ei tiukka epayhtalo reunalla).
  TEST_ASSERT_EQUAL_INT(PE_OK,   status_classify(0.8f, &t));
  TEST_ASSERT_EQUAL_INT(PE_OK,   status_classify(1.2f, &t));
}

// Pelkka katto (lehtilampo): ei ala-rajaa -> matala arvo on OK, vain yli = HIGH.
void test_classify_max_only() {
  PeTarget t = { FIELD_LEAF_TEMP, PE_NO_LIMIT, 28.0f, false };
  TEST_ASSERT_EQUAL_INT(PE_OK,   status_classify(10.0f, &t));
  TEST_ASSERT_EQUAL_INT(PE_OK,   status_classify(28.0f, &t));
  TEST_ASSERT_EQUAL_INT(PE_HIGH, status_classify(30.0f, &t));
}

// ── Nykyarvon luku + validiteetti ──────────────────────────────────────

static DisplayData makeData() {
  DisplayData d = {};
  d.dataValid = true;
  return d;
}

void test_field_value_reads_valid() {
  DisplayData d = makeData();
  d.vpdKpa = 1.5f; d.vpdValid = true;
  float v = 0;
  TEST_ASSERT_TRUE(status_fieldValue(FIELD_VPD, &d, &v));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.5f, v);
}

void test_field_value_invalid_returns_false() {
  DisplayData d = makeData();
  d.leafTempValid = false;      // MLX rikki — nykyarvo ei saatavilla
  float v = 0;
  TEST_ASSERT_FALSE(status_fieldValue(FIELD_LEAF_TEMP, &d, &v));
}

// DLI on validi jos ppfdValid TAI dli>0 (peilaa resolve_field.h:ta).
void test_field_value_dli_from_accumulator() {
  DisplayData d = makeData();
  d.ppfdValid = false; d.dli = 5.0f;
  float v = 0;
  TEST_ASSERT_TRUE(status_fieldValue(FIELD_DLI, &d, &v));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.0f, v);
}

// evalTarget: invalidi anturi -> PE_NODATA (ei valmennusrivia, ei nollaa).
void test_eval_target_nodata_when_invalid() {
  DisplayData d = makeData();
  d.co2Valid = false;
  PeTarget t = { FIELD_CO2, 400.0f, 800.0f, false };
  TEST_ASSERT_EQUAL_INT(PE_NODATA, status_evalTarget(&t, &d, nullptr));
}

// ── HOIDAN/AUTA-jako (pe-ohjausmalli §3) ───────────────────────────────

// V1:ssa mikaan naista ei ole suljettu silmukka (ei PPFD- eika DLI-ohjausta) -
// lampun korkeus ja valoaika ovat kasin saadettavia (kasvatus-kasikirjoitus-
// basilika.md §0.2). Kaikki viisi ovat siis AUTA. Aiemmin DLI/PPFD olivat
// virheellisesti HOIDAN ("saadan automaattisesti") - korjattu 22.7.2026.
void test_all_fields_are_user_actioned_in_v1() {
  const PeTarget* t = PE_TARGETS_FALLBACK;
  for (int i = 0; i < PE_TARGET_FALLBACK_COUNT; i++) {
    TEST_ASSERT_FALSE_MESSAGE(t[i].deviceActs,
        "V1:ssa mikaan PE-tavoite ei ole suljettu silmukka - kaikki AUTA");
  }
}

// ── Valmennusviesti ────────────────────────────────────────────────────

void test_coach_message_on_out_of_range() {
  const char* title; const char* body;
  status_coachMessage(FIELD_VPD, PE_HIGH, &title, &body);
  TEST_ASSERT_TRUE(title[0] != '\0');
  TEST_ASSERT_TRUE(body[0] != '\0');
}

void test_coach_message_empty_when_ok() {
  const char* title; const char* body;
  status_coachMessage(FIELD_VPD, PE_OK, &title, &body);
  TEST_ASSERT_EQUAL_STRING("", title);
  TEST_ASSERT_EQUAL_STRING("", body);
}

// ── PPFD/DLI-redundanssi (kayttajan rautahavainnot 22.7. + 28.7.2026) ───
// Suunta kaannettiin 28.7.2026: PPFD-rivi piiloutuu DLI:n taakse, ei toisin
// pain, koska korttirivilla nakyy DLI ja neuvon yksikon on vastattava sita.

void test_ppfd_redundant_when_dli_same_direction() {
  TEST_ASSERT_TRUE(status_ppfdRedundantWithDli(PE_LOW, PE_LOW));
  TEST_ASSERT_TRUE(status_ppfdRedundantWithDli(PE_HIGH, PE_HIGH));
}

void test_ppfd_not_redundant_when_dli_differs_or_missing() {
  TEST_ASSERT_FALSE(status_ppfdRedundantWithDli(PE_LOW, PE_HIGH));
  TEST_ASSERT_FALSE(status_ppfdRedundantWithDli(PE_LOW, PE_OK));
  TEST_ASSERT_FALSE(status_ppfdRedundantWithDli(PE_LOW, PE_NODATA));
}

void test_ppfd_redundancy_check_skipped_when_ppfd_itself_ok_or_nodata() {
  TEST_ASSERT_FALSE(status_ppfdRedundantWithDli(PE_OK, PE_OK));
  TEST_ASSERT_FALSE(status_ppfdRedundantWithDli(PE_NODATA, PE_LOW));
}

// ── STATUS_CARDS-taulun eheys (peilaa test_layout_def_tables:ia) ───────

static bool isAsciiOnly(const char* s) {
  if (!s) return true;
  for (const unsigned char* p = (const unsigned char*)s; *p; p++)
    if (*p < 0x20 || *p > 0x7E) return false;
  return true;
}

void test_status_cards_count_is_four() {
  TEST_ASSERT_EQUAL_INT(4, STATUS_CARD_COUNT);
}

void test_status_cards_valid_and_ascii() {
  for (int i = 0; i < STATUS_CARD_COUNT; i++) {
    TEST_ASSERT_TRUE(STATUS_CARDS[i].field > FIELD_NONE && STATUS_CARDS[i].field <= FIELD_ENERGY_WH);
    TEST_ASSERT_NOT_NULL(STATUS_CARDS[i].label);
    TEST_ASSERT_TRUE(STATUS_CARDS[i].label[0] != '\0');
    TEST_ASSERT_TRUE_MESSAGE(isAsciiOnly(STATUS_CARDS[i].label), "STATUS_CARDS label ei-ASCII");
  }
}

// ── status_formatTarget: tavoiteikkuna luettavaksi ─────────────────────
// Tama on se teksti joka kertoo kayttajalle MIHIN pyritaan. Ilman sita
// "Ilma kuiva" jaa vertailukohdatta (kayttajan havainto 27.7.2026: RH 61 %
// nayttaa arkijarjella kostealta, vaikka VPD 1.03 kPa on taimelle kuiva).

void test_format_target_range_with_decimals() {
  PeTarget t = { FIELD_VPD, 0.4f, 0.8f, false };
  char out[24];
  status_formatTarget(&t, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("tav 0.4-0.8 kPa", out);
}

void test_format_target_range_integer_units() {
  char out[24];
  PeTarget dli = { FIELD_DLI, 6.0f, 10.0f, false };
  status_formatTarget(&dli, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("tav 6-10 mol", out);

  PeTarget co2 = { FIELD_CO2, 400.0f, 800.0f, false };
  status_formatTarget(&co2, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("tav 400-800 ppm", out);

  PeTarget ppfd = { FIELD_PPFD, 100.0f, 200.0f, false };
  status_formatTarget(&ppfd, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("tav 100-200 umol", out);
}

// Pelkka katto (lehtilampo) -> "tav <28 C", ei "tav -1.0-28".
void test_format_target_max_only() {
  PeTarget t = { FIELD_LEAF_TEMP, PE_NO_LIMIT, 28.0f, false };
  char out[24];
  status_formatTarget(&t, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("tav <28 C", out);
}

// Tuntematon kentta / NULL -> tyhja (kutsuja jattaa tavoitteen piirtamatta).
void test_format_target_unknown_field_is_empty() {
  PeTarget t = { FIELD_AIR_RH, 40.0f, 60.0f, false };
  char out[24];
  status_formatTarget(&t, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("", out);

  status_formatTarget(nullptr, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("", out);
}

// Jokaiselle fallback-taulukon tavoitteelle on tuotettava teksti — muuten
// jokin valmennusrivi jaisi ilman tavoitetta ilman etta kukaan huomaa.
// (Laitteen julkaisemat kaistat kulkevat saman status_formatTarget():n lapi
// samalla PeTarget-muodolla, joten yksi taulukko riittaa kattamaan koodin.)
void test_format_target_covers_fallback_table() {
  for (int i = 0; i < PE_TARGET_FALLBACK_COUNT; i++) {
    char out[24];
    status_formatTarget(&PE_TARGETS_FALLBACK[i], out, sizeof(out));
    TEST_ASSERT_TRUE_MESSAGE(out[0] != '\0', "PE-tavoite ilman tavoitetekstia");
    // Ei katkaisua: 24 tavua riittaa pisimmalle ("tav 100-200 umol" = 17).
    TEST_ASSERT_TRUE_MESSAGE(strlen(out) < sizeof(out) - 1, "tavoiteteksti katkesi");
  }
}

// ── Skandikonventio: valmennusviestit kulkevat status_u8*():n lapi ─────
// -> niiden PITAA olla validia UTF-8:aa. Yksinainen \xE4-tavu naytettaisi
// katkenneelta sekvenssilta ja merkki KATOAISI HILJAA piirrossa. Nama olivat
// ASCII:na 19.7.-27.7.2026 vanhentuneen "GxEPD2 ei piirra 0xE4" -oletuksen
// takia; testi estaa seka paluun ASCII:hin etta vaaran \xE4-konvention.

static bool isValidUtf8(const char* s) {
  const unsigned char* p = (const unsigned char*)s;
  while (*p) {
    if (*p < 0x80) { p++; continue; }
    int need;
    if      ((*p & 0xE0) == 0xC0) need = 1;
    else if ((*p & 0xF0) == 0xE0) need = 2;   // yksinainen 0xE4 osuu tahan
    else if ((*p & 0xF8) == 0xF0) need = 3;
    else return false;
    for (int i = 1; i <= need; i++) if ((p[i] & 0xC0) != 0x80) return false;
    p += need + 1;
  }
  return true;
}

void test_coach_messages_are_valid_utf8() {
  const uint8_t fields[] = { FIELD_VPD, FIELD_CO2, FIELD_LEAF_TEMP, FIELD_DLI, FIELD_PPFD };
  const PeStatus dirs[]  = { PE_LOW, PE_HIGH };
  for (int f = 0; f < (int)(sizeof(fields) / sizeof(fields[0])); f++) {
    for (int s = 0; s < 2; s++) {
      const char* title; const char* body;
      status_coachMessage(fields[f], dirs[s], &title, &body);
      TEST_ASSERT_TRUE_MESSAGE(isValidUtf8(title), "valmennusotsikko ei ole validia UTF-8:aa");
      TEST_ASSERT_TRUE_MESSAGE(isValidUtf8(body),  "valmennusohje ei ole validia UTF-8:aa");
    }
  }
}

// Positiivinen puoli: skandit ovat oikeasti mukana, ei ASCII-taantumaa.
// "lisaa" laskisi lapi UTF-8-tarkistuksesta mutta olisi juuri se regressio
// joka elettiin 19.7.-27.7.2026. Odotukset kirjoitettu UTF-8:na kuten lahde.
void test_coach_messages_contain_scandics() {
  const char* title; const char* body;

  status_coachMessage(FIELD_VPD, PE_HIGH, &title, &body);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(body, "vettä"),
      "VPD HIGH -ohje: 'vetta' ilman aakkosia?");

  status_coachMessage(FIELD_LEAF_TEMP, PE_HIGH, &title, &body);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(body, "jäähdyttää"),
      "Lehti kuuma -ohje: 'jaahdyttaa' ilman aakkosia?");

  status_coachMessage(FIELD_DLI, PE_LOW, &title, &body);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(body, "lisää"),
      "DLI LOW -ohje: 'lisaa' ilman aakkosia?");

  status_coachMessage(FIELD_PPFD, PE_LOW, &title, &body);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(body, "lähemmäs"),
      "PPFD LOW -ohje: 'lahemmas' ilman aakkosia?");
}

// Liian valon neuvo EI saa nimeta lamppua. Laite ei ohjaa valonlahdetta eika
// tieda kumpi se on; 29.7.2026 kasvit olivat suorassa auringossa ja naytto
// neuvoi "nosta lamppua kauemmas" — toimenpide jota ei ollut olemassa.
// Mekaaninen vartija, koska sanamuoto on helppo palauttaa vahingossa.
void test_high_light_advice_does_not_name_the_lamp() {
  const char* title; const char* body;

  status_coachMessage(FIELD_PPFD, PE_HIGH, &title, &body);
  TEST_ASSERT_NULL_MESSAGE(strstr(body, "lamp"),
      "PPFD HIGH -ohje nimeaa lampun; aurinko ei nouse komennosta");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(body, "varjosta"),
      "PPFD HIGH -ohje ei neuvo varjostamaan");

  status_coachMessage(FIELD_DLI, PE_HIGH, &title, &body);
  TEST_ASSERT_NULL_MESSAGE(strstr(body, "lamp"),
      "DLI HIGH -ohje nimeaa lampun; aurinko ei nouse komennosta");

  // LOW-suunnat kuuluvat samaan vartijaan. Ne jaivat 29.7.2026 ensimmaisella
  // kierroksella pois, jolloin "laske lamppua" eli DLI:n ohjeessa viikon yli
  // vaikka HIGH-tekstit oli jo puhdistettu. Vartija joka kattaa vain puolet
  // suunnista ei estanyt juuri sita virhetta jota varten se kirjoitettiin.
  status_coachMessage(FIELD_DLI, PE_LOW, &title, &body);
  TEST_ASSERT_NULL_MESSAGE(strstr(body, "lamp"),
      "DLI LOW -ohje nimeaa lampun; parvekkeella sita ei ole");

  status_coachMessage(FIELD_PPFD, PE_LOW, &title, &body);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(body, "siirrä"),
      "PPFD LOW -ohje ei tarjoa siirtoa vaihtoehdoksi lampun saadolle");
}


// Alaraja ei voi todistaa alitusta -> PE_LOW vaihtuu PE_NODATA:ksi.
void test_saturated_ppfd_not_classified_low() {
  DisplayData d = {};
  d.ppfdValid = true; d.ppfd = 96.8f; d.ppfdSaturated = true;
  PeTarget t = { FIELD_PPFD, 150.0f, PE_NO_LIMIT, false };
  TEST_ASSERT_EQUAL_INT(PE_NODATA, status_evalTarget(&t, &d, nullptr));
}

// Alaraja todistaa silti YLITYKSEN: jos jo alaraja ylittaa katon, todellinen
// arvo ylittaa sen varmasti. Se paatelma alaraja kantaa.
void test_saturated_ppfd_can_still_prove_high() {
  DisplayData d = {};
  d.ppfdValid = true; d.ppfd = 500.0f; d.ppfdSaturated = true;
  PeTarget t = { FIELD_PPFD, 100.0f, 300.0f, false };
  TEST_ASSERT_EQUAL_INT(PE_HIGH, status_evalTarget(&t, &d, nullptr));
}

// Saturoimaton alitus luokitellaan normaalisti.
void test_unsaturated_ppfd_classified_low() {
  DisplayData d = {};
  d.ppfdValid = true; d.ppfd = 40.0f; d.ppfdSaturated = false;
  PeTarget t = { FIELD_PPFD, 150.0f, PE_NO_LIMIT, false };
  TEST_ASSERT_EQUAL_INT(PE_LOW, status_evalTarget(&t, &d, nullptr));
}

// ── Ristiriitainen pari: PPFD korkea + DLI matala (29.7.2026) ───────

// Ilman vartijaa naytto sanoisi yhta aikaa "varjosta harsolla" ja
// "lisaa valoaikaa" — jalkimmainen kasvattaisi juuri sita mika on jo liikaa.
void test_dli_low_hidden_when_ppfd_high() {
  TEST_ASSERT_TRUE(status_dliContradictsPpfd(PE_LOW, PE_HIGH));
}

// Muissa yhdistelmissa DLI-rivi jaa nakyviin. Erityisesti PE_LOW+PE_LOW on
// saman suunnan tapaus, jossa piilotetaan PPFD (status_ppfdRedundantWithDli),
// ei DLI — muuten valosumman neuvo katoaisi kokonaan.
void test_dli_visible_in_other_combinations() {
  TEST_ASSERT_FALSE(status_dliContradictsPpfd(PE_LOW, PE_LOW));
  TEST_ASSERT_FALSE(status_dliContradictsPpfd(PE_LOW, PE_NODATA));
  TEST_ASSERT_FALSE(status_dliContradictsPpfd(PE_LOW, PE_OK));
  TEST_ASSERT_FALSE(status_dliContradictsPpfd(PE_HIGH, PE_HIGH));
  TEST_ASSERT_FALSE(status_dliContradictsPpfd(PE_OK, PE_HIGH));
}

// Kaksi vartijaa eivat saa piilottaa MOLEMPIA rivejä samasta tilanteesta.
// Sama suunta -> vain PPFD katoaa; vastakkainen -> vain DLI katoaa.
void test_guards_never_hide_both_rows() {
  const PeStatus dirs[] = { PE_NODATA, PE_LOW, PE_OK, PE_HIGH };
  for (int a = 0; a < 4; a++) {
    for (int b = 0; b < 4; b++) {
      const bool hidePpfd = status_ppfdRedundantWithDli(dirs[a], dirs[b]);
      const bool hideDli  = status_dliContradictsPpfd(dirs[b], dirs[a]);
      TEST_ASSERT_FALSE_MESSAGE(hidePpfd && hideDli,
          "molemmat valorivit piilotettiin samasta tilanteesta");
    }
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_device_targets_used_when_present);
  RUN_TEST(test_no_device_targets_falls_back);
  RUN_TEST(test_null_display_data_falls_back);
  RUN_TEST(test_unknown_field_key_is_skipped);
  RUN_TEST(test_all_unknown_field_keys_falls_back);
  RUN_TEST(test_classify_low_ok_high);
  RUN_TEST(test_classify_max_only);
  RUN_TEST(test_field_value_reads_valid);
  RUN_TEST(test_field_value_invalid_returns_false);
  RUN_TEST(test_field_value_dli_from_accumulator);
  RUN_TEST(test_eval_target_nodata_when_invalid);
  RUN_TEST(test_all_fields_are_user_actioned_in_v1);
  RUN_TEST(test_coach_message_on_out_of_range);
  RUN_TEST(test_coach_message_empty_when_ok);
  RUN_TEST(test_ppfd_redundant_when_dli_same_direction);
  RUN_TEST(test_ppfd_not_redundant_when_dli_differs_or_missing);
  RUN_TEST(test_ppfd_redundancy_check_skipped_when_ppfd_itself_ok_or_nodata);
  RUN_TEST(test_status_cards_count_is_four);
  RUN_TEST(test_status_cards_valid_and_ascii);
  RUN_TEST(test_format_target_range_with_decimals);
  RUN_TEST(test_format_target_range_integer_units);
  RUN_TEST(test_format_target_max_only);
  RUN_TEST(test_format_target_unknown_field_is_empty);
  RUN_TEST(test_format_target_covers_fallback_table);
  RUN_TEST(test_coach_messages_are_valid_utf8);
  RUN_TEST(test_coach_messages_contain_scandics);
  RUN_TEST(test_high_light_advice_does_not_name_the_lamp);
  RUN_TEST(test_dli_low_hidden_when_ppfd_high);
  RUN_TEST(test_dli_visible_in_other_combinations);
  RUN_TEST(test_guards_never_hide_both_rows);
  RUN_TEST(test_saturated_ppfd_not_classified_low);
  RUN_TEST(test_saturated_ppfd_can_still_prove_high);
  RUN_TEST(test_unsaturated_ppfd_classified_low);
  return UNITY_END();
}
