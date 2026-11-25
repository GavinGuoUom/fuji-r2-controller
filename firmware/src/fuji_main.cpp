#include <Arduino.h>
#include <avr/wdt.h>
#include <util/atomic.h>
#include "fuji_board.h"
#include "fuji_control.h"
#ifdef FUJI_PCB_R2
#include "fuji_calibration_console.h"
#include "fuji_tuning_console.h"
#endif

#if !defined(__AVR_ATmega2560__)
#error "FJS requires Mega2560"
#endif
using namespace fuji;
uint8_t resetCause __attribute__((section(".noinit")));
void earlyWatchdogOff() __attribute__((naked, section(".init3"), used));
void earlyWatchdogOff() { resetCause = MCUSR; MCUSR = 0; wdt_disable(); }

namespace {
#ifdef FUJI_PCB_R2
Controller control(BoardRevision::R2);
CalibrationConsole calibration;
TuningConsole tuning;
#else
Controller control;
#endif
Params params[3];
constexpr uint8_t IRQ_BITS[6] = {4,5,3,2,1,0}; // ATmega2560 INT numbers, not Arduino interrupt indexes.
volatile uint8_t captured = 0;
bool irqArmed[6] = {};
bool lowTracking[6] = {};
uint32_t lowAt[6] = {};
uint16_t filtered[ADC_COUNT] = {}, currentRaw[3] = {};
uint16_t initialized = 0;
uint8_t adcChannel = 0;
uint32_t adcAt = 0, logAt = 0;
bool previousBias[3] = {}, biasOn[3] = {};
uint32_t biasAt[3] = {};
uint16_t lastDuty[3] = {65535,65535,65535};

void capture(uint8_t n) {
  captured |= uint8_t(1<<n);
  EIMSK &= uint8_t(~(1<<IRQ_BITS[n])); // Bound ISR load even if the contact chatters.
}
void irq0() { capture(0); } void irq1() { capture(1); }
void irq2() { capture(2); } void irq3() { capture(3); }
void irq4() { capture(4); } void irq5() { capture(5); }
void (*const ISR_HANDLERS[6])() = {irq0,irq1,irq2,irq3,irq4,irq5};

void pwm(uint8_t route, uint16_t duty) {
  if (duty == lastDuty[route]) return;
  const uint8_t shift = 6 - 2*route; // COM5A1:0, B1:0, C1:0.
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    if (!duty || duty >= FULL) {
      TCCR5A &= uint8_t(~(3<<shift));
      digitalWrite(PINS[route].pwm, duty ? HIGH : LOW);
    } else {
      // Fast PWM duty=(OCR+1)/(TOP+1). Register writes are 16-bit atomic.
      if (route == 0) OCR5A = duty-1;
      else if (route == 1) OCR5B = duty-1;
      else OCR5C = duty-1;
      TCCR5A = (TCCR5A & uint8_t(~(3<<shift))) | (2<<shift);
    }
  }
  lastDuty[route] = duty;
}
void cancelEvents(uint8_t i) {
  const uint8_t mask = uint8_t(3<<(2*i));
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    EIMSK &= uint8_t(~((1<<IRQ_BITS[2*i]) | (1<<IRQ_BITS[2*i+1])));
    captured &= uint8_t(~mask);
  }
  for (uint8_t n=2*i; n<2*i+2; ++n) { irqArmed[n]=false; lowTracking[n]=false; }
}
uint8_t sensors(uint32_t now) {
  uint8_t events;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { events=captured; captured=0; }
  for (uint8_t n=0; n<6; ++n) {
    const uint8_t i=n/2, pin=(n&1) ? PINS[i].right : PINS[i].left;
    if (!control.permit || !biasOn[i] || uint32_t(now-biasAt[i]) < BLANK_MS) {
      events &= uint8_t(~(1<<n)); continue;
    }
    if (events & (1<<n)) { irqArmed[n]=false; lowTracking[n]=false; }
    if (irqArmed[n]) continue;
    if (digitalRead(pin)) lowTracking[n]=false;
    else if (!lowTracking[n]) { lowTracking[n]=true; lowAt[n]=now; }
    else if (uint32_t(now-lowAt[n]) >= 20) {
      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { EIFR = 1<<IRQ_BITS[n]; EIMSK |= 1<<IRQ_BITS[n]; }
      irqArmed[n]=true;
    }
  }
  return events;
}
uint16_t potValue(uint8_t pin) {
  const uint8_t channel=pin-A0;
  const uint16_t adc=filtered[channel]/16;
#ifdef FUJI_PCB_R2
  return tuning.active.rotaryValue(channel,calibration.store.active[channel].scale(adc));
#else
  return normalized(adc);
#endif
}
void sampleAdc(uint32_t now) {
  if (uint32_t(now-adcAt) < 1) return;
  adcAt=now;
  const uint8_t pin=A0+adcChannel;
  analogRead(pin); // Discard after mux switch, including A8..A12 (MUX5).
  const uint16_t raw=analogRead(pin);
#ifdef FUJI_PCB_R2
  calibration.sample(adcChannel,raw);
#endif
  if (!(initialized & (1<<adcChannel))) filtered[adcChannel]=raw*16;
  else filtered[adcChannel] = filtered[adcChannel] - filtered[adcChannel]/16 + raw;
  initialized |= 1<<adcChannel;
  if (adcChannel >= 9 && adcChannel <= 11) currentRaw[adcChannel-9]=raw;
  adcChannel=(adcChannel+1)%ADC_COUNT;
  for (uint8_t i=0;i<3;++i) {
    const auto &p=PINS[i];
#ifdef FUJI_PCB_R2
    applyR2Rotaries(params[i],potValue(p.speed),potValue(p.ramp),potValue(p.minimum),tuning.active);
#else
    params[i].speed=potValue(p.speed);
    params[i].rampMs=3000-uint32_t(potValue(p.ramp))*2800/FULL;
    params[i].minimum=potValue(p.minimum);
#endif
  }
}
void outputs(uint32_t now) {
  // Disable the global gate before touching any per-route state on OFF/fault.
  if (!control.permit) digitalWrite(READY,LOW);
#ifdef FUJI_PCB_R2
  static bool busSinking = false;
  if (busSinking != control.faultRequest()) {
    busSinking = control.faultRequest();
    pinMode(BUS_SINK,busSinking ? OUTPUT : INPUT); // D36 only sinks the shared bus.
  }
#else
  if (control.fault != Fault::None) digitalWrite(FAULT_SET,HIGH);
#endif
  for (uint8_t i=0;i<3;++i) {
    const auto &p=PINS[i]; const auto &r=control.routes[i];
    const bool direction=r.right == DIR_RIGHT_HIGH[i];
    if (!control.permit) {
      pwm(i,0); digitalWrite(p.v1,LOW); digitalWrite(p.v2,LOW);
      biasOn[i]=false; cancelEvents(i);
    } else {
#ifdef FUJI_PCB_R2
      // Keep EN at the lighting duty while switching PH on the DRV8876.
      // Sensor rearming is independent of the track PWM.
      pwm(i,r.duty);
#endif
      if (!biasOn[i] || direction != previousBias[i]) {
#ifdef FUJI_PCB_R1
        pwm(i,0);
#endif
        cancelEvents(i);
        digitalWrite(p.dir,direction);
        const bool v1=direction == BIAS_V1_FOR_DIR_HIGH[i];
        digitalWrite(p.v1,v1); digitalWrite(p.v2,!v1);
        biasOn[i]=true; previousBias[i]=direction; biasAt[i]=now;
      }
#ifdef FUJI_PCB_R1
      pwm(i,uint32_t(now-biasAt[i]) >= BLANK_MS ? r.duty : 0);
#endif
    }
    // The panel reads this as the maintained automatic-mode bit.
    digitalWrite(p.green, control.permit && r.automatic());
  }
  if (control.permit) digitalWrite(READY,HIGH);
}
#ifdef FUJI_PCB_R2
uint16_t sensorHits[6] = {}, buttonHits[3] = {};

