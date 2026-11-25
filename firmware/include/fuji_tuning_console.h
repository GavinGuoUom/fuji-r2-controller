#pragma once
#include <EEPROM.h>
#include <avr/eeprom.h>
#include <string.h>
#include "fuji_control.h"

namespace fuji {
class TuningConsole {
  // Keep the calibration and earlier settings intact for upgrades and rollback.
  static constexpr uint8_t V1_FIRST=82, V1_SIZE=15, V2_FIRST=112, V2_SIZE=19;
  static constexpr uint8_t FIRST=150, SIZE=21, MARK=0xC6, VERSION=3;
  uint8_t data_[SIZE]={}, generation_=0, position_=0, length_=0;
  int8_t slot_=-1;
  bool reading_=false, overflow_=false, saving_=false;
  char command_[48]={}, reply_[63]={};

  static uint16_t word(const uint8_t *bytes) { return bytes[0] | (uint16_t(bytes[1])<<8); }
  static void put(uint8_t *bytes,uint16_t value) { bytes[0]=uint8_t(value); bytes[1]=uint8_t(value>>8); }
  static uint16_t crc(const uint8_t *bytes, uint8_t size) {
    uint16_t value=0xffff;
    for (uint8_t i=1;i<size-2;++i) {
      value^=uint16_t(bytes[i])<<8;
      for (uint8_t bit=0;bit<8;++bit) value = value&0x8000 ? (value<<1)^0x1021 : value<<1;
    }
    return value;
  }
  void status() {
    snprintf_P(reply_,sizeof(reply_),PSTR("@CFG 3 %u %u %u %u %u %u %u %u"),active.lightPermille,
               active.dwellMin,active.dwellMax,active.rampMin,active.rampMax,
               active.runoutMm,active.brakeMaxMs,active.reverseRotaries);
  }
  bool parseSet(const char *cursor,Tuning &tune) {
    uint16_t *const values[]={&tune.lightPermille,&tune.dwellMin,&tune.dwellMax,&tune.rampMin,
                              &tune.rampMax,&tune.runoutMm,&tune.brakeMaxMs,&tune.reverseRotaries};
    for (auto field:values) {
      while (*cursor==' ') ++cursor;
      if (*cursor<'0' || *cursor>'9') return false;
      uint32_t value=0;
      while (*cursor>='0' && *cursor<='9') {
        value=value*10+(*cursor++-'0');
        if (value>60000) return false;
      }
      if (*cursor && *cursor!=' ') return false;
      *field=uint16_t(value);
    }
    while (*cursor==' ') ++cursor;
    return !*cursor && tune.valid();
  }
  void dispatch(bool off) {
    if (overflow_) strcpy_P(reply_,PSTR("@ERR length"));
    else if (!strcmp(command_,"get")) status();
    else if (saving_) strcpy_P(reply_,PSTR("@ERR busy"));
    else if (!strncmp(command_,"set ",4)) {
      Tuning draft;
      if (!parseSet(command_+4,draft)) strcpy_P(reply_,PSTR("@ERR range"));
      else if (!off && draft.reverseRotaries!=active.reverseRotaries) strcpy_P(reply_,PSTR("@ERR master-on"));
      else { active=draft; status(); }
    } else if (!strcmp(command_,"save")) {
      if (!off) { strcpy_P(reply_,PSTR("@ERR master-on")); return; }
      data_[0]=MARK; data_[1]=VERSION; data_[2]=generation_+1;
      put(data_+3,active.lightPermille); put(data_+5,active.dwellMin); put(data_+7,active.dwellMax);
      put(data_+9,active.rampMin); put(data_+11,active.rampMax);
      put(data_+13,active.runoutMm); put(data_+15,active.brakeMaxMs);
      put(data_+17,active.reverseRotaries); put(data_+19,crc(data_,SIZE));
      position_=0; saving_=true;
    } else strcpy_P(reply_,PSTR("@ERR command"));
  }
  void loadRecords(uint8_t first, uint8_t size, uint8_t version) {
    slot_=-1;
    generation_=0;
    for (uint8_t slot=0;slot<2;++slot) {
      for (uint8_t i=0;i<size;++i) data_[i]=EEPROM.read(first+slot*size+i);
      if (data_[0]!=MARK || data_[1]!=version || word(data_+size-2)!=crc(data_,size)) continue;
      Tuning tune;
      tune.lightPermille=word(data_+3); tune.dwellMin=word(data_+5); tune.dwellMax=word(data_+7);
      tune.rampMin=word(data_+9); tune.rampMax=word(data_+11);
      if (version>=2) {
        tune.runoutMm=word(data_+13); tune.brakeMaxMs=word(data_+15);
      }
      if (version>=3) tune.reverseRotaries=word(data_+17);
      const uint8_t delta=data_[2]-generation_;
      if (tune.valid() && (slot_<0 || (delta && delta<128))) {
        active=tune; generation_=data_[2]; slot_=slot;
      }
    }
  }
public:
  Tuning active;
  void load() {
    active=Tuning();
    loadRecords(V1_FIRST,V1_SIZE,1);
    loadRecords(V2_FIRST,V2_SIZE,2);
    loadRecords(FIRST,SIZE,VERSION);
  }
  bool receive(char c,bool off) {
    if (c=='@') { reading_=true; length_=0; overflow_=false; }
    else if (!reading_) return false;
    else if (c=='\r' || c=='\n') {
      reading_=false; command_[length_]=0; dispatch(off);
    } else if (length_+1u<sizeof(command_)) command_[length_++]=c;
    else overflow_=true;
    return true;
  }
  bool service() {
    if (saving_ && eeprom_is_ready()) {
      const uint8_t dest=slot_==0 ? 1 : 0;
      const int base=FIRST+dest*SIZE;
      if (!position_) EEPROM.update(base,0);
      else if (position_<SIZE) EEPROM.update(base+position_,data_[position_]);
      else if (position_==SIZE) EEPROM.update(base,MARK);
      else {
        bool match=true;
        for (uint8_t i=0;i<SIZE;++i) match &= EEPROM.read(base+i)==data_[i];
        saving_=false;
        if (match) { slot_=dest; generation_=data_[2]; }
        strcpy_P(reply_,match ? PSTR("@SAVED") : PSTR("@ERR EEPROM"));
      }
      ++position_;
    }
    if (!reply_[0]) return false;
    if (Serial.availableForWrite()>=63) { Serial.println(reply_); reply_[0]=0; }
    return true;
  }
};
}
