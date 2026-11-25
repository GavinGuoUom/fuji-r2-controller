#pragma once
#include <Arduino.h>
namespace fuji {
struct Pins { uint8_t pwm, dir, left, right, v1, v2, start, green, speed, ramp, minimum, current; };
constexpr Pins PINS[3] = {
  {46,22,2,3,26,27,6,38,A0,A1,A2,A9},
  {45,23,18,19,28,29,7,39,A3,A4,A5,A10},
  {44,24,20,21,30,31,8,40,A6,A7,A8,A11}
};
constexpr uint8_t TOTAL = 10, READY = 35, FAULT = 37;
#if defined(FUJI_PCB_R2) && !defined(FUJI_PCB_R1)
constexpr uint8_t BUS_N = 32, BUS_SINK = 36, POR_N = 41, ADC_COUNT = 12;
#elif defined(FUJI_PCB_R1) && !defined(FUJI_PCB_R2)
constexpr uint8_t DRIVER_N[] = {32, 33, 34};
constexpr uint8_t FAULT_SET = 36, POWER = 41, VM = A12, ADC_COUNT = 13;
#else
#error "Select exactly one Fuji shield revision"
#endif
// Tune only after installing the feeder and checking direction/5558 orientation.
constexpr bool DIR_RIGHT_HIGH[3] = {true,true,true};
constexpr bool BIAS_V1_FOR_DIR_HIGH[3] = {true,true,true};
}
