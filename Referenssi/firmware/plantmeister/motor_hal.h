/*=====================================================================
  motor_hal.h - Motor Hardware Abstraction Layer

  Supported drivers (MOTOR_DRIVER in config.h):
    MOTOR_DRIVER_L298N             — proto: HALF4WIRE AccelStepper
    MOTOR_DRIVER_TMC2208           — step/dir AccelStepper
                                     (myös MKS SERVO42C-MT CR_vFOC-tilassa)
    MOTOR_DRIVER_SERVO             — hobby servo via PWM (single pin)
    MOTOR_DRIVER_MKS_SERVO42C_UART — MKS SERVO42C-MT closed-loop UART
    MOTOR_DRIVER_TMC2209           — step/dir AccelStepper (TMC2208:n seuraaja,
                                     max 2A, korkeampi nopeus)

  Public API (all drivers):
    motor_init(), motor_moveTo(), motor_moveBy(),
    motor_update(), motor_isMoving(), motor_getPositionMm()

  Call motor_update() from loop() — never blocks.
=====================================================================*/

#ifndef MOTOR_HAL_H
#define MOTOR_HAL_H

#include "config.h"
#include "structs.h"
#include "calibration_math.h"

#if ENABLE_MOTOR

#if MOTOR_DRIVER == MOTOR_DRIVER_MKS_SERVO42C_UART
  #include "motor_hal_mks_uart.h"
#endif

// TMC-perhe käyttää AccelStepper::DRIVER-tilaa identtisellä rajapinnalla.
// Tämän makron avulla samat haarat hoitavat sekä TMC2208:n että TMC2209:n.
#if (MOTOR_DRIVER == MOTOR_DRIVER_TMC2208) || (MOTOR_DRIVER == MOTOR_DRIVER_TMC2209)
  #define MOTOR_DRIVER_IS_TMC 1
#else
  #define MOTOR_DRIVER_IS_TMC 0
#endif

#if MOTOR_DRIVER == MOTOR_DRIVER_SERVO
  // ── Continuous rotation servo ────────────────────────────────
  // Position tracked by dead reckoning (speed × time).
  // Calibrate MOTOR_SERVO_MM_PER_SEC to match physical speed.
  #include <ESP32Servo.h>
  static Servo         g_servo;
  static int           g_servoCurrentMm  = 0;
  static int           g_servoTargetMm   = 0;
  static bool          g_servoMoving     = false;
  static unsigned long g_servoStartMs    = 0;
  static unsigned long g_servoDurationMs = 0;
  static int8_t        g_servoDirection  = 0;   // +1 up, -1 down

#elif MOTOR_DRIVER == MOTOR_DRIVER_MKS_SERVO42C_UART
  // ── MKS SERVO42C-MT UART (closed-loop) ──────────────────────
  // Ei AccelStepperia — protokolla tarjoaa oman pulssikomennon (0xFD).
  // Liike komennot menevät motor_hal_mks_uart.h:n läpi.

#else
  // ── AccelStepper for stepper drivers ────────────────────────
  #include <AccelStepper.h>

  #if MOTOR_DRIVER == MOTOR_DRIVER_L298N
    // HALF4WIRE: 8-step sequence, better torque and less resonance
    // PIN order swapped when MOTOR_DIRECTION_INVERT=true
    // AccelStepper HALF4WIRE args: (pin1, pin3, pin2, pin4) — coil pair order
    // Invert direction by reversing both coil pairs: swap (1,3,2,4) → (4,3,2,1) equivalent
    #if MOTOR_DIRECTION_INVERT
    static AccelStepper g_stepper(AccelStepper::HALF4WIRE,
      PIN_MOTOR_IN4, PIN_MOTOR_IN3, PIN_MOTOR_IN2, PIN_MOTOR_IN1);
    #else
    static AccelStepper g_stepper(AccelStepper::HALF4WIRE,
      PIN_MOTOR_IN2, PIN_MOTOR_IN1, PIN_MOTOR_IN4, PIN_MOTOR_IN3);
    #endif
  #elif MOTOR_DRIVER == MOTOR_DRIVER_TMC2208
    static AccelStepper g_stepper(AccelStepper::DRIVER, PIN_MOTOR_STEP, PIN_MOTOR_DIR);
  #elif MOTOR_DRIVER == MOTOR_DRIVER_TMC2209
    static AccelStepper g_stepper(AccelStepper::DRIVER, PIN_MOTOR_STEP, PIN_MOTOR_DIR);
  #endif
