#pragma once
// Suckboxx core - raakadatan rengaspuskuri ja CSV-vienti (WP-3, S5b, R3,
// docs/firmware.md §4.5).
//
// S5b: ensimmäinen moottorikäynti (M6) on ainoa tilaisuus nähdä, miltä
// signaali oikeasti näyttää (tasauskanavat, paluuaallot, H1). Siksi laite
// pitää muistissa viimeiset ~8 s raakoja millivoltteja, ja "Tallenna" vie ne
// CSV:nä puhelimeen. Raaka-mV eikä kPa, koska kalibrointia ja suodatusta
// viritetään vielä tällä samalla datalla (vaihe 4 / M7).
//
// Muisti: RawSample on 8 tavua, joten 8192 näytettä = 64 kt (staattinen
// taulukko ESP32:lla). Täysi 32-bittinen seq ei mahdu, joten näytteeseen
// tallennetaan vain alin 16 bittiä ja täysi numero rekonstruoidaan uusimmasta.
// Se on yksikäsitteinen, kun renkaan vanhin ja uusin ovat < 65536 näytteen
// päässä toisistaan - push() pitää siitä huolen.
//
// Säikeet: sampler-taski kutsuu push():ia 1 kHz:llä ja web-taski lukee.
// Lukitus on HAL-liiman vastuulla; CSV:n ajan rengas on jäädytetty
// (freeze), jolloin push() on ei-op eikä luettava data liiku. Jäädytys on
// viitelaskettu: jokainen lataus kutsuu freeze() kerran ja unfreeze() kerran
// (myös kun asiakas katkaisee yhteyden kesken), jolloin rinnakkaiset lataukset
// eivät vapauta rengasta toistensa alta.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "calibration.h"
#include "config.h"
#include "json_writer.h"
#include "types.h"

namespace sbx {

struct RawSample {
  uint16_t seq_lo;
  uint16_t mv[kChannels];
};
static_assert(sizeof(RawSample) == 8, "RawSample on 8 tavua (64 kt / 8192 näytettä)");

class RawRing {
 public:
  // 16-bittinen seq yksilöi näytteen tämän ikkunan sisällä.
  static constexpr uint32_t kSeqWindow = 0xFFFFu;

  // Muisti kutsujalta (ESP32: staattinen). nullptr tai 0 -> rengas, johon
  // mikään ei tallennu (ei kaadu).
  RawRing(RawSample* storage, size_t capacity)
      : buf_(storage), cap_(storage != nullptr ? capacity : 0) {}

  void push(const RawFrame& f) {
    if (frozen()) {
      // Jäädytyksen aikana tahallaan pudotetut näytteet eivät ole näytteenoton
      // vikoja: seuraavaa hyppyä ei lasketa gaps():iin (diag.gaps mittaa
      // samplerin terveyttä, ei CSV-latauksia).
      skip_gap_ = true;
      return;
    }
    if (cap_ == 0) return;
    if (count_ > 0) {
      const uint32_t d = f.seq - newest_seq_;  // modulo 2^32: kestää 32-bittisen kierron
      if (d != 1) {
        if (!skip_gap_) ++gaps_;
        // Taaksepäin tai sama numero: lähde käynnistyi uudelleen (esim. vaihto
        // simulaattorista ADC:hen). Vanha data on eri aikasarjaa, eikä sen seq
        // olisi enää rekonstruoitavissa -> aloitetaan puhdas tallenne.
        if (d == 0 || d > 0x7FFFFFFFu) clear();
      }
      // Pudotetaan vanhimmat, joiden seq ei enää mahdu 16-bittiseen ikkunaan
      // uuden näytteen kanssa (pitkä aukko tai kapasiteetti > 65536).
      // Kukin näyte pudotetaan korkeintaan kerran, joten tämä on tasoitetusti O(1).
      while (count_ > 0 && f.seq - expandSeq(newest_seq_, buf_[start_].seq_lo) > kSeqWindow) {
        start_ = next(start_);
        --count_;
      }
    }
    skip_gap_ = false;

    size_t idx;
    if (count_ < cap_) {
      idx = start_ + count_;
      if (idx >= cap_) idx -= cap_;
      ++count_;
    } else {  // täynnä: vanhin yli
      idx = start_;
      start_ = next(start_);
    }
    RawSample& s = buf_[idx];
    s.seq_lo = static_cast<uint16_t>(f.seq & 0xFFFFu);
    for (int c = 0; c < kChannels; ++c) s.mv[c] = f.mv[c];
    newest_seq_ = f.seq;
    ++version_;
  }

