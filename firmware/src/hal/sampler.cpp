// Suckboxx ESP32-liima - 1 kHz näytteenotto. Ks. sampler.h.

#include "hal/sampler.h"

#include <Arduino.h>
#include <driver/adc.h>
#include <esp_adc_cal.h>
#include <esp_timer.h>
#include <freertos/semphr.h>
#include <soc/adc_channel.h>

#include <atomic>

#include "hal/hal_config.h"
#include "hx710b.h"

// vTaskDelayUntil(&wake, 1) = 1 ms vain, jos tick on 1 kHz (sdkconfig CONFIG_FREERTOS_HZ).
// Arduino vaihtaa CPU:n 240 MHz:iin ajon aikana ja päivittää samalla tick-jakajan
// (esp32-hal-cpu.c:238), joten tick pysyy 1 ms:ssä.
static_assert(configTICK_RATE_HZ == 1000, "sampler olettaa 1 ms tickin (CONFIG_FREERTOS_HZ = 1000)");

namespace sbx {
namespace hal {
namespace {

uint16_t sat16(int64_t v) {
  if (v <= 0) return 0;
  if (v >= 65535) return 65535;
  return static_cast<uint16_t>(v);
}

// ---------------------------------------------------------------------------
// Lähde: simulaattori (D14). Parametrit tulevat postilaatikosta (alla).
// ---------------------------------------------------------------------------
class SimSource {
 public:
  // EngineSimin konstruktori laskee jo liukulukuja (derive). Olio luodaan
  // ensimmäisellä käytöllä taskissa (samplerStart setupista), ei globaalina
  // konstruktorina ennen schedulerin käynnistymistä, jolloin FPU:n käyttö
  // ilman taskia nojaisi CONFIG_FREERTOS_FPU_IN_ISR=y -polkuun.
  static EngineSim& sim() {
    static EngineSim s;
    return s;
  }
  void configure(const SimParams& p, const SensorParams& s) { sim().configure(p, s); }
  void read(RawFrame& f) {
    const RawFrame s = sim().next();  // simulaattorin oma seq ohitetaan: seq on samplerin
    for (int c = 0; c < kChannels; ++c) f.mv[c] = s.mv[c];
    f.rail_mv = s.rail_mv;
  }
};

// ---------------------------------------------------------------------------
// Lähde: ADC1 (D8), protolevyn kytkentä GPIO34/35/32 (docs/rauta.md §2.2:n
// poikkeama). rauta.md:n suunnitelma on GPIO34/35/36 + kisko GPIO39, mutta
// protolevyllä SYL 3 on GPIO32:ssa, eikä kiskomonitoria ole kytketty: rail_mv = 0
// ("ei mitattu", kuten simulaattorissa) - kelluva GPIO39 lukisi edellisen kanavan
// S&H-jäännöstä. Kun levy tehdään suunnitelman mukaan, palauta GPIO36 ja
// kAdcHasRail = true.
//
// Ei analogReadMilliVoltsia: se kutsuu joka lukukerralla __adcAttachPin:ia
// (pinMode -> gpio_config ja adc1_config_channel_atten) ja karakterisoi
// kalibroinnin uudelleen eFuseista (esp32-hal-adc.c:161, 187-254). Neljällä
// kanavalla 1 kHz:llä se olisi 4000 turhaa uudelleenkonfigurointia sekunnissa.
// Tässä konfiguroidaan kerran ja luetaan IDF:n adc1_get_raw:lla.
//
// Vaimennus 12 dB (IDF 4.4.7 nimeää vanhan 11 dB:n ADC_ATTEN_DB_12:ksi): ainoa,
// jolla 0,2-2,7 V mahtuu mittausalueeseen. Lineaarinen alue on n. 150-2450 mV;
// sen yläpuolella esp_adc_cal korjaa käyrää taulukolla (CONFIG_ADC_CAL_LUT_ENABLE,
// raw >= 2880). Ilmanpaineen 2,7 V (GN-anturin nollakohta) on siis taulukko-
// alueella - mitattava penkissä tunnetulla jännitteellä.
// ---------------------------------------------------------------------------
constexpr adc1_channel_t kAdcCh[kChannels] = {ADC1_GPIO34_CHANNEL, ADC1_GPIO35_CHANNEL,
                                               ADC1_GPIO32_CHANNEL};
constexpr bool kAdcHasRail = false;  // protolevyllä kiskomonitoria ei ole kytketty
constexpr adc1_channel_t kAdcRail = ADC1_GPIO39_CHANNEL;
constexpr adc_atten_t kAdcAtten = ADC_ATTEN_DB_12;
constexpr uint32_t kDefaultVrefMv = 1100;  // käytetään vain, jos eFusessa ei ole kalibrointia
constexpr int kAdcRawMax = 4095;
constexpr uint16_t kAdcFullScaleMv = 3300;
constexpr uint16_t kRailDivider = 2;  // kisko GPIO39:ään jakajalla 10 kΩ / 10 kΩ (rauta.md §3.3)

class AdcSource {
 public:
  void begin(AdcInfo& info) {
    adc1_config_width(ADC_WIDTH_BIT_12);
    for (int c = 0; c < kChannels; ++c) adc1_config_channel_atten(kAdcCh[c], kAdcAtten);
    if (kAdcHasRail) adc1_config_channel_atten(kAdcRail, kAdcAtten);
    // ESP32-erratum 3.11 (IDF driver/adc.h:224-230): kun SAR ADC:n virta kytketään,
    // GPIO36:n (SYL 3) ja GPIO39:n (kisko) tulo vetäytyy alas ~80 ns. adc1_get_raw
    // pyytää ja vapauttaa virran joka luvulla (adc1_rtc_mode_acquire ->
    // sar_periph_ctrl_adc_oneshot_power_acquire, viitelaskuri). Pysyvä viite pitää
    // virran päällä, jolloin vain SYL 3:een osuvaa epäsymmetristä häiriötä ei synny.
    // (Protolevyllä SYL 3 on GPIO32:ssa; pidetään silti suunnitelman levyä varten.)
    adc_power_acquire();
    info.cal_type = static_cast<int>(
        esp_adc_cal_characterize(ADC_UNIT_1, kAdcAtten, ADC_WIDTH_BIT_12, kDefaultVrefMv, &chars_));
    info.vref_mv = chars_.vref;
    info.coeff_a = chars_.coeff_a;
    info.coeff_b = chars_.coeff_b;
    info.raw0_mv = esp_adc_cal_raw_to_voltage(0, &chars_);
    info.raw4095_mv = esp_adc_cal_raw_to_voltage(kAdcRawMax, &chars_);
    info.valid = true;
  }