#endif

static bool g_motorReady = false;

#if MOTOR_DRIVER != MOTOR_DRIVER_SERVO
static long g_motorPositionSteps = 0;
static long g_motorTargetSteps = 0;
static int32_t g_motorLimitStepsDown = 0;
static int32_t g_motorLimitStepsUp = 0;

#if MOTOR_DRIVER == MOTOR_DRIVER_L298N
static bool          g_l298nOutputsEnabled = true;
static bool          g_l298nWakePulseActive = false;
static unsigned long g_l298nWakePulseStartMs = 0;
static long          g_l298nWakeTargetSteps = 0;
static float         g_l298nSpeedSetting = MOTOR_L298N_MAX_SPEED;
static float         g_l298nAccelSetting = MOTOR_L298N_ACCELERATION;
static float         g_l298nDecelSetting = MOTOR_L298N_DECELERATION;
static float         g_l298nFinalDecelSetting = MOTOR_L298N_FINAL_DECELERATION;
static float         g_l298nFinalSpeedSetting = MOTOR_L298N_FINAL_MAX_SPEED;
static float         g_l298nActiveProfileSpeed = MOTOR_L298N_MAX_SPEED;
static float         g_l298nActiveProfileAccel = MOTOR_L298N_ACCELERATION;
#endif

#if MOTOR_DRIVER == MOTOR_DRIVER_MKS_SERVO42C_UART
// 42C-MT F6/FD-komennon speed-kenttä (0..127, epälineaarinen enum).
// Käytetään runtime-muutettavana — MOTOR_SPEED-komento päivittää tämän.
static uint8_t g_mksuRuntimeSpeed = MKS_UART_DEFAULT_SPEED;
#endif

// ── Conversion helpers (stepper only) ───────────────────────────

static inline long motor_mmToSteps(int mm) {
#if MOTOR_DRIVER == MOTOR_DRIVER_TMC2208
  return (long)mm * MOTOR_STEPS_PER_REV * MOTOR_TMC_MICROSTEPS / MOTOR_LEAD_MM;
#elif MOTOR_DRIVER == MOTOR_DRIVER_TMC2209
  return (long)mm * MOTOR_STEPS_PER_REV * MOTOR_TMC2209_MICROSTEPS / MOTOR_LEAD_MM;
#elif MOTOR_DRIVER == MOTOR_DRIVER_MKS_SERVO42C_UART
  return (long)mm * MKSU_STEPS_PER_REV / MOTOR_LEAD_MM;
#else
  // L298N HALF4WIRE: 2 half-steps per full step
  return (long)mm * MOTOR_STEPS_PER_REV * 2 / MOTOR_LEAD_MM;
#endif
}

static inline int motor_stepsToMm(long steps) {
#if MOTOR_DRIVER == MOTOR_DRIVER_TMC2208
  return (int)(steps * MOTOR_LEAD_MM / (MOTOR_STEPS_PER_REV * MOTOR_TMC_MICROSTEPS));
#elif MOTOR_DRIVER == MOTOR_DRIVER_TMC2209
  return (int)(steps * MOTOR_LEAD_MM / (MOTOR_STEPS_PER_REV * MOTOR_TMC2209_MICROSTEPS));
#elif MOTOR_DRIVER == MOTOR_DRIVER_MKS_SERVO42C_UART
  return (int)(steps * MOTOR_LEAD_MM / MKSU_STEPS_PER_REV);
#else
  return (int)(steps * MOTOR_LEAD_MM / (MOTOR_STEPS_PER_REV * 2));
#endif
}
#endif // !MOTOR_DRIVER_SERVO

// ── Public API ──────────────────────────────────────────────────

