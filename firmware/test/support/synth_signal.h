#pragma once
// Testien synteettinen imusarjasignaali (WP-2:n DSP-testit).
//
// Tarkoituksella erillinen EngineSimistä (WP-1): tämä on pieni analyyttinen
// malli, jonka jokaisen termin integraali tunnetaan tarkasti. Siksi DSP:n
// keskiarvovirhe voidaan mitata absoluuttista totuutta vasten eikä vain
// "näyttää järkevältä".
//
// Malli kanavalle c (ajat sekunteina; double, koska totuuden pitää olla
// tarkempi kuin testattavan float-DSP:n):
//
//   p_c(t) = base + err_c + K
//            - sum_m g_mc * ( depth * w(t; sylinterin m imu) + R_c / P_m )
//            + r_c(t) + kohina
//
//   - imut vuorottelevat sylintereille 240 kampiasteen välein, imu kestää
//     intake_frac * P (P = saman sylinterin seuraavaan imuun),
//     w = puolisini 0..1..0
//   - g_mc = 1 omalle sylinterille, xtalk muille (tasauskanavat, D6)
//   - K = depth * (2/pi) * intake_frac * (1 + 2 xtalk) kumoaa pulssien
//     keskiarvon -> jokaisen P-jakson keskiarvo on täsmälleen base + err_c
//   - r_c = jokaisen imun herättämä vaimeneva letkuresonanssi
//     g * res_kpa * exp(-t/decay) * sin(2 pi f_c t); sen integraali R_c
//     kompensoidaan samalla jaksolla (-R_c/P), joten pitkän ajan keskiarvo on
//     täsmälleen base + err_c myös resonanssin kanssa.
//   - jitter: jokainen 240 asteen segmentti (imu -> seuraavan sylinterin imu)
//     on oma satunnaismuuttujansa (palotapahtuma muuttaa kampiakselin
//     nopeutta). Sylinterin jakso = 3 peräkkäistä segmenttiä, joten jokaisella
//     kanavalla on sama jaksohajonta; segmentin hajonta = jitter * sqrt(3),
//     jolloin 720 asteen jakson suhteellinen hajonta = jitter. N rajattu +-3 sigmaan.

#include <cmath>
#include <complex>
#include <cstdint>

#include "core/types.h"

namespace sbxtest {

struct SynthParams {
  bool engine_on = true;
  float rpm = 850.0f;
  float jitter = 0.0f;              // jakson suhteellinen keskihajonta
  float base_kpa = -55.0f;
  float sync_err_kpa[sbx::kChannels] = {0.0f, 0.0f, 0.0f};
  float depth_kpa = 20.0f;
  float intake_frac = 0.25f;
  float res_hz[sbx::kChannels] = {90.0f, 135.0f, 180.0f};  // R1-rikkomus tahallaan
  float res_kpa = 3.0f;
  float res_decay_s = 0.02f;
  float xtalk = 0.0f;
  float noise_kpa = 0.2f;
  float atm_kpa = 0.0f;             // moottori seis / letku irti
  bool hose_off[sbx::kChannels] = {false, false, false};
  float start_phase = 0.0f;         // moottorisyklin vaihe hetkellä 0 (0..1)
  uint32_t seed = 1;
  // mV-muunnos RawFrameen (XGZP 3,3 V oletukset, docs/rauta.md §1.2)
  float v_zero_mv = 2700.0f;
  float mv_per_kpa = 25.0f;
};

class SynthSignal {
 public:
  explicit SynthSignal(const SynthParams& p = SynthParams()) { reset(p); }

  void reset(const SynthParams& p) {
    p_ = p;
    n_ = 0;
    rng_ = p.seed ? p.seed : 1u;
    spare_valid_ = false;
    for (int c = 0; c < sbx::kChannels; ++c) z_[c] = std::complex<double>(0.0, 0.0);
    anchor(0.0, p.start_phase);
  }

  // Muuttaa parametrit lennossa (aika ja syklit jatkuvat). Moottorin
  // käynnistys (engine_on false -> true) aloittaa syklit tästä hetkestä.
  void setParams(const SynthParams& p) {
    const bool start = p.engine_on && !p_.engine_on;
    p_ = p;
    updateResonance();
    if (start) {
      for (int c = 0; c < sbx::kChannels; ++c) z_[c] = std::complex<double>(0.0, 0.0);
      anchor(time(), p.start_phase);
    }
  }
  const SynthParams& params() const { return p_; }

