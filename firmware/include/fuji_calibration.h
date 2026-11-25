#pragma once
#include "fuji_control.h"
#include <string.h>

namespace fuji {
constexpr uint8_t POT_COUNT = 9;
struct PotRange {
  uint16_t low = 0, high = 1023; // Mechanical MIN/MAX, including reversed wiring.
  bool valid() const {
    return low <= 1023 && high <= 1023 && (low > high ? low-high : high-low) >= 128;
  }
  uint16_t scale(uint16_t adc) const {
    const int offset = high >= low ? int(adc)-int(low) : int(low)-int(adc);
    const int span = high >= low ? high-low : low-high;
    if (offset <= 8) return 0;
    if (offset >= span-8) return FULL;
    return uint32_t(offset-8)*FULL/(span-16);
  }
};

// Two 41-byte slots. The marker is published last; CRC covers version,
// generation and all little-endian ADC endpoints.
class PotStore {
  static constexpr uint8_t BYTES=41, MARK=0xA7, VERSION=1;
  uint8_t record_[BYTES] = {}, generation_=0, position_=0;
  int8_t slot_=-1;
  static uint16_t crc(const uint8_t *p) {
    uint16_t result=0xffff;
    for (uint8_t i=1;i<BYTES-2;++i) {
      result ^= uint16_t(p[i])<<8;
      for (uint8_t b=0;b<8;++b)
        result = result & 0x8000 ? (result<<1)^0x1021 : result<<1;
    }
    return result;
  }
  static uint16_t word(const uint8_t *p) { return p[0] | (uint16_t(p[1])<<8); }
  static void put(uint8_t *p,uint16_t v) { p[0]=uint8_t(v); p[1]=uint8_t(v>>8); }
  template<class Memory> bool readSlot(Memory &memory,uint8_t slot) {
    for (uint8_t i=0;i<BYTES;++i) record_[i]=memory.read(slot*BYTES+i);
    if (record_[0]!=MARK || record_[1]!=VERSION || word(record_+BYTES-2)!=crc(record_)) return false;
    PotRange ranges[POT_COUNT];
    for (uint8_t i=0;i<POT_COUNT;++i) {
      ranges[i].low=word(record_+3+4*i);
      ranges[i].high=word(record_+5+4*i);
      if (!ranges[i].valid()) return false;
    }
    uint8_t delta=record_[2]-generation_;
    if (slot_>=0 && (!delta || delta>=128)) return false;
    memcpy(active,ranges,sizeof(active));
    generation_=record_[2]; slot_=slot;
    return true;
  }
public:
  PotRange active[POT_COUNT];
  bool saved() const { return slot_>=0; }
  template<class Memory> void load(Memory &memory) {
    readSlot(memory,0); readSlot(memory,1);
  }
  void prepare(const PotRange ranges[POT_COUNT]) {
    record_[0]=MARK; record_[1]=VERSION; record_[2]=generation_+1;
    for (uint8_t i=0;i<POT_COUNT;++i) {
      put(record_+3+4*i,ranges[i].low); put(record_+5+4*i,ranges[i].high);
    }
    put(record_+BYTES-2,crc(record_));
    position_=0;
  }
  // Caller waits for hardware ready between calls, including before readback.
  enum SaveResult { Busy, Saved, Error };
  template<class Memory> SaveResult writeNext(Memory &memory) {
    const uint8_t dest=slot_==0 ? 1 : 0;
    const int base=dest*BYTES;
    if (position_==0) memory.update(base,0);
    else if (position_<BYTES) memory.update(base+position_,record_[position_]);
    else if (position_==BYTES) memory.update(base,MARK);
    else {
      for (uint8_t i=0;i<BYTES;++i)
        if (memory.read(base+i)!=record_[i]) return Error;
      return readSlot(memory,dest) ? Saved : Error;
    }
    ++position_;
    return Busy;
  }
};
}