// TURVA: aja moottorin EN heti disabled-tilaan. Kutsutaan setup():n
// ENSIMMAISENA rivina — EI motor_init():ssa, joka ajetaan vasta
// sensors_init():n jalkeen eli useita sekunteja bootista.
//
// MIKSI: PIN_MOTOR_ENABLE on active-LOW (HIGH = disabled). Ennen
// pinMode()-kutsua ESP32:n GPIO on korkeaimpedanssinen TULO, jonka ajuri
// lukee LOW:na = KAYTOSSA. Ja 42C-MT on closed-loop (CR_vFOC): "kaytossa"
// ei tarkoita paikallaan pysymista vaan aktiivista asemanhakua. Siksi
// moottori ei surissut vaan LIIKKUI.
//
// Rautahavainto 17.7.2026: moottori liikkui useita sekunteja JOKA bootissa
// (koko delay(500) + platform + power + plants + sensors -ikkunan ajan).
// Huomattiin vain koska kelkka oli irti — kuormassa se olisi ajanut paatya
// kohti ilman etta kukaan kaski.
//
// RAJOITE: tama ei poista ikkunaa kokonaan. Virrankytkennan ja ensimmaisen
// kayskyn valissa (ROM-bootloader, ~100 ms) pinni kelluu yha, eika MCU voi
// tehda sille mitaan. Lopullinen korjaus on RAUTA-PULLUP EN-pinnissa, joka
// pitaa ajurin disabloituna myos silloin kun MCU on resetissa tai kuollut.
// Kirjattu: docs/laitteisto/. GPIO3 on lisaksi strapping-pin, joten
// pullupin arvo pitaa valita niin ettei se sotke boot-strappia.
inline void motor_safeDisable() {
#if ENABLE_MOTOR && defined(PIN_MOTOR_ENABLE) && MOTOR_DRIVER != MOTOR_DRIVER_SERVO && MOTOR_DRIVER != MOTOR_DRIVER_L298N
  pinMode(PIN_MOTOR_ENABLE, OUTPUT);
  digitalWrite(PIN_MOTOR_ENABLE, HIGH);  // HIGH = disabled (active-LOW EN)
#endif
}

bool motor_init() {
#if MOTOR_DRIVER == MOTOR_DRIVER_SERVO
  g_servo.attach(PIN_MOTOR_SERVO);
  g_servo.write(servo_mmToAngle(0));
  g_servoCurrentMm = 0;
  g_servoTargetMm  = 0;
  DEBUG_INFO(F("Motor: Servo initialized (PWM)"));

#elif MOTOR_DRIVER == MOTOR_DRIVER_L298N
  g_l298nSpeedSetting = MOTOR_L298N_MAX_SPEED;
  g_l298nAccelSetting = MOTOR_L298N_ACCELERATION;
  g_l298nDecelSetting = MOTOR_L298N_DECELERATION;
  g_l298nFinalDecelSetting = MOTOR_L298N_FINAL_DECELERATION;
  g_l298nFinalSpeedSetting = MOTOR_L298N_FINAL_MAX_SPEED;
  g_l298nActiveProfileSpeed = g_l298nSpeedSetting;
  g_l298nActiveProfileAccel = g_l298nAccelSetting;
  g_stepper.setMaxSpeed(g_l298nActiveProfileSpeed);
  g_stepper.setAcceleration(g_l298nActiveProfileAccel);
  g_stepper.setCurrentPosition(0);
  g_l298nOutputsEnabled = true;
  g_l298nWakePulseActive = false;
  DEBUG_INFO(F("Motor: L298N initialized (AccelStepper HALF4WIRE)"));

#elif MOTOR_DRIVER == MOTOR_DRIVER_TMC2208
  pinMode(PIN_MOTOR_ENABLE, OUTPUT);
  digitalWrite(PIN_MOTOR_ENABLE, HIGH);
  #if MOTOR_DIRECTION_INVERT
    g_stepper.setPinsInverted(true, false, false);
  #endif
  g_stepper.setMaxSpeed(MOTOR_TMC_MAX_SPEED);
  g_stepper.setAcceleration(MOTOR_TMC_ACCELERATION);
  g_stepper.setCurrentPosition(0);
  DEBUG_INFO(F("Motor: TMC2208 initialized (step/dir, microstepping)"));

#elif MOTOR_DRIVER == MOTOR_DRIVER_TMC2209
  pinMode(PIN_MOTOR_ENABLE, OUTPUT);
  digitalWrite(PIN_MOTOR_ENABLE, HIGH);
  #if MOTOR_DIRECTION_INVERT
    g_stepper.setPinsInverted(true, false, false);
  #endif
  g_stepper.setMaxSpeed(MOTOR_TMC2209_MAX_SPEED);
  g_stepper.setAcceleration(MOTOR_TMC2209_ACCELERATION);
  g_stepper.setCurrentPosition(0);
  DEBUG_INFO(F("Motor: TMC2209 initialized (step/dir, microstepping)"));

#elif MOTOR_DRIVER == MOTOR_DRIVER_MKS_SERVO42C_UART
  if (!mksu_init()) {
    DEBUG_ERROR(F("Motor: MKS 42C-MT UART init failed"));
    g_motorReady = false;
    return false;
  }
  DEBUG_INFO(F("Motor: MKS SERVO42C-MT UART initialized (closed-loop, CR_UART)"));
#endif

#if MOTOR_DRIVER != MOTOR_DRIVER_SERVO
  g_motorPositionSteps = 0;
  g_motorTargetSteps = 0;
  g_motorLimitStepsDown = calibration_defaultMotorDownSteps();
  g_motorLimitStepsUp = calibration_defaultMotorUpSteps();
#endif
  g_motorReady = true;
  return true;
}

