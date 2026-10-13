#pragma once
#include <cstdint>
#include <vector>
#include <array>
#include <cstring>
struct MockBus {
  bool present=true, parasite=false, complete=true;
  std::vector<std::array<uint8_t,8>> roms;
  std::array<uint8_t,9> scratch{};
  int conversions=0;
};
extern MockBus mockBuses[40];
class OneWire {
  uint8_t pin; size_t idx=0, pos=0; uint8_t op=0; int configWrites=0;
public:
  explicit OneWire(uint8_t p):pin(p){}
  void reset_search(){idx=0;}
  bool search(uint8_t* out){ if(!mockBuses[pin].present||idx>=mockBuses[pin].roms.size())return false;
    std::memcpy(out,mockBuses[pin].roms[idx++].data(),8);return true; }
  bool reset(){pos=0;configWrites=0;return mockBuses[pin].present;}
  void select(const uint8_t*){}
  void write(uint8_t value, uint8_t=0){
    if(configWrites){--configWrites;return;} op=value;pos=0;
    if(value==0x4E)configWrites=3;
    if(value==0x44)mockBuses[pin].conversions++;
  }
  uint8_t read_bit(){return op==0xB4?!mockBuses[pin].parasite:mockBuses[pin].complete;}
  uint8_t read(){return mockBuses[pin].present?mockBuses[pin].scratch.at(pos++):0xFF;}
  static uint8_t crc8(const uint8_t* data, uint8_t count){uint8_t crc=0;
    for(uint8_t i=0;i<count;i++){uint8_t v=data[i];for(uint8_t j=0;j<8;j++){
      uint8_t mix=(crc^v)&1;crc>>=1;if(mix)crc^=0x8C;v>>=1;}}return crc;}
};
