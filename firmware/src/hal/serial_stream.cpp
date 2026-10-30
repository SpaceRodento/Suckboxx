// Suckboxx ESP32-liima - raakadatan sarjavirta. Ks. serial_stream.h.

#include "hal/serial_stream.h"

#include <Arduino.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "hal/proc_task.h"
#include "hal/sampler.h"

namespace sbx {
namespace hal {
namespace {

constexpr uint32_t kLogBaud = 115200;         // main.cpp:n Serial.begin
constexpr uint32_t kDefaultStreamBaud = 921600;
constexpr uint32_t kMaxStreamS = 3600;
constexpr size_t kRingLen = 1024;             // 1 s puskuria 1 kHz:llä

struct StreamSample {
  uint32_t seq;
  uint16_t mv[kChannels];
};

// SPSC: käsittelytaski kirjoittaa (head), loopTask lukee (tail). Keosta
// ensimmäisellä käytöllä (12 kt): staattinen DRAM on jo Processorin renkaan
// (64 kt) käytössä, eikä virtaa käytetä normaalisti lainkaan.
StreamSample* g_ring = nullptr;
std::atomic<uint32_t> g_head{0};
std::atomic<uint32_t> g_tail{0};
std::atomic<bool> g_active{false};
std::atomic<uint32_t> g_dropped{0};

// loopTaskin oma tila
uint32_t g_end_ms = 0;
uint32_t g_baud = kLogBaud;
uint32_t g_sent = 0;
uint32_t g_seq0 = 0;
bool g_have_seq0 = false;
char g_cmd[48];
size_t g_cmd_len = 0;

void printHeader() {
  procLock();
  const Config cfg = processor().config();
  const Calibration cal = processor().calibration();
  procUnlock();
  Serial.printf("# Suckboxx sarjavirta\n# device=suckboxx\n# format=1\n# source=%s\n# fs_hz=%lu\n",
                sourceName(cfg.source), static_cast<unsigned long>(kSampleRateHz));
  Serial.printf("# v_zero_mv=%.3f\n# mv_per_kpa=%.4f\n", static_cast<double>(cfg.sensor.v_zero_mv),
                static_cast<double>(cfg.sensor.mv_per_kpa));
  Serial.printf("# zero_valid=%d\n# gain_valid=%d\n", cal.zero_valid ? 1 : 0, cal.gain_valid ? 1 : 0);
  Serial.printf("# offset_kpa=%.4f,%.4f,%.4f\n", static_cast<double>(cal.offset_kpa[0]),
                static_cast<double>(cal.offset_kpa[1]), static_cast<double>(cal.offset_kpa[2]));
  Serial.printf("# gain=%.6f,%.6f,%.6f\n", static_cast<double>(cal.gain[0]),
                static_cast<double>(cal.gain[1]), static_cast<double>(cal.gain[2]));
  Serial.print("seq,ch1_mv,ch2_mv,ch3_mv\n");
}

void start(uint32_t seconds, uint32_t baud) {
  if (g_ring == nullptr) {
    g_ring = static_cast<StreamSample*>(malloc(sizeof(StreamSample) * kRingLen));
    if (g_ring == nullptr) {
      Serial.println("# stream virhe: muisti loppui");
      return;
    }
  }
  if (seconds == 0 || seconds > kMaxStreamS) seconds = 60;
  if (baud < kLogBaud || baud > 2000000) baud = kDefaultStreamBaud;
  Serial.printf("# stream ok %lu %lu\n", static_cast<unsigned long>(seconds),
                static_cast<unsigned long>(baud));
  Serial.flush();
  delay(30);  // PC vaihtaa nopeutensa kuittauksen jälkeen
  if (baud != kLogBaud) Serial.updateBaudRate(baud);
  g_baud = baud;
  printHeader();
  g_sent = 0;
  g_have_seq0 = false;
  g_dropped.store(0, std::memory_order_relaxed);
  g_tail.store(g_head.load(std::memory_order_acquire), std::memory_order_release);  // vanhat pois
  g_end_ms = millis() + seconds * 1000u;
  g_active.store(true, std::memory_order_release);
}

void stop() {
  g_active.store(false, std::memory_order_release);
  Serial.printf("# end samples=%lu dropped=%lu\n", static_cast<unsigned long>(g_sent),
                static_cast<unsigned long>(g_dropped.load(std::memory_order_relaxed)));
  Serial.flush();
  if (g_baud != kLogBaud) {
    delay(30);
    Serial.updateBaudRate(kLogBaud);
    g_baud = kLogBaud;
  }
}

void handleCommand(const char* line) {
  if (std::strncmp(line, "stream", 6) == 0) {
    char* end = nullptr;
    const unsigned long s = std::strtoul(line + 6, &end, 10);
    const unsigned long b = end != nullptr ? std::strtoul(end, nullptr, 10) : 0;
    if (!g_active.load(std::memory_order_acquire)) start(static_cast<uint32_t>(s), static_cast<uint32_t>(b));
  } else if (std::strcmp(line, "stop") == 0) {
    if (g_active.load(std::memory_order_acquire)) stop();
  }
}

void readCommands() {
  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c < 0) break;
    if (c == '\n' || c == '\r') {
      if (g_cmd_len > 0) {
        g_cmd[g_cmd_len] = '\0';
        handleCommand(g_cmd);
        g_cmd_len = 0;
      }
    } else if (g_cmd_len + 1 < sizeof(g_cmd)) {
      g_cmd[g_cmd_len++] = static_cast<char>(c);
    }
  }
}

}  // namespace

void streamPush(const RawFrame& f) {
  if (!g_active.load(std::memory_order_acquire)) return;
  const uint32_t h = g_head.load(std::memory_order_relaxed);
  if (h - g_tail.load(std::memory_order_acquire) >= kRingLen) {
    g_dropped.fetch_add(1, std::memory_order_relaxed);  // näkyy seq-aukkona
    return;
  }
  StreamSample& s = g_ring[h % kRingLen];
  s.seq = f.seq;
  for (int c = 0; c < kChannels; ++c) s.mv[c] = f.mv[c];
  g_head.store(h + 1, std::memory_order_release);
}

bool streamActive() { return g_active.load(std::memory_order_acquire); }

void streamPoll() {
  readCommands();
  if (!g_active.load(std::memory_order_acquire)) return;
  // Tulostus erissä: yksi printf-puskuri per ~20 riviä, jotta UART-ajurin
  // kutsuja on vähän. Rivi on enintään ~24 merkkiä.
  char buf[512];
  for (;;) {
    size_t len = 0;
    uint32_t t = g_tail.load(std::memory_order_relaxed);
    const uint32_t h = g_head.load(std::memory_order_acquire);
    while (t != h && len + 32 < sizeof(buf)) {
      const StreamSample& s = g_ring[t % kRingLen];
      if (!g_have_seq0) {
        g_seq0 = s.seq;
        g_have_seq0 = true;
      }
      len += static_cast<size_t>(std::snprintf(buf + len, sizeof(buf) - len, "%lu,%u,%u,%u\n",
                                               static_cast<unsigned long>(s.seq - g_seq0), s.mv[0],
                                               s.mv[1], s.mv[2]));
      ++t;
      ++g_sent;
    }
    g_tail.store(t, std::memory_order_release);
    if (len == 0) break;
    Serial.write(reinterpret_cast<const uint8_t*>(buf), len);
  }
  if (static_cast<int32_t>(millis() - g_end_ms) >= 0) stop();
}

}  // namespace hal
}  // namespace sbx