void motor_enable() {
#if MOTOR_DRIVER == MOTOR_DRIVER_L298N
  if (!g_l298nOutputsEnabled) {
    g_stepper.enableOutputs();
    g_l298nOutputsEnabled = true;
    DEBUG_VERBOSE(F("Motor: L298N outputs enabled"));
  }
#elif MOTOR_DRIVER_IS_TMC
  digitalWrite(PIN_MOTOR_ENABLE, LOW);
  DEBUG_VERBOSE(F("Motor: driver enabled"));
#elif MOTOR_DRIVER == MOTOR_DRIVER_MKS_SERVO42C_UART
  mksu_enable();
  DEBUG_VERBOSE(F("Motor: MKS 42C-MT enabled (F3 0x01)"));
#endif
}

void motor_disable() {
#if MOTOR_DRIVER == MOTOR_DRIVER_SERVO
  g_servo.write(MOTOR_SERVO_STOP_PWM);
  g_servoMoving = false;
  DEBUG_VERBOSE(F("Motor: servo stopped"));
#elif MOTOR_DRIVER == MOTOR_DRIVER_L298N
  g_stepper.disableOutputs();
  g_l298nOutputsEnabled = false;
  g_l298nWakePulseActive = false;
  DEBUG_VERBOSE(F("Motor: L298N coils off"));
#elif MOTOR_DRIVER_IS_TMC
  digitalWrite(PIN_MOTOR_ENABLE, HIGH);
  DEBUG_VERBOSE(F("Motor: TMC driver disabled"));
#elif MOTOR_DRIVER == MOTOR_DRIVER_MKS_SERVO42C_UART
  mksu_disable();
  DEBUG_VERBOSE(F("Motor: MKS 42C-MT disabled (F3 0x00)"));
#endif
}

// Maintenance lock — mirrors DEVICE_MAINTENANCE (see pump_setMaintenanceLock()
// for the full rationale). A lamp carriage moving while the operator has hands
// in the enclosure is the motor half of the same hazard. Derived from state
// every tick by the caller, never latched on a transition.
static bool g_motorMaintenanceLock = false;

void motor_setMaintenanceLock(bool locked) {
  if (locked != g_motorMaintenanceLock) {
    DEBUG_PRINTF("[INFO]  Motor: huoltolukko %s\n", locked ? "päälle" : "pois");
  }
  g_motorMaintenanceLock = locked;
  // A move in flight is left to finish rather than cut mid-travel: motor_stop()
  // on a stepper mid-move loses the position reference (DEVICE_FAULT_MOTOR),
  // and the carriage is slow and quiet enough that finishing is the lesser
  // hazard. New moves are refused immediately, which is what matters.
}

bool motor_isMaintenanceLocked() { return g_motorMaintenanceLock; }

