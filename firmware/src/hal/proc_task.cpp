// Suckboxx ESP32-liima - käsittelytaski. Ks. proc_task.h.

#include "hal/proc_task.h"

#include <Arduino.h>
#include <esp_timer.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include "hal/hal_config.h"
#include "hal/sampler.h"

namespace sbx {
namespace hal {
namespace {

constexpr int kStatSlots = kChannels + 1;  // kanavat + kisko

// Yhden ikkunan kertymä. Vain käsittelytaski käsittelee.
struct WindowAcc {
  uint32_t n = 0;
  uint32_t n_dt = 0;
  uint32_t sum_dt_us = 0;  // 100 ms ikkunassa enintään ~1e5, mahtuu
  uint32_t read_sum_us = 0;
  uint16_t dev_max_us = 0;
  uint16_t lag_max_us = 0;
  uint16_t read_max_us = 0;
  uint16_t q_peak = 0;
  // WP-7a: mV (alustimet tekevät oliosta vakioalustettavan, ei globaalia konstruktoria)
  uint16_t mv_min[kStatSlots] = {};
  uint16_t mv_max[kStatSlots] = {};
  uint32_t mv_sum[kStatSlots] = {};

  void reset() {
    *this = WindowAcc();
    for (int i = 0; i < kStatSlots; ++i) {
      mv_min[i] = 0xFFFF;
      mv_max[i] = 0;
      mv_sum[i] = 0;
    }
  }
};

// Sarjalokin jakso: ikkunat kootaan tähän mutexin alla kerran per ikkuna.
struct PeriodAcc {
  uint32_t windows = 0;
  uint32_t n = 0;
  uint32_t n_dt = 0;
  uint64_t sum_dt_us = 0;
  uint64_t read_sum_us = 0;
  uint16_t dev_max_us = 0;
  uint16_t lag_max_us = 0;
  uint16_t read_max_us = 0;
  uint16_t q_peak = 0;
};

template <typename T>
void keepMax(T& m, T v) {
  if (v > m) m = v;
}

TaskHandle_t g_task = nullptr;
SemaphoreHandle_t g_mutex = nullptr;
ProcWindow g_window;  // mutexin alla
PeriodAcc g_period;   // mutexin alla

// Käsittelytaskin oma tila
WindowAcc g_acc;
bool g_have_last = false;
uint32_t g_last_seq = 0;
uint32_t g_last_t_us = 0;
uint32_t g_late = 0;
uint32_t g_gaps = 0;
uint32_t g_windows = 0;
bool g_led = false;

void onSample(const SampleMsg& m) {
  const uint32_t seq = m.frame.seq;
  if (g_have_last) {
    const int32_t d = static_cast<int32_t>(seq - g_last_seq);
    if (d == 1) {
      const uint32_t dt = m.t_us - g_last_t_us;  // kiertoturvallinen
      g_acc.sum_dt_us += dt;
      ++g_acc.n_dt;
      const int32_t dev = static_cast<int32_t>(dt) - static_cast<int32_t>(kSamplePeriodUs);
      const uint32_t adev = static_cast<uint32_t>(dev < 0 ? -dev : dev);
      keepMax(g_acc.dev_max_us, static_cast<uint16_t>(adev > 65535u ? 65535u : adev));
    } else if (d > 1) {
      g_gaps += static_cast<uint32_t>(d - 1);  // pudotus (jono täynnä) tai ohitettu jakso
    }
    // d <= 0: seq ei kasvanut (ei pitäisi tapahtua) -> väliä ei lasketa
  }
  g_have_last = true;
  g_last_seq = seq;
  g_last_t_us = m.t_us;

  if (m.lag_us > kLateUs) ++g_late;
  keepMax(g_acc.lag_max_us, m.lag_us);
  keepMax(g_acc.read_max_us, m.read_us);
  g_acc.read_sum_us += m.read_us;
  ++g_acc.n;

  // 7b: tämä lohko korvautuu kutsulla processor.push(m.frame) (mutex otettu erää varten).
  for (int i = 0; i < kStatSlots; ++i) {
    const uint16_t mv = i < kChannels ? m.frame.mv[i] : m.frame.rail_mv;
    if (mv < g_acc.mv_min[i]) g_acc.mv_min[i] = mv;
    if (mv > g_acc.mv_max[i]) g_acc.mv_max[i] = mv;
    g_acc.mv_sum[i] += mv;
  }
}

void closeWindow(uint32_t now_ms) {
  ProcWindow w;
  w.t_ms = now_ms;
  w.index = ++g_windows;
  w.seq = g_last_seq;
  w.n = static_cast<uint16_t>(g_acc.n > 65535u ? 65535u : g_acc.n);
  w.fs_hz = g_acc.sum_dt_us > 0
                ? 1.0e6f * static_cast<float>(g_acc.n_dt) / static_cast<float>(g_acc.sum_dt_us)
                : 0.0f;
  w.dev_max_us = g_acc.dev_max_us;
  w.lag_max_us = g_acc.lag_max_us;
  w.read_mean_us = static_cast<uint16_t>(g_acc.n > 0 ? g_acc.read_sum_us / g_acc.n : 0);
  w.read_max_us = g_acc.read_max_us;
  w.q_peak = g_acc.q_peak;
  w.late = g_late;
  w.gaps = g_gaps;
  for (int i = 0; i < kStatSlots; ++i) {
    if (g_acc.n > 0) {
      w.mv_min[i] = g_acc.mv_min[i];
      w.mv_max[i] = g_acc.mv_max[i];
      w.mv_mean[i] = static_cast<uint16_t>((g_acc.mv_sum[i] + g_acc.n / 2) / g_acc.n);
    }
  }

  xSemaphoreTake(g_mutex, portMAX_DELAY);
  g_window = w;
  ++g_period.windows;
  g_period.n += g_acc.n;
  g_period.n_dt += g_acc.n_dt;
  g_period.sum_dt_us += g_acc.sum_dt_us;
  g_period.read_sum_us += g_acc.read_sum_us;
  keepMax(g_period.dev_max_us, g_acc.dev_max_us);
  keepMax(g_period.lag_max_us, g_acc.lag_max_us);
  keepMax(g_period.read_max_us, g_acc.read_max_us);
  keepMax(g_period.q_peak, g_acc.q_peak);
  xSemaphoreGive(g_mutex);

  g_acc.reset();

  if (g_windows % kLedToggleWindows == 0) {
    g_led = !g_led;
    digitalWrite(kPinLed, g_led ? HIGH : LOW);
  }
}

void procLoop(void*) {
  QueueHandle_t q = samplerQueue();
  g_acc.reset();
  uint32_t win_start_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(kProcPollMs));