  void next(float (&kpa)[sbx::kChannels], sbx::RawFrame& raw) {
    const double t = time();
    double v[sbx::kChannels];
    for (int c = 0; c < sbx::kChannels; ++c) v[c] = static_cast<double>(p_.atm_kpa);

    if (p_.engine_on) {
      const double d = static_cast<double>(p_.depth_kpa);
      const double frac = static_cast<double>(p_.intake_frac);
      const double x = static_cast<double>(p_.xtalk);
      const double kconst = d * (2.0 / kPi) * frac * (1.0 + 2.0 * x);

      for (int c = 0; c < sbx::kChannels; ++c) z_[c] *= rot_[c];  // resonanssit eteenpäin

      double dip[sbx::kChannels];
      double inv_span[sbx::kChannels];
      for (int m = 0; m < sbx::kChannels; ++m) {
        // Uudet imutapahtumat, jotka alkoivat edellisen näytteen jälkeen.
        while (true) {
          ensure(cur_[m] + 6);
          const double tau = ev_[ring(cur_[m] + 3)];
          if (tau > t) break;
          cur_[m] += 3;
          if (m == 0) ++cycles_started_;
          for (int c = 0; c < sbx::kChannels; ++c) {
            const double g = (m == c) ? 1.0 : x;
            z_[c] += g * static_cast<double>(p_.res_kpa) * std::exp(lambda_[c] * (t - tau));
          }
        }
        const double tau = ev_[ring(cur_[m])];
        const double span = ev_[ring(cur_[m] + 3)] - tau;
        const double u = (t - tau) / (frac * span);
        dip[m] = (u >= 0.0 && u < 1.0) ? std::sin(kPi * u) : 0.0;
        inv_span[m] = 1.0 / span;
      }

      for (int c = 0; c < sbx::kChannels; ++c) {
        if (p_.hose_off[c]) continue;
        double s = static_cast<double>(p_.base_kpa) + static_cast<double>(p_.sync_err_kpa[c]) + kconst;
        for (int m = 0; m < sbx::kChannels; ++m) {
          const double g = (m == c) ? 1.0 : x;
          s -= g * (d * dip[m] + res_int_[c] * inv_span[m]);
        }
        s += z_[c].imag();
        v[c] = s;
      }
    }

    for (int c = 0; c < sbx::kChannels; ++c) {
      v[c] += static_cast<double>(p_.noise_kpa) * gauss();
      kpa[c] = static_cast<float>(v[c]);
      double mv = static_cast<double>(p_.v_zero_mv) + static_cast<double>(p_.mv_per_kpa) * v[c];
      if (mv < 0.0) mv = 0.0;
      if (mv > 3300.0) mv = 3300.0;
      raw.mv[c] = static_cast<uint16_t>(mv + 0.5);
    }
    raw.seq = static_cast<uint32_t>(n_);
    raw.rail_mv = 3300;
    ++n_;
  }

  // Totuus: pitkän ajan keskiarvo (täsmälleen, ks. malli yllä).
  float trueMean(int ch) const {
    if (!p_.engine_on || p_.hose_off[ch]) return p_.atm_kpa;
    return p_.base_kpa + p_.sync_err_kpa[ch];
  }
  float trueRpm() const { return p_.rpm; }
  double time() const { return static_cast<double>(n_) / static_cast<double>(sbx::kSampleRateHz); }
  uint64_t sampleIndex() const { return n_; }
  // Sylinterin 1 alkaneet imut (= moottorisyklit) generaattorin alusta.
  uint32_t cyclesStarted() const { return cycles_started_; }
 private:
  static constexpr double kPi = 3.14159265358979323846;
  static constexpr int kRing = 32;

  static int ring(int j) { return ((j % kRing) + kRing) % kRing; }

  // Tapahtumat j = 0, 1, 2, ... vuorotellen sylintereille j mod 3.
  void ensure(int j) {
    while (last_j_ < j) {
      ev_[ring(last_j_ + 1)] = ev_[ring(last_j_)] + nextSegment();
      ++last_j_;
    }
  }

  double nextSegment() {
    double g = gauss();
    if (g > 3.0) g = 3.0;
    if (g < -3.0) g = -3.0;
    return t0() / 3.0 * (1.0 + static_cast<double>(p_.jitter) * 1.7320508075688772 * g);
  }

  double t0() const { return 120.0 / static_cast<double>(p_.rpm); }

  // Tapahtumat alkavat hetkestä t_now vaiheessa phase. Ensimmäiset kolme
  // tapahtumaa asetetaan nimellisvälein menneisyyteen, jotta jokaisella
  // sylinterillä on "kuluva" imu jo hetkellä t_now.
  void anchor(double t_now, float phase) {
    const double T = t0();
    ev_[0] = t_now - static_cast<double>(phase) * T - T;
    ev_[1] = ev_[0] + T / 3.0;
    ev_[2] = ev_[1] + T / 3.0;
    last_j_ = 2;
    for (int m = 0; m < sbx::kChannels; ++m) cur_[m] = m;
    ensure(8);
    updateResonance();
  }

  void updateResonance() {
    const double dt = 1.0 / static_cast<double>(sbx::kSampleRateHz);
    for (int c = 0; c < sbx::kChannels; ++c) {
      const double alpha = 1.0 / static_cast<double>(p_.res_decay_s);
      const double w = 2.0 * kPi * static_cast<double>(p_.res_hz[c]);
      lambda_[c] = std::complex<double>(-alpha, w);
      rot_[c] = std::exp(lambda_[c] * dt);
      // integral 0..inf A e^{-at} sin(wt) dt = A w / (a^2 + w^2)
      res_int_[c] = static_cast<double>(p_.res_kpa) * w / (alpha * alpha + w * w);
    }
  }

  uint32_t xorshift() {
    uint32_t x = rng_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_ = x;
    return x;
  }

  double uniform() { return (static_cast<double>(xorshift()) + 1.0) / 4294967297.0; }

  double gauss() {  // Box-Muller
    if (spare_valid_) {
      spare_valid_ = false;
      return spare_;
    }
    const double u1 = uniform();
    const double u2 = uniform();
    const double r = std::sqrt(-2.0 * std::log(u1));
    spare_ = r * std::sin(2.0 * kPi * u2);
    spare_valid_ = true;
    return r * std::cos(2.0 * kPi * u2);
  }

  SynthParams p_;
  uint64_t n_ = 0;
  uint32_t rng_ = 1;
  bool spare_valid_ = false;
  double spare_ = 0.0;
  double ev_[kRing] = {};
  int last_j_ = 0;
  int cur_[sbx::kChannels] = {0, 1, 2};  // kunkin sylinterin kuluva tapahtuma
  uint32_t cycles_started_ = 0;
  std::complex<double> z_[sbx::kChannels];
  std::complex<double> rot_[sbx::kChannels];
  std::complex<double> lambda_[sbx::kChannels];
  double res_int_[sbx::kChannels] = {0.0, 0.0, 0.0};
};

}  // namespace sbxtest