// Move lamp to absolute height in mm (non-blocking)
void motor_moveTo(int mm) {
  if (!g_motorReady) return;
  if (g_motorMaintenanceLock) {
    DEBUG_WARN(F("Motor: refused — huoltotila aktiivinen"));
    return;
  }
  if (mm < 0) mm = 0;
  if (mm > MOTOR_MAX_HEIGHT_MM) mm = MOTOR_MAX_HEIGHT_MM;

#if MOTOR_DRIVER == MOTOR_DRIVER_SERVO
  int delta = mm - g_servoCurrentMm;
  if (delta == 0) return;
  g_servoTargetMm   = mm;
  g_servoDirection  = (delta > 0) ? 1 : -1;
  g_servoDurationMs = (unsigned long)(abs(delta) / MOTOR_SERVO_MM_PER_SEC * 1000.0f);
  g_servoStartMs    = millis();
  g_servoMoving     = true;
  int pwm = MOTOR_SERVO_STOP_PWM + g_servoDirection * MOTOR_SERVO_SPEED_PWM;
  g_servo.write(pwm);
  DEBUG_PRINTF("[INFO]  Motor: servo → %d mm, %lums, pwm=%d\n",
               mm, g_servoDurationMs, pwm);

#else
  g_motorTargetSteps = motor_mmToSteps(mm);
  if (g_motorLimitStepsUp <= g_motorLimitStepsDown) {
    g_motorLimitStepsDown = calibration_defaultMotorDownSteps();
    g_motorLimitStepsUp = calibration_defaultMotorUpSteps();
  }
  if (g_motorTargetSteps < g_motorLimitStepsDown) g_motorTargetSteps = g_motorLimitStepsDown;
  if (g_motorTargetSteps > g_motorLimitStepsUp) g_motorTargetSteps = g_motorLimitStepsUp;

  #if MOTOR_DRIVER == MOTOR_DRIVER_MKS_SERVO42C_UART
    // 42C-MT ei tarjoa absoluuttista positiointia natiivisti — laske delta
    // softassa ja lähetä suhteellinen pulssikomento.
    long delta = g_motorTargetSteps - g_motorPositionSteps;
    if (delta == 0) return;
    int32_t pulses = (int32_t)delta;
    #if MOTOR_DIRECTION_INVERT
      pulses = -pulses;
    #endif
    mksu_runPulses(pulses, g_mksuRuntimeSpeed);
    g_motorPositionSteps = g_motorTargetSteps;  // optimistic — varmistus poll():lla
    DEBUG_PRINTF("[INFO]  Motor: MKS UART → %d mm (delta %ld steps, speed=%u)\n",
                 mm, delta, (unsigned)g_mksuRuntimeSpeed);
  #else
    if (g_motorTargetSteps == g_stepper.currentPosition() && g_stepper.distanceToGo() == 0) {
      return;
    }

    #if MOTOR_DRIVER == MOTOR_DRIVER_L298N
      if (g_l298nWakePulseActive) {
        // If a new command arrives during wake pulse, just update pending target.
        g_l298nWakeTargetSteps = g_motorTargetSteps;
        DEBUG_VERBOSE(F("Motor: wake pulse target updated"));
        return;
      }

      bool needsWakePulse = !g_l298nOutputsEnabled && (MOTOR_L298N_WAKE_PULSE_MS > 0);
      motor_enable();
      if (needsWakePulse) {
        g_l298nWakePulseActive = true;
        g_l298nWakePulseStartMs = millis();
        g_l298nWakeTargetSteps = g_motorTargetSteps;
        DEBUG_PRINTF("[VERB]  Motor: wake pulse %lums before move\n", MOTOR_L298N_WAKE_PULSE_MS);
        return;
      }
    #else
      // Ensure driver outputs are active after idle/disable before any new move.
      motor_enable();
    #endif

    g_stepper.moveTo(g_motorTargetSteps);
    DEBUG_PRINTF("[INFO]  Motor: moving to %d mm (%ld steps)\n", mm, g_motorTargetSteps);
  #endif
#endif
}

// Set motor speed at runtime (half-steps/sec for L298N, steps/sec for TMC)
// Use for noise/speed testing without reflashing.
void motor_setSpeed(float speed) {
  if (speed <= 0) return;
#if MOTOR_DRIVER == MOTOR_DRIVER_L298N
  g_l298nSpeedSetting = speed;
  g_l298nActiveProfileSpeed = speed;
  g_stepper.setMaxSpeed(g_l298nActiveProfileSpeed);
  DEBUG_PRINTF("[INFO]  Motor: speed set to %.0f half-steps/s\n", speed);
#elif MOTOR_DRIVER_IS_TMC
  g_stepper.setMaxSpeed(speed);
  DEBUG_PRINTF("[INFO]  Motor: speed set to %.0f steps/s\n", speed);
#elif MOTOR_DRIVER == MOTOR_DRIVER_MKS_SERVO42C_UART
  // 42C-MT F6/FD-komennon speed on 0..127 enum (epälineaarinen, ei steps/sec).
  // Asetus tulee voimaan SEURAAVAAN motor_moveTo()-kutsuun (kesken liikkeen
  // ei voi muuttaa, koska F6/FD lähettää koko pulssi+nopeus-paketin kerralla).
  int rounded = (int)(speed + 0.5f);
  if (rounded < 1) rounded = 1;
  if (rounded > 0x7F) rounded = 0x7F;
  g_mksuRuntimeSpeed = (uint8_t)rounded;
  DEBUG_PRINTF("[INFO]  Motor: MKS speed set to %u (0..127 enum)\n",
               (unsigned)g_mksuRuntimeSpeed);
#endif
}