    // Jonossa odottavat = huippu edellisen tyhjennyksen jälkeen (vain tämä taski poistaa).
    const UBaseType_t waiting = uxQueueMessagesWaiting(q);
    keepMax(g_acc.q_peak, static_cast<uint16_t>(waiting));

    // 7b: xSemaphoreTake(processor_mutex) tähän - yksi lukitus per erä (~4 kehystä).
    SampleMsg m;
    while (xQueueReceive(q, &m, 0) == pdTRUE) onSample(m);
    // 7b: processor.tick(now_s) 20 Hz:n välein ja vapautus tähän.

    const uint32_t now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    if (now_ms - win_start_ms >= kWindowMs) {
      closeWindow(now_ms);
      win_start_ms += kWindowMs;
      if (now_ms - win_start_ms >= kWindowMs) win_start_ms = now_ms;  // jäätiin jälkeen
    }
  }
}

}  // namespace

bool procStart() {
  if (g_task != nullptr || samplerQueue() == nullptr) return false;
  g_mutex = xSemaphoreCreateMutex();
  if (g_mutex == nullptr) return false;
  pinMode(kPinLed, OUTPUT);
  digitalWrite(kPinLed, LOW);
  return xTaskCreatePinnedToCore(procLoop, "sbx_proc", kProcStack, nullptr, kProcPrio, &g_task,
                                 kProcCore) == pdPASS;
}

void procWindow(ProcWindow& out) {
  if (g_mutex == nullptr) {
    out = ProcWindow();
    return;
  }
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  out = g_window;
  xSemaphoreGive(g_mutex);
}

void procTakePeriod(ProcPeriod& out) {
  out = ProcPeriod();
  if (g_mutex == nullptr) return;
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  const PeriodAcc p = g_period;
  g_period = PeriodAcc();
  xSemaphoreGive(g_mutex);

  out.windows = p.windows;
  out.n = p.n;
  // 10 s jaksossa summa ~1e7 µs < 2^24, joten float riittää tarkasti.
  out.fs_hz = p.sum_dt_us > 0
                  ? 1.0e6f * static_cast<float>(p.n_dt) / static_cast<float>(p.sum_dt_us)
                  : 0.0f;
  out.dev_max_us = p.dev_max_us;
  out.lag_max_us = p.lag_max_us;
  out.read_mean_us = static_cast<uint16_t>(p.n > 0 ? p.read_sum_us / p.n : 0);
  out.read_max_us = p.read_max_us;
  out.q_peak = p.q_peak;
}

TaskHandle_t procTask() { return g_task; }

}  // namespace hal
}  // namespace sbx
