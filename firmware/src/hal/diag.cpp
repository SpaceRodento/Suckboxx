// Suckboxx ESP32-liima - diagnostiikan kokoaja. Ks. diag.h.

#include "hal/diag.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_timer.h>

#include <atomic>
#include <cstdio>

#include "hal/hal_config.h"
#include "hal/proc_task.h"
#include "hal/sampler.h"
#include "hal/web.h"
#include "hal/wifi_ap.h"

namespace sbx {
namespace hal {
namespace {

// Hitaat arvot päivitetään enintään kerran sekunnissa. heap_caps_get_largest_
// free_block käy koko keon läpi keon spinlockin alla (keskeytykset pois), joten
// sitä ei ajeta 15 Hz:llä eikä ytimellä 1. diagJsonia kutsutaan vain ytimen 0
// taskeista (sbx_push, async_tcp), joten läpikäynti osuu verkkoytimelle.
std::atomic<uint32_t> g_slow_ms{0};
std::atomic<uint32_t> g_slow_busy{0};
std::atomic<uint32_t> g_heap_block{0};
std::atomic<uint32_t> g_sta{0};

void refreshSlow(uint32_t now_ms) {
  const uint32_t last = g_slow_ms.load(std::memory_order_relaxed);
  if (last != 0 && now_ms - last < kSlowRefreshMs) return;
  uint32_t expected = 0;
  if (!g_slow_busy.compare_exchange_strong(expected, 1)) return;  // toinen taski päivittää
  g_heap_block.store(static_cast<uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
                     std::memory_order_relaxed);
  g_sta.store(wifiApStations(), std::memory_order_relaxed);
  g_slow_ms.store(now_ms != 0 ? now_ms : 1, std::memory_order_relaxed);
  g_slow_busy.store(0);
}

unsigned long ul(uint32_t v) { return static_cast<unsigned long>(v); }

const char* calTypeName(int t) {
  switch (t) {
    case 0:
      return "eFuse Vref";
    case 1:
      return "eFuse TP";
    case 2:
      return "oletus-Vref";
    case 3:
      return "eFuse TP+fit";
    default:
      return "?";
  }
}

uint32_t stackFree(TaskHandle_t t) {
  return t != nullptr ? static_cast<uint32_t>(uxTaskGetStackHighWaterMark(t)) : 0;  // tavuina
}

// Toteutunut ydin ja prioriteetti suoraan FreeRTOSilta, ei vakioista.
void logTask(const char* name, TaskHandle_t t) {
  if (t == nullptr) {
    Serial.printf(" %s=PUUTTUU", name);
    return;
  }
  const BaseType_t core = xTaskGetAffinity(t);
  Serial.printf(" %s=c%d/p%lu", name, core == tskNO_AFFINITY ? -1 : static_cast<int>(core),
                static_cast<unsigned long>(uxTaskPriorityGet(t)));
}

TaskHandle_t g_async_tcp = nullptr;  // AsyncTCP:n oma taski, haetaan nimellä

// Sarjalokin edelliset arvot (vain loopTask käyttää)
uint32_t g_prev_late = 0;
uint32_t g_prev_gaps = 0;
uint32_t g_prev_drops = 0;
uint32_t g_prev_skipped = 0;
uint32_t g_prev_sent = 0;

}  // namespace

size_t diagJson(char* buf, size_t n) {
  const uint32_t now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
  refreshSlow(now_ms);

  ProcWindow w;
  procWindow(w);
  SamplerCounters sc;
  samplerCounters(sc);
  WebStats ws;
  webStats(ws);
  const uint32_t heap_free = static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_8BIT));
  const uint32_t heap_min = static_cast<uint32_t>(heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
  const int R = kRailIndex;

  const int len = snprintf(
      buf, n,
      "{\"v\":1,\"t\":%lu,\"up\":%lu,\"src\":\"%s\","
      "\"win\":%lu,\"n\":%u,\"seq\":%lu,\"fs\":%.2f,\"dev_us\":%u,\"lag_us\":%u,"
      "\"late\":%lu,\"gaps\":%lu,\"q_drop\":%lu,\"skipped\":%lu,\"hx_bad\":%lu,"
      "\"q_peak\":%u,\"q_len\":%lu,\"read_us\":[%u,%u],"
      "\"heap\":[%lu,%lu,%lu],\"sta\":%lu,\"sse\":%lu,\"sse_tx\":[%lu,%lu,%lu],"
      "\"push_us\":%lu,"
      "\"mv\":[[%u,%u,%u],[%u,%u,%u],[%u,%u,%u]],\"rail\":[%u,%u,%u]}",
      ul(now_ms), ul(now_ms / 1000), sourceName(samplerSource()),
      ul(w.index), static_cast<unsigned>(w.n), ul(w.seq), static_cast<double>(w.fs_hz),
      static_cast<unsigned>(w.dev_max_us), static_cast<unsigned>(w.lag_max_us),
      ul(w.late), ul(w.gaps), ul(sc.queue_drops), ul(sc.skipped), ul(sc.hx_bad),
      static_cast<unsigned>(w.q_peak), ul(kQueueLen), static_cast<unsigned>(w.read_mean_us),
      static_cast<unsigned>(w.read_max_us),
      ul(heap_free), ul(heap_min), ul(g_heap_block.load(std::memory_order_relaxed)),
      ul(g_sta.load(std::memory_order_relaxed)), ul(ws.sse_clients), ul(ws.sse_sent),
      ul(ws.sse_partial), ul(ws.sse_dropped), ul(ws.push_us_last),
      static_cast<unsigned>(w.mv_min[0]), static_cast<unsigned>(w.mv_mean[0]),
      static_cast<unsigned>(w.mv_max[0]), static_cast<unsigned>(w.mv_min[1]),
      static_cast<unsigned>(w.mv_mean[1]), static_cast<unsigned>(w.mv_max[1]),
      static_cast<unsigned>(w.mv_min[2]), static_cast<unsigned>(w.mv_mean[2]),
      static_cast<unsigned>(w.mv_max[2]), static_cast<unsigned>(w.mv_min[R]),
      static_cast<unsigned>(w.mv_mean[R]), static_cast<unsigned>(w.mv_max[R]));
  return (len > 0 && static_cast<size_t>(len) < n) ? static_cast<size_t>(len) : 0;
}

void diagLogBoot() {
  g_async_tcp = xTaskGetHandle("async_tcp");  // luotu AsyncServer::begin()issä

  Serial.printf("[INFO] Suckboxx WP-7b: sampler + Processor + AP + web/SSE (docs/firmware.md 4.9)\n");
  Serial.printf("[INFO] Arduino %d.%d.%d / IDF %s, CPU %lu MHz, FreeRTOS %d Hz\n",
                ESP_ARDUINO_VERSION_MAJOR, ESP_ARDUINO_VERSION_MINOR, ESP_ARDUINO_VERSION_PATCH,
                esp_get_idf_version(), ul(getCpuFrequencyMhz()), configTICK_RATE_HZ);
  Serial.printf("[INFO] lahde=%s, jono %lu x %u t, ikkuna %lu ms, SSE %lu Hz, myohassa > %lu us\n",
                sourceName(samplerSource()), ul(kQueueLen), static_cast<unsigned>(sizeof(SampleMsg)),
                ul(kWindowMs), ul(kPushHz), ul(kLateUs));
  const AdcInfo& a = samplerAdcInfo();
  if (a.valid) {
    Serial.printf("[INFO] ADC: kalibrointi=%s vref=%lu mV a=%lu b=%lu, raw0->%lu mV raw4095->%lu mV"
                  " (koodataan 0 / 3300 mV)\n",
                  calTypeName(a.cal_type), ul(a.vref_mv), ul(a.coeff_a), ul(a.coeff_b),
                  ul(a.raw0_mv), ul(a.raw4095_mv));
  }
  char ip[16];
  wifiApIp(ip, sizeof(ip));
  Serial.printf("[INFO] AP SSID=%s kanava=%u max=%u IP=%s -> http://%s/\n", kApSsid,
                static_cast<unsigned>(kApChannel), static_cast<unsigned>(kApMaxClients), ip, ip);
  Serial.printf("[INFO] taskit (ydin/prio):");
  logTask("sbx_sample", samplerTask());
  logTask("sbx_proc", procTask());
  logTask("sbx_push", webPushTask());
  logTask("async_tcp", g_async_tcp);
  logTask("loopTask", xTaskGetCurrentTaskHandle());
  Serial.printf("\n");
}

void diagLogPeriodic() {
  ProcWindow w;
  procWindow(w);
  ProcPeriod p;
  procTakePeriod(p);
  SamplerCounters sc;
  samplerCounters(sc);
  WebStats ws;
  webStats(ws);
  const uint32_t push_max = webTakePushMax();
  const uint32_t up_s = static_cast<uint32_t>(esp_timer_get_time() / 1000000);

  Serial.printf(
      "[DIAG] up=%lu s %s fs=%.2f Hz poikk=%u us lag=%u us myoh=%lu(+%lu) aukot=%lu(+%lu)"
      " pudot=%lu(+%lu) ohit=%lu(+%lu) jono=%u/%lu luku=%u/%u us hx_bad=%lu\n",
      ul(up_s), sourceName(samplerSource()), static_cast<double>(p.fs_hz),
      static_cast<unsigned>(p.dev_max_us), static_cast<unsigned>(p.lag_max_us), ul(w.late),
      ul(w.late - g_prev_late), ul(w.gaps), ul(w.gaps - g_prev_gaps), ul(sc.queue_drops),
      ul(sc.queue_drops - g_prev_drops), ul(sc.skipped), ul(sc.skipped - g_prev_skipped),
      static_cast<unsigned>(p.q_peak), ul(kQueueLen), static_cast<unsigned>(p.read_mean_us),
      static_cast<unsigned>(p.read_max_us), ul(sc.hx_bad));
  Serial.printf(
      "[DIAG] heap=%lu min=%lu blk=%lu | ap=%lu sse=%lu tx=%lu(+%lu) osit=%lu hyl=%lu"
      " push=%lu/%lu us | mv %u/%u/%u kisko %u\n",
      ul(static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_8BIT))),
      ul(static_cast<uint32_t>(heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT))),
      ul(g_heap_block.load(std::memory_order_relaxed)), ul(wifiApStations()), ul(ws.sse_clients),
      ul(ws.sse_sent), ul(ws.sse_sent - g_prev_sent), ul(ws.sse_partial), ul(ws.sse_dropped),
      ul(ws.push_us_last), ul(push_max), static_cast<unsigned>(w.mv_mean[0]),
      static_cast<unsigned>(w.mv_mean[1]), static_cast<unsigned>(w.mv_mean[2]),
      static_cast<unsigned>(w.mv_mean[kRailIndex]));
  Serial.printf("[PINO] vapaa min: sample=%lu proc=%lu push=%lu async_tcp=%lu loop=%lu t\n",
                ul(stackFree(samplerTask())), ul(stackFree(procTask())), ul(stackFree(webPushTask())),
                ul(stackFree(g_async_tcp)), ul(stackFree(xTaskGetCurrentTaskHandle())));

  g_prev_late = w.late;
  g_prev_gaps = w.gaps;
  g_prev_drops = sc.queue_drops;
  g_prev_skipped = sc.skipped;
  g_prev_sent = ws.sse_sent;
}

}  // namespace hal
}  // namespace sbx