// One short line per loop. A stalled USB connection must not delay rail service.
void diagnostics(uint32_t now) {
  constexpr uint8_t END = 13;
  static uint8_t line = END;
  static bool watch = false, help = false;
  if (tuning.service() || calibration.service()) return;
  if (Serial.available()) {
    const char c=Serial.read();
    if (tuning.receive(c,digitalRead(TOTAL))) return;
    if (calibration.receive(c,now,digitalRead(TOTAL),control.ready,filtered)) {
      watch=false; line=END; return;
    }
    switch (c) {
      case '?': help = true; break;
      case 'w': watch = true; // Also request the first report immediately.
        /* fall through */
      case 's': if (line == END) line = 0; break;
      case 'q': watch = false; line = END; break;
    }
  }
  if (Serial.availableForWrite() < 63) return;
  if (help) {
    Serial.println(F("FJS R2: s=status w=watch q=quiet :cal<Enter> :status<Enter>"));
    help = false;
    return;
  }
  if (watch && line == END && uint32_t(now-logAt) >= 1000) line = 0;
  if (line == END) return;
  char out[64];
  int size;
  if (line == 0) {
    logAt = now;
    size = snprintf_P(out,sizeof(out),
      PSTR("R2 totalN=%d porN=%d busN=%d sink=%d lat=%d en=%d F=%u mask=%u\n"),
      digitalRead(TOTAL),digitalRead(POR_N),digitalRead(BUS_N),
      int(control.faultRequest()),digitalRead(FAULT),digitalRead(READY),
      unsigned(control.fault),unsigned(control.faultRoutes));
  } else {
    const uint8_t i=(line-1)/4;
    const auto &p=PINS[i]; const auto &r=control.routes[i];
    const unsigned route=i+1;
    switch ((line-1)%4) {
      case 0:
        size = snprintf_P(out,sizeof(out),
          PSTR("R%u state=%u dir=%c pwm=%u goal=%u min=%u time=%ums\n"),
          route,unsigned(r.state),r.right?'R':'L',lastDuty[i],target(params[i]),
          params[i].minimum,params[i].rampMs);
        break;
      case 1:
        size = snprintf_P(out,sizeof(out),
          PSTR("R%u ADC speed=%u time=%u min=%u Iraw=%u dwell=%ums\n"),route,
          filtered[p.speed-A0]/16,filtered[p.ramp-A0]/16,
          filtered[p.minimum-A0]/16,currentRaw[i],params[i].dwellMs);
        break;
      case 2:
        size = snprintf_P(out,sizeof(out),PSTR("R%u brake=%ums pots=%u,%u,%u\n"),route,params[i].brakeMs,
                         potValue(p.speed),potValue(p.ramp),potValue(p.minimum));
        break;
      default:
        size = snprintf_P(out,sizeof(out),
          PSTR("R%u keyN=%d L=%d R=%d hits(key,L,R)=%u,%u,%u\n"),route,
          digitalRead(p.start),digitalRead(p.left),digitalRead(p.right),
          buttonHits[i],sensorHits[2*i],sensorHits[2*i+1]);
    }
  }
  if (size>0 && size<int(sizeof(out))) Serial.write((const uint8_t*)out,size);
  ++line;
}
#else
void diagnostics(uint32_t now) {
  // Bounded optional telemetry. No commands can start a route or clear a fault.
  if (uint32_t(now-logAt)<500 || Serial.availableForWrite()<60) return;
  logAt=now;
  static uint8_t i=0;
  Serial.print(F("FJS R"));Serial.print(i+1);
  Serial.print(F(" S="));Serial.print(uint8_t(control.routes[i].state));
  Serial.print(F(" PWM="));Serial.print(control.routes[i].duty);
  Serial.print(F(" Iraw="));Serial.print(currentRaw[i]);
  Serial.print(F(" F="));Serial.print(uint8_t(control.fault));
  Serial.print(F(" P="));Serial.println(control.permit);
  i=(i+1)%3;
}
#endif
}