// Set motor acceleration at runtime (half-steps/sec^2 for L298N, steps/sec^2 for TMC)
void motor_setAcceleration(float acceleration) {
  if (acceleration <= 0) return;
#if MOTOR_DRIVER == MOTOR_DRIVER_L298N
  g_l298nAccelSetting = acceleration;
  g_l298nActiveProfileAccel = acceleration;
  g_stepper.setAcceleration(g_l298nActiveProfileAccel);
  DEBUG_PRINTF("[INFO]  Motor: acceleration set to %.0f half-steps/s^2\n", acceleration);
#elif MOTOR_DRIVER_IS_TMC
  g_stepper.setAcceleration(acceleration);
  DEBUG_PRINTF("[INFO]  Motor: acceleration set to %.0f steps/s^2\n", acceleration);
#elif MOTOR_DRIVER == MOTOR_DRIVER_MKS_SERVO42C_UART
  // 42C-MT:n ACC asetetaan 0xA4-komennolla erikseen — ei runtime-säätöä tästä.
  (void)acceleration;
#endif
}

void motor_setDeceleration(float deceleration) {
  if (deceleration <= 0) return;
#if MOTOR_DRIVER == MOTOR_DRIVER_L298N
  g_l298nDecelSetting = deceleration;
  DEBUG_PRINTF("[INFO]  Motor: deceleration set to %.0f half-steps/s^2\n", deceleration);
#elif MOTOR_DRIVER_IS_TMC
  g_stepper.setAcceleration(deceleration);
  DEBUG_PRINTF("[INFO]  Motor: deceleration set to %.0f steps/s^2\n", deceleration);
#elif MOTOR_DRIVER == MOTOR_DRIVER_MKS_SERVO42C_UART
  (void)deceleration;
#endif
}

void motor_setFinalDeceleration(float deceleration) {
  if (deceleration <= 0) return;
#if MOTOR_DRIVER == MOTOR_DRIVER_L298N
  g_l298nFinalDecelSetting = deceleration;
  DEBUG_PRINTF("[INFO]  Motor: final deceleration set to %.0f half-steps/s^2\n", deceleration);
#elif MOTOR_DRIVER_IS_TMC
  g_stepper.setAcceleration(deceleration);
  DEBUG_PRINTF("[INFO]  Motor: final deceleration set to %.0f steps/s^2\n", deceleration);
#elif MOTOR_DRIVER == MOTOR_DRIVER_MKS_SERVO42C_UART
  (void)deceleration;
#endif
}

void motor_setFinalSpeed(float speed) {
  if (speed <= 0) return;
#if MOTOR_DRIVER == MOTOR_DRIVER_L298N
  g_l298nFinalSpeedSetting = speed;
  DEBUG_PRINTF("[INFO]  Motor: final speed set to %.0f half-steps/s\n", speed);
#elif MOTOR_DRIVER_IS_TMC
  g_stepper.setMaxSpeed(speed);
  DEBUG_PRINTF("[INFO]  Motor: final speed set to %.0f steps/s\n", speed);
#elif MOTOR_DRIVER == MOTOR_DRIVER_MKS_SERVO42C_UART
  (void)speed;
#endif
}

float motor_getSpeed() {
#if MOTOR_DRIVER == MOTOR_DRIVER_SERVO
  return 0.0f;
#elif MOTOR_DRIVER == MOTOR_DRIVER_L298N
  return g_l298nSpeedSetting;
#elif MOTOR_DRIVER == MOTOR_DRIVER_MKS_SERVO42C_UART
  return (float)g_mksuRuntimeSpeed;
#else
  return g_stepper.maxSpeed();
#endif
}