  void read(RawFrame& f) {
    // Lämmittelyluku per kanava: ADC1:n yhteinen S&H-kondensaattori ei ehdi
    // asettua 10 kΩ:n lähteen kautta, ja ilman pinnin 100 nF:ää (rauta.md §3.4)
    // edellisen kanavan jännite vuotaa lukemaan - protolevyllä imu yhteen
    // letkuun näkyi kaikissa kanavissa. Ensimmäinen luku hylätään. ~42 µs/luku.
    for (int c = 0; c < kChannels; ++c) {
      (void)adc1_get_raw(kAdcCh[c]);
      f.mv[c] = toMv(adc1_get_raw(kAdcCh[c]));
    }
    if (!kAdcHasRail) {
      f.rail_mv = 0;  // ei mitattu
      return;
    }
    const uint32_t rail = static_cast<uint32_t>(toMv(adc1_get_raw(kAdcRail))) * kRailDivider;
    f.rail_mv = static_cast<uint16_t>(rail > 65535u ? 65535u : rail);
  }

 private:
  // ADC:n lukualueen ulkopuoli koodataan kiskoarvoiksi 0 / 3300 mV, kuten
  // simulaattorin anturiviat (engine_sim.h kSensorLowMv / kSensorHighMv).
  // esp_adc_cal antaisi raw 0:lle offsetin coeff_b (eFuse Vref -karakterisoinnissa
  // 142 mV, libesp_adc_cal.a adc1_vref_atten_offset[3]) ja raw 4095:lle taulukon
  // pään 3060...3209 mV piirin Vrefistä riippuen (lut_adc1_low/high[19], Vref
  // 1100 mV -> ~3134 mV). 142 mV ei alita rivin 7 rajaa fault_lo_mv = 100 eikä
  // ~3134 mV ylitä rajaa fault_hi_mv = 3150, eli maadoittunut tai kiskoon
  // oikosulkeutunut anturi jäisi huomaamatta.
  uint16_t toMv(int raw) const {
    if (raw <= 0) return 0;
    if (raw >= kAdcRawMax) return kAdcFullScaleMv;
    return static_cast<uint16_t>(esp_adc_cal_raw_to_voltage(static_cast<uint32_t>(raw), &chars_));
  }

