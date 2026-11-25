#pragma once
#include <EEPROM.h>
#include <avr/eeprom.h>
#include "fuji_calibration.h"

namespace fuji {
class CalibrationConsole {
  PotRange draft_[POT_COUNT];
  enum Mode { Idle, Editing, Sampling, Saving } mode_=Idle;
  uint8_t step_=0, samples_=0, length_=0;
  uint16_t low_=0, high_=0, sum_=0;
  uint32_t touched_=0;
  bool parsing_=false, overflow_=false, awaitOff_=false;
  char command_[16]={}, reply_[64]={};

  const char *name(uint8_t index) const {
    const char *const names[]={"SPEED","TIME","LIGHT"};
    return names[index%3];
  }
  void prompt(const char *prefix) {
    if (step_==2*POT_COUNT)
      snprintf_P(reply_,sizeof(reply_),PSTR("%s ALL 18 DONE; :save or :abort"),prefix);
    else
      snprintf_P(reply_,sizeof(reply_),PSTR("%s STEP %u TRACK %u %s %s"),
        prefix,unsigned(step_+1),unsigned(step_/6+1),name(step_/2),step_%2?"MAX":"MIN");
  }
  static int number(const char *s) {
    if (!*s) return -1;
    int n=0;
    while (*s) {
      if (*s<'0' || *s>'9' || n>99) return -1;
      n=n*10+(*s++-'0');
    }
    return n;
  }
  void command(uint32_t now,bool off,bool ready,const uint16_t *filtered) {
    touched_=now;
    if (overflow_) { strcpy_P(reply_,PSTR("ERR command too long")); return; }
    if (!strcmp(command_,"status")) {
      if (mode_==Idle) snprintf_P(reply_,sizeof(reply_),PSTR("CAL IDLE %s"),store.saved()?"EEPROM":"DEFAULT");
      else if (mode_==Saving) strcpy_P(reply_,PSTR("CAL SAVING"));
      else prompt(mode_==Sampling?"SAMPLING":"CAL");
    } else if (!strncmp(command_,"pot ",4)) {
      int n=number(command_+4);
      if (n<1 || n>POT_COUNT) { strcpy_P(reply_,PSTR("ERR :pot 1..9")); return; }
      const auto &r=store.active[n-1];
      uint16_t adc=filtered[n-1]/16;
      snprintf_P(reply_,sizeof(reply_),PSTR("P%u %s lo=%u hi=%u raw=%u mapped=%u/512"),
        unsigned(n),store.saved()?"EEPROM":"DEFAULT",r.low,r.high,adc,r.scale(adc));
    } else if (!strcmp(command_,"cal")) {
      if (!off || !ready || awaitOff_) { strcpy_P(reply_,PSTR("ERR need master OFF and healthy ready board")); return; }
      if (mode_==Idle) { mode_=Editing; step_=0; }
      if (mode_==Saving) strcpy_P(reply_,PSTR("CAL SAVING"));
      else prompt("CAL");
    } else if (!strcmp(command_,"back")) {
      if (mode_!=Editing || !step_) { strcpy_P(reply_,PSTR("ERR no completed step to undo")); return; }
      --step_; prompt("CAL");
    } else if (!strcmp(command_,"abort")) {
      if (mode_==Saving) { strcpy_P(reply_,PSTR("CAL SAVING")); return; }
      mode_=Idle; strcpy_P(reply_,PSTR("CAL ABORT; stored endpoints retained"));
    } else if (!strncmp(command_,"take ",5)) {
      if (mode_!=Editing || step_>=2*POT_COUNT || number(command_+5)!=step_+1) {
        strcpy_P(reply_,PSTR("ERR unexpected step; :status")); return;
      }
      mode_=Sampling; samples_=0; low_=1023; high_=sum_=0;
      prompt("SAMPLING");
    } else if (!strcmp(command_,"save")) {
      if (mode_!=Editing || step_!=2*POT_COUNT) { strcpy_P(reply_,PSTR("ERR need all 18 endpoints")); return; }
      store.prepare(draft_); mode_=Saving; strcpy_P(reply_,PSTR("CAL SAVING"));
    } else strcpy_P(reply_,PSTR("ERR use :cal :status :take N :pot N :save :abort"));
  }
public:
  PotStore store;
  bool blockOutput(uint32_t now,bool off,bool healthy) {
    if (off) awaitOff_=false;
    if (mode_!=Idle && !off) awaitOff_=true;
    if ((mode_==Editing || mode_==Sampling) &&
        (!off || !healthy || uint32_t(now-touched_)>=300000UL)) {
      mode_=Idle;
      strcpy_P(reply_,PSTR("CAL ABORT: master ON, fault or timeout"));
    }
    return mode_!=Idle || awaitOff_;
  }
  void sample(uint8_t channel,uint16_t raw) {
    if (mode_!=Sampling || channel!=step_/2) return;
    if (raw<low_) low_=raw;
    if (raw>high_) high_=raw;
    sum_+=raw;
    if (++samples_<16) return;
    mode_=Editing;
    if (high_-low_>12) { prompt("ERR unstable; retry"); return; }
    const uint16_t value=(sum_+8)/16;
    auto &range=draft_[channel];
    if (!(step_%2)) range.low=value;
    else {
      range.high=value;
      if (!range.valid()) { prompt("ERR span <128; retry"); return; }
    }
    ++step_;
    char result[23];
    snprintf_P(result,sizeof(result),PSTR("OK %u raw=%u"),unsigned(step_),value);
    prompt(result);
  }
  bool receive(char c,uint32_t now,bool off,bool ready,const uint16_t *filtered) {
    if (c==':') {
      parsing_=true; overflow_=false; length_=0;
    } else if (!parsing_) return false;
    else if (c=='\r' || c=='\n') {
      parsing_=false; command_[length_]='\0'; command(now,off,ready,filtered);
    } else if (length_+1u<sizeof(command_)) command_[length_++]=c;
    else overflow_=true;
    return true;
  }
  bool service() {
    if (mode_==Saving && eeprom_is_ready()) {
      auto result=store.writeNext(EEPROM);
      if (result!=PotStore::Busy) {
        mode_=Idle;
        strcpy_P(reply_,result==PotStore::Saved ? PSTR("OK SAVED 9 EEPROM verified") : PSTR("ERR EEPROM verify"));
      }
    }
    if (!reply_[0]) return false;
    if (Serial.availableForWrite()>=63) { Serial.println(reply_); reply_[0]=0; }
    return true;
  }
};
}