float motor_getAcceleration() {
#if MOTOR_DRIVER == MOTOR_DRIVER_SERVO
  return 0.0f;
#elif MOTOR_DRIVER == MOTOR_DRIVER_L298N
  return g_l298nAccelSetting;
#elif MOTOR_DRIVER == MOTOR_DRIVER_MKS_SERVO42C_UART
  return 0.0f;
#else
  return g_stepper.acceleration();
#endif
}

float motor_getDeceleration() {
#if MOTOR_DRIVER == MOTOR_DRIVER_SERVO
  return 0.0f;
#elif MOTOR_DRIVER == MOTOR_DRIVER_L298N
  return g_l298nDecelSetting;
#elif MOTOR_DRIVER == MOTOR_DRIVER_MKS_SERVO42C_UART
  return 0.0f;
#else
  return g_stepper.acceleration();
#endif
}

float motor_getFinalDeceleration() {
#if MOTOR_DRIVER == MOTOR_DRIVER_SERVO
  return 0.0f;
#elif MOTOR_DRIVER == MOTOR_DRIVER_L298N
  return g_l298nFinalDecelSetting;
#elif MOTOR_DRIVER == MOTOR_DRIVER_MKS_SERVO42C_UART
  return 0.0f;
#else
  return g_stepper.acceleration();
#endif
}

float motor_getFinalSpeed() {
#if MOTOR_DRIVER == MOTOR_DRIVER_SERVO
  return 0.0f;
#elif MOTOR_DRIVER == MOTOR_DRIVER_L298N
  return g_l298nFinalSpeedSetting;
#elif MOTOR_DRIVER == MOTOR_DRIVER_MKS_SERVO42C_UART
  return (float)g_mksuRuntimeSpeed;
#else
  return g_stepper.maxSpeed();
#endif
}

// Move relative to current position
void motor_moveBy(int deltaMm) {
#if MOTOR_DRIVER == MOTOR_DRIVER_SERVO
  motor_moveTo(g_servoCurrentMm + deltaMm);
#else
  motor_moveTo(motor_stepsToMm(g_motorTargetSteps) + deltaMm);
#endif
}

// Call from loop() — non-blocking, returns true if still moving
bool motor_update() {
#if MOTOR_DRIVER == MOTOR_DRIVER_SERVO
  if (!g_servoMoving) return false;
  if ((millis() - g_servoStartMs) >= g_servoDurationMs) {
    g_servo.write(MOTOR_SERVO_STOP_PWM);
    g_servoCurrentMm = g_servoTargetMm;
    g_servoMoving    = false;
    DEBUG_INFO(F("Motor: servo stopped"));
  }
  return g_servoMoving;
#elif MOTOR_DRIVER == MOTOR_DRIVER_MKS_SERVO42C_UART
  // Non-blocking UART poll: kerää uplink-tavut + päivittää g_mksuMoving
  // kun status=02 (run complete) saapuu.
  mksu_poll();
  return mksu_isMoving();
#else
#if MOTOR_DRIVER == MOTOR_DRIVER_L298N
  if (g_l298nWakePulseActive) {
    if ((millis() - g_l298nWakePulseStartMs) < MOTOR_L298N_WAKE_PULSE_MS) {
      return true;
    }
    g_l298nWakePulseActive = false;
    g_stepper.moveTo(g_l298nWakeTargetSteps);
    DEBUG_VERBOSE(F("Motor: wake pulse complete"));
  }

  long dist = g_stepper.distanceToGo();
  if (dist < 0) dist = -dist;
  float profileAccel = g_l298nAccelSetting;
  float profileSpeed = g_l298nSpeedSetting;

  if (dist <= MOTOR_L298N_FINAL_DECEL_WINDOW_STEPS) {
    profileAccel = g_l298nFinalDecelSetting;
    profileSpeed = g_l298nFinalSpeedSetting;
  } else if (dist <= MOTOR_L298N_DECEL_WINDOW_STEPS) {
    profileAccel = g_l298nDecelSetting;
  }

  if (profileSpeed > g_l298nSpeedSetting) {
    profileSpeed = g_l298nSpeedSetting;
  }

  if (profileAccel != g_l298nActiveProfileAccel) {
    g_l298nActiveProfileAccel = profileAccel;
    g_stepper.setAcceleration(g_l298nActiveProfileAccel);
  }
  if (profileSpeed != g_l298nActiveProfileSpeed) {
    g_l298nActiveProfileSpeed = profileSpeed;
    g_stepper.setMaxSpeed(g_l298nActiveProfileSpeed);
  }
#endif

  if (g_stepper.distanceToGo() != 0) {
    g_stepper.run();
    g_motorPositionSteps = g_stepper.currentPosition();
    return true;
  }
  return false;
#endif
}

