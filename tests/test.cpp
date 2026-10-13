#include "Ds18b20Psychrometer.h"
#include "PsychrometricMath.h"
#include <cassert>
#include <iostream>
#include <limits>
uint32_t mockMillis=0;
MockSerial Serial;
MockBus mockBuses[40];
static void prepare(uint8_t pin, float t, uint8_t id){
  mockBuses[pin]=MockBus{};std::array<uint8_t,8> rom{{0x28,id,2,3,4,5,6,0}};
  rom[7]=OneWire::crc8(rom.data(),7);mockBuses[pin].roms.push_back(rom);
  int16_t raw=int16_t(t*16);auto &s=mockBuses[pin].scratch;
  s={{uint8_t(raw&255),uint8_t((raw>>8)&255),75,70,0x7F,0xFF,0x0C,0x10,0}};
  s[8]=OneWire::crc8(s.data(),8);
}
int main(){
  using namespace PsychrometricMath;
  auto r=calculate(12,10);assert(r.ok && r.humidity>76 && r.humidity<80);
  r=calculate(20,20);assert(r.ok && std::abs(r.humidity-100)<0.001);
  assert(calculate(20,20.25f).ok);
  assert(!calculate(20,21).ok);
  assert(!calculate(12,-1).ok);
  assert(!calculate(85,10).ok);
  assert(!calculate(1,0.7f).ok);
  assert(!calculate(30,1).ok);
  assert(!calculate(12,10,200).ok);
  assert(!calculate(12,10,1013.25f,-0.1f).ok);
  assert(!calculate(std::numeric_limits<float>::quiet_NaN(),10).ok);
  const auto lowP=calculate(20,16,850);const auto highP=calculate(20,16,1013.25f);
  assert(lowP.ok&&highP.ok&&lowP.humidity>highP.humidity);
  prepare(21,12,1);prepare(22,10,9);
  Ds18b20Psychrometer driver(21,22);assert(driver.begin());
  assert(driver.startMeasurement());assert(!driver.measurementReady());
  float dry=0,wet=0;mockMillis=799;assert(!driver.readMeasurement(dry,wet));
  mockMillis=800;assert(driver.measurementReady());assert(driver.readMeasurement(dry,wet));
  assert(dry==12&&wet==10);assert(mockBuses[21].conversions==1&&mockBuses[22].conversions==1);
  assert(driver.startMeasurement());mockMillis+=800;
  mockBuses[22].scratch[8]^=1;assert(!driver.readMeasurement(dry,wet));
  assert(std::strcmp(driver.errorText(),"WET_SCRATCHPAD_CRC")==0);
  prepare(22,10,9);assert(driver.startMeasurement());mockMillis+=800;assert(driver.readMeasurement(dry,wet));
  mockBuses[21].present=false;assert(!driver.startMeasurement());
  prepare(21,12,1);mockBuses[22].parasite=true;assert(!driver.begin());
  mockBuses[22].parasite=false;mockBuses[21].roms.push_back(mockBuses[21].roms[0]);assert(!driver.begin());
  prepare(21,12,1);prepare(22,10,9);assert(driver.begin());
  mockMillis=0xFFFFFF00u;assert(driver.startMeasurement());mockMillis+=800;
  assert(driver.measurementReady());assert(driver.readMeasurement(dry,wet));
  assert(driver.startMeasurement());mockMillis+=800;mockBuses[22].complete=false;
  assert(!driver.readMeasurement(dry,wet));
  std::cout<<"PASS: psychrometric formula, range checks, CRC, external power, fixed roles, retry, conversion timing, millis wrap\n";
  std::cout<<"Reference RH at 12/10 C: "<<calculate(12,10).humidity<<" %\n";
}