  esp_adc_cal_characteristics_t chars_{};
};

// ---------------------------------------------------------------------------
// Lähde: HX710B-penkkitesti (D14) olemassa olevalla luokalla (src/hx710b.h).
//
// Moduuli tuottaa 10 SPS, joten kehyksessä on viimeisin lukema pidettynä
// (nollannen asteen pito). Hx710b::readRaw pitää keskeytyksiä kiellettynä
// 25 kellopulssin ajan (~65 µs ytimellä 1) - lyhyesti, mutta siksi enintään
// yksi luku per 1 ms:n jakso ja kanava kerrallaan vuorotellen. kHxMinIntervalUs
// estää b838911:n virheen: kytkemätön, kelluva DOUT lukee LOW, isReady() on aina
// tosi, ja ilman rajaa luettaisiin joka jaksossa.
//
// HX710B:n lukema ei ole jännite. Se kuvataan virtuaaliseksi pinnijännitteeksi
// 0...3300 mV (0 count = 1650 mV), jotta kehys kulkee samaa polkua; kPa-luvut
// ovat tällä lähteellä merkityksettömiä (anturi 0-40 kPa, SUUNNITTELU.md §3.1).
// ---------------------------------------------------------------------------
class HxSource {
 public:
  void begin() {
    for (int c = 0; c < kChannels; ++c) hx_[c].begin(kPinHxSck[c], kPinHxDout[c]);
  }

  void read(RawFrame& f, int64_t now_us, std::atomic<uint32_t>& bad) {
    for (int k = 0; k < kChannels; ++k) {
      const int c = (next_ + k) % kChannels;
      if (now_us < due_us_[c] || !hx_[c].isReady()) continue;
      int32_t raw = 0;
      if (hx_[c].readRaw(raw)) {
        hold_mv_[c] = countsToMv(raw);
      } else {
        bad.fetch_add(1, std::memory_order_relaxed);
      }
      due_us_[c] = now_us + kHxMinIntervalUs;
      next_ = (c + 1) % kChannels;
      break;  // enintään yksi luku per jakso
    }
    for (int c = 0; c < kChannels; ++c) f.mv[c] = hold_mv_[c];
    f.rail_mv = 0;  // ei mitattu
  }

 private:
  static uint16_t countsToMv(int32_t raw) {
    const int64_t mv = 1650 + (static_cast<int64_t>(raw) * 1650) / 8388608;  // 2^23
    if (mv < 0) return 0;
    if (mv > 3300) return 3300;
    return static_cast<uint16_t>(mv);
  }

