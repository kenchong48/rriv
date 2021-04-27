#include "hardware.h"

TwoWire Wire1 (1);
TwoWire Wire2 (2);

void setupEEPROMSensorPins()
{
  unsigned char sensorPins[5] = {ANALOG_INPUT_1_PIN, ANALOG_INPUT_2_PIN, \
      ANALOG_INPUT_3_PIN, ANALOG_INPUT_4_PIN, ANALOG_INPUT_5_PIN};
  for (size_t i = 0; i < 5; i++)
  {
    int x = SENSOR_1_ADDRESS_START+(i*SENSOR_ADDRESS_LENGTH);
    Serial2.print("address:");
    Serial2.println(x);
    Serial2.print("   pin:");
    Serial2.println(sensorPins[i]);
    Serial2.flush();
    writeEEPROM(&Wire, EEPROM_I2C_ADDRESS, SENSOR_1_ADDRESS_START+(i*SENSOR_ADDRESS_LENGTH), sensorPins[i]);
  }
}