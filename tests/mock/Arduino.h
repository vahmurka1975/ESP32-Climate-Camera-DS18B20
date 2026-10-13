#pragma once
#include <cstdint>
#include <cstddef>
#define F(x) x
extern uint32_t mockMillis;
inline uint32_t millis() { return mockMillis; }
struct MockSerial { template<class... T> void print(T...){ } template<class... T> void println(T...){ } template<class... T> void printf(T...){ } };
extern MockSerial Serial;