  // Viitelaskettu jäädytys. Pelkkä bool ei riitä: AsyncWebServer ajaa kaksi
  // GET /api/raw.csv -vastausta rinnakkain (tuplanapautus "Tallenna",
  // selaimen uusintapyyntö), ja ensimmäisen unfreeze() vapauttaisi renkaan
  // toisen alta - S5b:n ainoa tallenne korruptoituisi hiljaa. Ylimääräinen
  // unfreeze() on ei-op, jotta pariton kutsu ei jää jäädyttämään ikuisesti.
  void freeze() { ++freeze_count_; }
  void unfreeze() {
    if (freeze_count_ > 0) --freeze_count_;
  }
  bool frozen() const { return freeze_count_ > 0; }

  // Tyhjentää datan (esim. lähteen vaihto). gaps() säilyy: se on diagnostiikkaa.
  void clear() {
    start_ = 0;
    count_ = 0;
    ++version_;
  }

  // Kasvaa jokaisella sisältöä muuttavalla push():lla ja clear():lla.
  // CsvWriter vertaa tätä tunnistaakseen, että rengas muuttui viennin aikana
  // (jäädytys unohtui tai vapautettiin liian aikaisin).
  uint32_t version() const { return version_; }

  size_t size() const { return count_; }
  size_t capacity() const { return cap_; }
  // Uusimman näytteen täysi seq; 0 jos mitään ei ole vielä tallennettu.
  uint32_t newestSeq() const { return newest_seq_; }
  // Havaitut seq-epäjatkuvuudet (hyppy = yksi aukko, koosta riippumatta).
  // Kertyvä, ei nollaudu clear():lla.
  uint32_t gaps() const { return gaps_; }

  // i = 0 on vanhin. false jos i ≥ size().
  bool at(size_t i, RawSample& out) const {
    if (i >= count_) return false;
    size_t idx = start_ + i;
    if (idx >= cap_) idx -= cap_;
    out = buf_[idx];
    return true;
  }

  // Näytteen täysi seq renkaan nykyisestä uusimmasta.
  uint32_t seqOf(const RawSample& s) const { return expandSeq(newest_seq_, s.seq_lo); }

  // newest - (newest_lo - lo) mod 2^16: oikein kun näyte on enintään 65535
  // näytettä uusinta vanhempi (renkaan invariantti).
  static uint32_t expandSeq(uint32_t newest, uint16_t lo) {
    const uint16_t back = static_cast<uint16_t>(static_cast<uint16_t>(newest & 0xFFFFu) - lo);
    return newest - back;
  }

 private:
  size_t next(size_t i) const { return i + 1 < cap_ ? i + 1 : 0; }

  RawSample* buf_;
  size_t cap_;
  size_t start_ = 0;   // vanhimman indeksi
  size_t count_ = 0;
  uint32_t newest_seq_ = 0;
  uint32_t gaps_ = 0;
  uint32_t version_ = 0;
  uint32_t freeze_count_ = 0;
  bool skip_gap_ = false;
};

// CSV paloina AsyncWebServerin chunked-vastaukseen. Rengas on jäädytettynä koko
// ajan (docs/firmware.md §4.5). Kirjoitin ottaa konstruktorissa kopion
// anturiparametreista ja kalibroinnista, jotta otsake ja kPa-sarakkeet
// vastaavat toisiaan, vaikka asetuksia muutettaisiin latauksen aikana.
// Jos rengas silti muuttuu kesken (ei jäädytetty), vienti lopetetaan ennen
// ensimmäistä väärää riviä ja loppuun kirjoitetaan "# aborted=..."-rivi:
// katkennut tiedosto ei saa näyttää ehjältä.
//
// Muoto: '#'-alkuiset avain=arvo-metatietorivit (pandas: comment='#'), sitten
// sarakeotsake ja rivit. Desimaalipiste, '\n'-rivinvaihdot, pelkkää ASCII:ta.
// t_ms lasketaan seq:stä, joten pudonneet näytteet näkyvät aikahyppynä eivätkä
// siirrä myöhempiä näytteitä väärään kohtaan.
class CsvWriter {
 public:
  static constexpr int kFormatVersion = 1;

  CsvWriter(const RawRing& ring, const Config& cfg, const Calibration& cal)
      : ring_(ring), sensor_(cfg.sensor), source_(cfg.source), cal_(cal),
        count_(ring.size()), newest_(ring.newestSeq()), gaps_total_(ring.gaps()),
        version_(ring.version()) {
    // Aukot juuri tässä ikkunassa (kertyvä gaps() sisältää myös jo pois
    // pyörineet). Yksi O(n)-läpikäynti latauksen alussa.
    RawSample prev{}, s{};
    for (size_t i = 0; i < count_ && ring_.at(i, s); ++i) {
      if (i == 0) {
        first_ = RawRing::expandSeq(newest_, s.seq_lo);
      } else if (static_cast<uint16_t>(s.seq_lo - prev.seq_lo) != 1) {
        ++gaps_window_;
      }
      prev = s;
    }
  }

