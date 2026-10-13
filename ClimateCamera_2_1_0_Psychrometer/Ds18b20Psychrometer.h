#pragma once
#include <Arduino.h>
#include <OneWire.h>

class Ds18b20Psychrometer {
public:
  enum class Error {
    None, Waiting, DryMissing, WetMissing, DryMultiple, WetMultiple,
    DryRomCrc, WetRomCrc, DryFamily, WetFamily,
    DryParasite, WetParasite, DryBus, WetBus,
    DryScratchpadCrc, WetScratchpadCrc, DryConversion, WetConversion,
    DryResolution, WetResolution, DryTemperature, WetTemperature
  };
  Ds18b20Psychrometer(uint8_t dryPin, uint8_t wetPin);
  bool begin();
  bool startMeasurement();
  bool measurementReady() const;
  bool readMeasurement(float& dry, float& wet);
  bool isMeasuring() const { return _measuring; }
  const char* errorText() const;
  static constexpr uint32_t CONVERSION_MS = 800; // 12 bit: максимум 750 ms + запас

private:
  OneWire _dryBus;
  OneWire _wetBus;
  uint8_t _dryAddress[8]{};
  uint8_t _wetAddress[8]{};
  bool _ready = false;
  bool _measuring = false;
  uint32_t _startedAt = 0;
  Error _error = Error::Waiting;
  bool discover(OneWire& bus, uint8_t address[8], bool dry);
  bool scratchpad(OneWire& bus, const uint8_t address[8], uint8_t data[9], bool dry);
  bool configure(OneWire& bus, const uint8_t address[8], bool dry);
  bool startOne(OneWire& bus, const uint8_t address[8], bool dry);
  bool readOne(OneWire& bus, const uint8_t address[8], float& value, bool dry);
  bool fail(Error error);
};