  Hx710b hx_[kChannels];
  uint16_t hold_mv_[kChannels] = {0, 0, 0};  // 0 = ei vielä lukemaa
  int64_t due_us_[kChannels] = {0, 0, 0};
  int next_ = 0;
};

// ---------------------------------------------------------------------------
// Tila. Lähteet ovat sampler-taskin omia samplerStartin jälkeen.
// ---------------------------------------------------------------------------
SourceKind g_src = SourceKind::Sim;
SimSource g_sim;
AdcSource g_adc;
HxSource g_hx;
AdcInfo g_adc_info;

QueueHandle_t g_queue = nullptr;
TaskHandle_t g_task = nullptr;

std::atomic<uint32_t> g_queue_drops{0};
std::atomic<uint32_t> g_skipped{0};
std::atomic<uint32_t> g_hx_bad{0};

// Simulaattorin postilaatikko: asettaja (mikä tahansa taski) kirjoittaa mutexin
// alla, sampler poimii try-lockilla (xSemaphoreTake(..., 0)) eikä siis koskaan
// odota eikä peri asettajan prioriteettia.
SemaphoreHandle_t g_sim_mutex = nullptr;
std::atomic<uint32_t> g_sim_pending{0};
SimParams g_sim_next;           // mutexin alla
SensorParams g_sim_next_sensor;  // mutexin alla
SimParams g_sim_current;        // mutexin alla: viimeisin sovellettu

void applyPendingSim() {
  if (g_sim_pending.load(std::memory_order_acquire) == 0) return;
  if (xSemaphoreTake(g_sim_mutex, 0) != pdTRUE) return;  // seuraavalla jaksolla
  g_sim.configure(g_sim_next, g_sim_next_sensor);
  g_sim_current = g_sim_next;
  g_sim_pending.store(0, std::memory_order_release);
  xSemaphoreGive(g_sim_mutex);
}

void readSource(RawFrame& f, int64_t now_us) {
  switch (g_src) {
    case SourceKind::Adc:
      g_adc.read(f);
      break;
    case SourceKind::Hx710b:
      g_hx.read(f, now_us, g_hx_bad);
      break;
    case SourceKind::Sim:
    default:
      g_sim.read(f);
      break;
  }
}

// Tahti on tick-laskurin (wake), ja ideal_us on sama tahti mikrosekunteina
// ankkuroituna ensimmäiseen herätykseen, jolloin herätysviive ei näy lagina.
// Alle kCatchUpMaxUs myöhästyminen otetaan kiinni: vTaskDelayUntil palaa heti,
// kunnes tahti on saavutettu, ja näytteet ovat oikeassa seq-järjestyksessä
// (myöhässä, mutta korkeintaan muutaman ms). Suurempi myöhästyminen ohitetaan.
void samplerLoop(void*) {
  TickType_t wake = xTaskGetTickCount();
  vTaskDelayUntil(&wake, 1);  // ensimmäinen näyte tickin kohdalle
  int64_t ideal_us = esp_timer_get_time();
  uint32_t seq = 0;
  for (;;) {
    const int64_t t0 = esp_timer_get_time();
    const int64_t lag = t0 - ideal_us;
    if (lag >= static_cast<int64_t>(kCatchUpMaxUs)) {
      // Jäätiin jälkeen (flash-operaatio pysäytti ytimen, pitkä keskeytys).
      // Jaksot wake..nyt (tämäkin) jäävät ilman näytettä: seq hyppää niiden yli,
      // jolloin seq pysyy tickien tahdissa, ja tahti ankkuroidaan seuraavaan
      // tickiin. (Ankkurointi nykyhetkeen mikrosekunnin tarkkuudella menisi
      // tick-rajan lähellä pieleen ja jättäisi pysyvän ~1 ms:n lagin.)
      const TickType_t now_tick = xTaskGetTickCount();
      const uint32_t missed = static_cast<uint32_t>(now_tick - wake) + 1;
      seq += missed;
      g_skipped.fetch_add(missed, std::memory_order_relaxed);
      wake = now_tick;
      vTaskDelayUntil(&wake, 1);
      ideal_us = esp_timer_get_time();
      continue;
    }

    applyPendingSim();

    SampleMsg m;
    const int64_t r0 = esp_timer_get_time();
    readSource(m.frame, t0);
    m.read_us = sat16(esp_timer_get_time() - r0);  // vain lähde: CPU-budjetti per lähde
    m.frame.seq = seq++;
    m.t_us = static_cast<uint32_t>(t0);
    m.lag_us = sat16(lag);
    if (xQueueSend(g_queue, &m, 0) != pdTRUE) {
      g_queue_drops.fetch_add(1, std::memory_order_relaxed);
    }

    ideal_us += kSamplePeriodUs;
    vTaskDelayUntil(&wake, 1);
  }
}

}  // namespace

bool samplerStart(SourceKind src) {
  if (g_task != nullptr) return false;
  g_src = src;
  g_queue = xQueueCreate(kQueueLen, sizeof(SampleMsg));
  g_sim_mutex = xSemaphoreCreateMutex();
  if (g_queue == nullptr || g_sim_mutex == nullptr) return false;

  // Lähde alustetaan kutsujan taskissa (setup), jotta AdcInfo on heti lokattavissa.
  // Simulaattori luodaan aina: postilaatikko (samplerSetSim) toimii lähteestä riippumatta.
  SimSource::sim();
  switch (src) {
    case SourceKind::Adc:
      g_adc.begin(g_adc_info);
      break;
    case SourceKind::Hx710b:
      g_hx.begin();
      break;
    case SourceKind::Sim:
    default:
      break;  // EngineSim on valmis oletusparametreilla
  }

  return xTaskCreatePinnedToCore(samplerLoop, "sbx_sample", kSamplerStack, nullptr, kSamplerPrio,
                                 &g_task, kSamplerCore) == pdPASS;
}

QueueHandle_t samplerQueue() { return g_queue; }
SourceKind samplerSource() { return g_src; }
TaskHandle_t samplerTask() { return g_task; }
const AdcInfo& samplerAdcInfo() { return g_adc_info; }

void samplerCounters(SamplerCounters& out) {
  out.queue_drops = g_queue_drops.load(std::memory_order_relaxed);
  out.skipped = g_skipped.load(std::memory_order_relaxed);
  out.hx_bad = g_hx_bad.load(std::memory_order_relaxed);
}

const char* sourceName(SourceKind s) {
  switch (s) {
    case SourceKind::Adc:
      return "adc";
    case SourceKind::Hx710b:
      return "hx710b";
    case SourceKind::Sim:
    default:
      return "sim";
  }
}

void samplerSetSim(const SimParams& p, const SensorParams& s) {
  if (g_sim_mutex == nullptr) return;
  xSemaphoreTake(g_sim_mutex, portMAX_DELAY);
  g_sim_next = p;
  g_sim_next_sensor = s;
  g_sim_pending.store(1, std::memory_order_release);
  xSemaphoreGive(g_sim_mutex);
}

void samplerGetSim(SimParams& out) {
  if (g_sim_mutex == nullptr) {
    out = SimParams();
    return;
  }
  xSemaphoreTake(g_sim_mutex, portMAX_DELAY);
  out = g_sim_pending.load(std::memory_order_acquire) != 0 ? g_sim_next : g_sim_current;
  xSemaphoreGive(g_sim_mutex);
}

}  // namespace hal
}  // namespace sbx