  // Kirjoittaa enintään max_len tavua (ei NUL-päätettä). Palauttaa
  // kirjoitettujen tavujen määrän; 0 = valmis. Tulos on tavulleen sama
  // palakoosta riippumatta, koska rivit muotoillaan sisäiseen puskuriin ja
  // kopioidaan sieltä kohdasta, johon edellinen pala jäi.
  size_t fill(char* buf, size_t max_len) {
    if (buf == nullptr) return 0;
    size_t out = 0;
    while (out < max_len) {
      if (pos_ >= len_ && !nextLine()) break;
      size_t m = len_ - pos_;
      if (m > max_len - out) m = max_len - out;
      std::memcpy(buf + out, line_ + pos_, m);
      out += m;
      pos_ += m;
    }
    return out;
  }

  bool done() const { return stage_ == Stage::Done && pos_ >= len_; }
  // Rengas muuttui viennin aikana; tuloste päättyy "# aborted=..."-riviin.
  bool aborted() const { return aborted_; }

 private:
  enum class Stage : uint8_t { Header, Columns, Rows, Done };

  // Seuraava rivi line_:iin. false kun kaikki on kirjoitettu.
  bool nextLine() {
    pos_ = 0;
    len_ = 0;
    switch (stage_) {
      case Stage::Header:
        if (header(hdr_line_)) {
          ++hdr_line_;
          return true;
        }
        stage_ = Stage::Columns;
        return nextLine();
      case Stage::Columns:
        setLine(std::snprintf(line_, sizeof(line_),
                              "seq,t_ms,ch1_mv,ch2_mv,ch3_mv,ch1_kpa,ch2_kpa,ch3_kpa\n"));
        stage_ = Stage::Rows;
        return true;
      case Stage::Rows:
        if (row_ >= count_) {
          stage_ = Stage::Done;
          return false;
        }
        if (row(row_)) {
          ++row_;
          return true;
        }
        // Rengas muuttui: otsakkeen samples= ei enää pidä, joten kerrotaan
        // montako riviä ehti tulla. '#'-rivi, jotta pandas (comment='#') lukee
        // ehjän alkuosan.
        aborted_ = true;
        stage_ = Stage::Done;
        setLine(std::snprintf(line_, sizeof(line_), "# aborted=ring_changed rows=%lu/%lu\n",
                              static_cast<unsigned long>(row_),
                              static_cast<unsigned long>(count_)));
        return true;
      case Stage::Done:
        return false;
    }
    return false;
  }

  void setLine(int r) {
    // snprintf palauttaa tarvittavan pituuden; katkaistu rivi (ei pitäisi
    // tapahtua, puskuri on mitoitettu pahimman tapauksen mukaan) rajataan.
    if (r < 0) r = 0;
    len_ = static_cast<size_t>(r) < sizeof(line_) ? static_cast<size_t>(r) : sizeof(line_) - 1;
  }

