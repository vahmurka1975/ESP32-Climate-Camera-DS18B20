#include "Ds18b20Psychrometer.h"
#include <cmath>
#include <cstring>

Ds18b20Psychrometer::Ds18b20Psychrometer(uint8_t dryPin, uint8_t wetPin)
  : _dryBus(dryPin), _wetBus(wetPin) {}

bool Ds18b20Psychrometer::fail(Error error) {
  _error = error;
  _ready = false;
  _measuring = false;
  return false;
}
bool Ds18b20Psychrometer::discover(OneWire& bus, uint8_t address[8], bool dry) {
  uint8_t candidate[8], extra[8];
  bus.reset_search();
  if (!bus.search(candidate)) return fail(dry ? Error::DryMissing : Error::WetMissing);
  if (OneWire::crc8(candidate, 7) != candidate[7])
    return fail(dry ? Error::DryRomCrc : Error::WetRomCrc);
  if (candidate[0] != 0x28) return fail(dry ? Error::DryFamily : Error::WetFamily);
  if (bus.search(extra)) return fail(dry ? Error::DryMultiple : Error::WetMultiple);
  std::memcpy(address, candidate, 8);
  if (!bus.reset()) return fail(dry ? Error::DryBus : Error::WetBus);
  bus.select(address);
  bus.write(0xB4); // READ POWER SUPPLY: поддерживается только 3-проводное питание
  if (bus.read_bit() == 0) return fail(dry ? Error::DryParasite : Error::WetParasite);
  return true;
}
bool Ds18b20Psychrometer::scratchpad(OneWire& bus, const uint8_t address[8],
                                     uint8_t data[9], bool dry) {
  if (!bus.reset()) return fail(dry ? Error::DryBus : Error::WetBus);
  bus.select(address);
  bus.write(0xBE);
  for (uint8_t i = 0; i < 9; ++i) data[i] = bus.read();
  bool allZero = true, allFF = true;
  for (uint8_t i = 0; i < 9; ++i) {
    allZero = allZero && data[i] == 0;
    allFF = allFF && data[i] == 0xFF;
  }
  if (allZero || allFF || OneWire::crc8(data, 8) != data[8])
    return fail(dry ? Error::DryScratchpadCrc : Error::WetScratchpadCrc);
  return true;
}
bool Ds18b20Psychrometer::configure(OneWire& bus, const uint8_t address[8], bool dry) {
  uint8_t data[9];
  if (!scratchpad(bus, address, data, dry)) return false;
  if (!bus.reset()) return fail(dry ? Error::DryBus : Error::WetBus);
  bus.select(address);
  bus.write(0x4E); // WRITE SCRATCHPAD; без записи EEPROM при каждом старте
  bus.write(data[2]);
  bus.write(data[3]);
  bus.write(0x7F); // 12-bit
  return true;
}
bool Ds18b20Psychrometer::begin() {
  _ready = false;
  _measuring = false;
  if (!discover(_dryBus, _dryAddress, true) || !discover(_wetBus, _wetAddress, false))
    return false;
  if (!configure(_dryBus, _dryAddress, true) || !configure(_wetBus, _wetAddress, false))
    return false;
  _ready = true;
  _error = Error::Waiting;
  Serial.print(F("[DS18B20] DRY GPIO ROM="));
  for (uint8_t i = 0; i < 8; ++i) Serial.printf("%02X", _dryAddress[i]);
  Serial.print(F(" WET GPIO ROM="));
  for (uint8_t i = 0; i < 8; ++i) Serial.printf("%02X", _wetAddress[i]);
  Serial.println();
  return true;
}
bool Ds18b20Psychrometer::startOne(OneWire& bus, const uint8_t address[8], bool dry) {
  if (!bus.reset()) return fail(dry ? Error::DryBus : Error::WetBus);
  bus.select(address);
  bus.write(0x44, 0); // CONVERT T, внешнее питание
  return true;
}
bool Ds18b20Psychrometer::startMeasurement() {
  if (_measuring) return false;
  if (!_ready && !begin()) return false;
  if (!startOne(_dryBus, _dryAddress, true) || !startOne(_wetBus, _wetAddress, false))
    return false;
  _startedAt = millis();
  _measuring = true;
  return true;
}
bool Ds18b20Psychrometer::measurementReady() const {
  return _measuring && uint32_t(millis() - _startedAt) >= CONVERSION_MS;
}
bool Ds18b20Psychrometer::readOne(OneWire& bus, const uint8_t address[8],
                                 float& value, bool dry) {
  uint8_t data[9];
  if (!scratchpad(bus, address, data, dry)) return false;
  // Проверяем разрешение: при сбросе/подмене датчика нельзя молча использовать иной режим.
  if ((data[4] & 0x60) != 0x60) return fail(dry ? Error::DryResolution : Error::WetResolution);
  const int16_t raw = int16_t((uint16_t(data[1]) << 8) | data[0]);
  value = float(raw) / 16.0f;
  if (!std::isfinite(value) || value < -55.0f || value > 125.0f)
    return fail(dry ? Error::DryTemperature : Error::WetTemperature);
  return true;
}
bool Ds18b20Psychrometer::readMeasurement(float& dry, float& wet) {
  if (!measurementReady()) return false;
  // Каждая шина остаётся в состоянии CONVERT T до проверки готовности.
  if (_dryBus.read_bit() == 0) return fail(Error::DryConversion);
  if (_wetBus.read_bit() == 0) return fail(Error::WetConversion);
  float d, w;
  if (!readOne(_dryBus, _dryAddress, d, true) || !readOne(_wetBus, _wetAddress, w, false))
    return false;
  dry = d;
  wet = w;
  _measuring = false;
  _error = Error::None;
  return true;
}
const char* Ds18b20Psychrometer::errorText() const {
  switch (_error) {
    case Error::None: return "OK";
    case Error::Waiting: return "WAITING_FOR_PAIR";
    case Error::DryMissing: return "DRY_NOT_FOUND_GPIO21";
    case Error::WetMissing: return "WET_NOT_FOUND_GPIO22";
    case Error::DryMultiple: return "MORE_THAN_ONE_DRY_PROBE";
    case Error::WetMultiple: return "MORE_THAN_ONE_WET_PROBE";
    case Error::DryRomCrc: return "DRY_ROM_CRC";
    case Error::WetRomCrc: return "WET_ROM_CRC";
    case Error::DryFamily: return "DRY_NOT_DS18B20";
    case Error::WetFamily: return "WET_NOT_DS18B20";
    case Error::DryParasite: return "DRY_PARASITE_POWER_UNSUPPORTED";
    case Error::WetParasite: return "WET_PARASITE_POWER_UNSUPPORTED";
    case Error::DryBus: return "DRY_BUS_DISCONNECTED";
    case Error::WetBus: return "WET_BUS_DISCONNECTED";
    case Error::DryScratchpadCrc: return "DRY_SCRATCHPAD_CRC";
    case Error::WetScratchpadCrc: return "WET_SCRATCHPAD_CRC";
    case Error::DryConversion: return "DRY_CONVERSION_NOT_FINISHED";
    case Error::WetConversion: return "WET_CONVERSION_NOT_FINISHED";
    case Error::DryResolution: return "DRY_RESOLUTION_RESET";
    case Error::WetResolution: return "WET_RESOLUTION_RESET";
    case Error::DryTemperature: return "DRY_SENSOR_RANGE";
    case Error::WetTemperature: return "WET_SENSOR_RANGE";
  }
  return "DS18B20_ERROR";
}