bool motor_isMoving() {
#if MOTOR_DRIVER == MOTOR_DRIVER_SERVO
  return g_servoMoving;
#elif MOTOR_DRIVER == MOTOR_DRIVER_MKS_SERVO42C_UART
  return mksu_isMoving();
#else
  #if MOTOR_DRIVER == MOTOR_DRIVER_L298N
    if (g_l298nWakePulseActive) return true;
  #endif
  return g_stepper.distanceToGo() != 0;
#endif
}

int motor_getPositionMm() {
#if MOTOR_DRIVER == MOTOR_DRIVER_SERVO
  return g_servoCurrentMm;
#else
  return motor_stepsToMm(g_motorPositionSteps);
#endif
}

int motor_getTargetMm() {
#if MOTOR_DRIVER == MOTOR_DRIVER_SERVO
  return g_servoTargetMm;
#else
  return motor_stepsToMm(g_motorTargetSteps);
#endif
}

long motor_getPositionSteps() {
#if MOTOR_DRIVER == MOTOR_DRIVER_SERVO
  return g_servoCurrentMm;
#else
  return g_motorPositionSteps;
#endif
}

long motor_getTargetSteps() {
#if MOTOR_DRIVER == MOTOR_DRIVER_SERVO
  return g_servoTargetMm;
#else
  return g_motorTargetSteps;
#endif
}

// Set current position as reference (after manual adjustment)
void motor_setPositionMm(int mm) {
#if MOTOR_DRIVER == MOTOR_DRIVER_SERVO
  g_servoCurrentMm = mm;
  g_servoTargetMm  = mm;
  g_servoMoving    = false;
  g_servo.write(MOTOR_SERVO_STOP_PWM);
#elif MOTOR_DRIVER == MOTOR_DRIVER_MKS_SERVO42C_UART
  g_motorPositionSteps = motor_mmToSteps(mm);
  g_motorTargetSteps = g_motorPositionSteps;
  // 42C-MT:ssä softan asema-vakio päivittyy; encoder-zero vaatii erillisen
  // 0x91/0x94-komentosekvenssin, jota tässä ei vielä toteuteta.
#else
  g_motorPositionSteps = motor_mmToSteps(mm);
  g_motorTargetSteps = g_motorPositionSteps;
  g_stepper.setCurrentPosition(g_motorPositionSteps);
#endif
  DEBUG_PRINTF("[INFO]  Motor: position set to %d mm\n", mm);
}

void motor_setStepLimits(int32_t downSteps, int32_t upSteps) {
#if MOTOR_DRIVER == MOTOR_DRIVER_SERVO
  (void)downSteps;
  (void)upSteps;
#else
  g_motorLimitStepsDown = downSteps;
  g_motorLimitStepsUp = upSteps;
  if (g_motorLimitStepsUp <= g_motorLimitStepsDown) {
    g_motorLimitStepsDown = calibration_defaultMotorDownSteps();
    g_motorLimitStepsUp = calibration_defaultMotorUpSteps();
  }
  g_motorPositionSteps = calibration_clampMotorStepTarget(g_motorPositionSteps, NULL);
  g_motorTargetSteps = calibration_clampMotorStepTarget(g_motorTargetSteps, NULL);
  #if MOTOR_DRIVER != MOTOR_DRIVER_MKS_SERVO42C_UART
  g_stepper.setCurrentPosition(g_motorPositionSteps);
  #endif
#endif
}

int32_t motor_getStepLimitUp() {
#if MOTOR_DRIVER == MOTOR_DRIVER_SERVO
  return MOTOR_MAX_HEIGHT_MM;
#else
  return g_motorLimitStepsUp;
#endif
}

int32_t motor_getStepLimitDown() {
#if MOTOR_DRIVER == MOTOR_DRIVER_SERVO
  return 0;
#else
  return g_motorLimitStepsDown;
#endif
}

bool motor_isReady() { return g_motorReady; }

#endif // ENABLE_MOTOR
#endif // MOTOR_HAL_H
