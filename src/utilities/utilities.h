#ifndef WATERBEAR_UTILITIES
#define WATERBEAR_UTILITIES

// #include <Arduino.h>
#include <Wire_slave.h> // Communicate with I2C/TWI devices
#include "DS3231.h"
#include "configuration.h"
#include "system/control.h"


void printInterruptStatus(HardwareSerial &serial);
void printDateTime(HardwareSerial &serial, DateTime now);

void blink(int times, int duration);
void printDS3231Time();
void printNVICStatus();

//void pinBlink(uint8 pin, int times, int duration);
void warmup(int minutes);

#endif