void setup() {
  digitalWrite(READY,LOW);pinMode(READY,OUTPUT);
#ifdef FUJI_PCB_R2
  pinMode(BUS_SINK,INPUT);digitalWrite(BUS_SINK,LOW);
  pinMode(BUS_N,INPUT);pinMode(POR_N,INPUT);
#else
  digitalWrite(FAULT_SET,LOW);pinMode(FAULT_SET,OUTPUT);
  pinMode(POWER,INPUT);
  for (uint8_t pin : DRIVER_N) pinMode(pin,INPUT);
#endif
  pinMode(TOTAL,INPUT);pinMode(FAULT,INPUT);
  for (const auto &p:PINS) {
    const uint8_t outputs[] = {p.pwm,p.dir,p.v1,p.v2,p.green};
    for (uint8_t pin : outputs) { digitalWrite(pin,LOW);pinMode(pin,OUTPUT); }
    pinMode(p.start,INPUT);
    pinMode(p.left,INPUT);pinMode(p.right,INPUT);
  }
  TCCR5A=0;TCCR5B=0;TCNT5=0;ICR5=511;OCR5A=OCR5B=OCR5C=0;
  TCCR5A=_BV(WGM51);TCCR5B=_BV(WGM53)|_BV(WGM52)|_BV(CS50);
  for (uint8_t n=0;n<6;++n) {
    attachInterrupt(digitalPinToInterrupt((n&1)?PINS[n/2].right:PINS[n/2].left),ISR_HANDLERS[n],RISING);
    EIMSK &= uint8_t(~(1<<IRQ_BITS[n]));
  }
  analogReference(DEFAULT);
#ifdef FUJI_PCB_R2
  calibration.store.load(EEPROM);
  tuning.load();
#endif
  Serial.begin(115200);
  control.begin(millis(),resetCause & _BV(WDRF));
  wdt_enable(WDTO_250MS);
}
void loop() {
  const uint32_t now=millis();
  uint8_t faults=0,presses=0;
  for (uint8_t i=0;i<3;++i) {
#ifdef FUJI_PCB_R1
    if (!digitalRead(DRIVER_N[i])) faults|=1<<i;
#endif
    if (!digitalRead(PINS[i].start)) presses|=1<<i;
  }
  const uint8_t events=sensors(now);
  bool totalOn=!digitalRead(TOTAL);
  const bool latched=digitalRead(FAULT);
  const bool adcReady=initialized==((1u<<ADC_COUNT)-1);
#ifdef FUJI_PCB_R2
  const bool powerGood=digitalRead(POR_N), commonFault=!digitalRead(BUS_N);
#ifdef FUJI_PANEL_RESET
  control.servicePanelReset(now,!totalOn,powerGood && !commonFault && !latched && adcReady);
#endif
  if (calibration.blockOutput(now,!totalOn,powerGood && !commonFault && !latched && control.fault==Fault::None))
    totalOn=false;
#else
  const bool powerGood=digitalRead(POWER), commonFault=false;
#endif
  control.update(now,powerGood,latched,faults,totalOn,
                 adcReady,presses,events,params,commonFault);
  outputs(now); // Safety/output service precedes slow ADC and serial work.
  sampleAdc(now);
#ifdef FUJI_PCB_R2
  for (uint8_t n=0;n<6;++n) {
    if (events & (1u<<n)) ++sensorHits[n];
  }
  for (uint8_t i=0;i<3;++i) {
    if (control.buttonEvents & (1u<<i)) ++buttonHits[i];
  }
#endif
  diagnostics(now);
  wdt_reset();
}