  // Metatietorivi numero k; false kun otsake on valmis.
  bool header(int k) {
    char a[48], b[48], c[48];  // mikä tahansa äärellinen float mahtuu
    switch (k) {
      case 0:
        setLine(std::snprintf(line_, sizeof(line_), "# Suckboxx raakadata (S5b)\n"));
        return true;
      case 1:
        setLine(std::snprintf(line_, sizeof(line_), "# device=suckboxx\n# format=%d\n",
                              kFormatVersion));
        return true;
      case 2:
        setLine(std::snprintf(line_, sizeof(line_), "# source=%s\n# fs_hz=%lu\n", sourceName(),
                              static_cast<unsigned long>(kSampleRateHz)));
        return true;
      case 3:
        formatFixed(a, sizeof(a), sensor_.v_zero_mv, 3, true);
        formatFixed(b, sizeof(b), sensor_.mv_per_kpa, 4, true);
        setLine(std::snprintf(line_, sizeof(line_), "# v_zero_mv=%s\n# mv_per_kpa=%s\n", a, b));
        return true;
      case 4:
        formatFixed(a, sizeof(a), sensor_.rail_nominal_mv, 1, true);
        setLine(std::snprintf(line_, sizeof(line_), "# rail_comp=%d\n# rail_nominal_mv=%s\n",
                              sensor_.rail_comp ? 1 : 0, a));
        return true;
      case 5:
        setLine(std::snprintf(line_, sizeof(line_), "# zero_valid=%d\n# gain_valid=%d\n",
                              cal_.zero_valid ? 1 : 0, cal_.gain_valid ? 1 : 0));
        return true;
      case 6:
        formatFixed(a, sizeof(a), cal_.offset_kpa[0], 4, true);
        formatFixed(b, sizeof(b), cal_.offset_kpa[1], 4, true);
        formatFixed(c, sizeof(c), cal_.offset_kpa[2], 4, true);
        setLine(std::snprintf(line_, sizeof(line_), "# offset_kpa=%s,%s,%s\n", a, b, c));
        return true;
      case 7:
        // gain ~1: 6 desimaalia on floatin tarkkuus, jolloin kPa:n voi laskea
        // CSV:stä uudelleen samoin kuin laite.
        formatFixed(a, sizeof(a), cal_.gain[0], 6, true);
        formatFixed(b, sizeof(b), cal_.gain[1], 6, true);
        formatFixed(c, sizeof(c), cal_.gain[2], 6, true);
        setLine(std::snprintf(line_, sizeof(line_), "# gain=%s,%s,%s\n", a, b, c));
        return true;
      case 8:
        setLine(std::snprintf(line_, sizeof(line_), "# samples=%lu\n# first_seq=%lu\n",
                              static_cast<unsigned long>(count_),
                              static_cast<unsigned long>(count_ > 0 ? first_ : 0u)));
        return true;
      case 9:
        setLine(std::snprintf(line_, sizeof(line_), "# gaps=%lu\n# gaps_total=%lu\n",
                              static_cast<unsigned long>(gaps_window_),
                              static_cast<unsigned long>(gaps_total_)));
        return true;
      case 10:
        // kPa-sarakkeet ilman kiskokorjausta: rail_mv:tä ei tallenneta renkaaseen.
        setLine(std::snprintf(line_, sizeof(line_),
                              "# kpa=gain*(mv-v_zero_mv)/mv_per_kpa+offset (ei kiskokorjausta)\n"));
        return true;
      default:
        return false;
    }
  }

  bool row(size_t i) {
    // Muuttuneessa renkaassa indeksi i osoittaa eri näytteeseen, ja seq
    // rekonstruoituisi vanhentuneesta newest_:sta (hyppy taaksepäin 65536,
    // t_ms kymmeniä minuutteja). Tarkistus ennen jokaista riviä: jäädytettynä
    // version() ei voi muuttua, joten ehjä vienti ei koskaan keskeydy.
    if (ring_.version() != version_) return false;
    RawSample s;
    if (!ring_.at(i, s)) return false;
    const uint32_t seq = RawRing::expandSeq(newest_, s.seq_lo);
    // Rengas pitää vanhimman ja uusimman < 65536 näytteen päässä, joten
    // 32-bittinen kertolasku ei vuoda yli.
    const uint32_t t_ms = (seq - first_) * 1000u / kSampleRateHz;
    char k[kChannels][48];  // mahtuu mikä tahansa äärellinen float (±3.4e38 + 3 desimaalia)
    for (int c = 0; c < kChannels; ++c) {
      const float kpa = applyCal(nominalKpa(s.mv[c], 0, sensor_), c, cal_);
      formatFixed(k[c], sizeof(k[c]), kpa, 3);
    }
    setLine(std::snprintf(line_, sizeof(line_), "%lu,%lu,%u,%u,%u,%s,%s,%s\n",
                          static_cast<unsigned long>(seq), static_cast<unsigned long>(t_ms),
                          static_cast<unsigned>(s.mv[0]), static_cast<unsigned>(s.mv[1]),
                          static_cast<unsigned>(s.mv[2]), k[0], k[1], k[2]));
    return true;
  }

  const char* sourceName() const {
    switch (source_) {
      case SourceKind::Sim: return "sim";
      case SourceKind::Adc: return "adc";
      case SourceKind::Hx710b: return "hx710b";
    }
    return "unknown";
  }

  const RawRing& ring_;
  SensorParams sensor_;
  SourceKind source_;
  Calibration cal_;
  size_t count_;
  uint32_t newest_;
  uint32_t gaps_total_;
  uint32_t version_;
  uint32_t first_ = 0;
  uint32_t gaps_window_ = 0;
  bool aborted_ = false;

  Stage stage_ = Stage::Header;
  int hdr_line_ = 0;
  size_t row_ = 0;
  // Pisin rivi: seq 10 + t_ms 10 + 3 × 5 (mV) + 3 × 45 (kPa, pahin float) +
  // 8 erotinta = 178 merkkiä; tavallinen rivi ~45. Otsakerivit < 80.
  char line_[192] = {0};
  size_t len_ = 0;
  size_t pos_ = 0;
};

}  // namespace sbx
