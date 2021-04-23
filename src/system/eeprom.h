#ifndef WATERBEAR_EEPROM
#define WATERBEAR_EEPROM

#include <Arduino.h>
#include <Wire.h> // Communicate with I2C/TWI devices

/*
 * Partition scheme (24LC01B 1Kbit)
 * 000-099 Waterbear Device Info
 * 100-199 Waterbear Device Calibration / Significant Values
 * 200-299
 * 300-399
 * 400-499
 * 500-599 Sensor Calibration Block 1
 * 600-699 Sensor Calibration Block 2
 * 700-799 Sensor Calibration Block 3
 * 800-899 Sensor Calibration Block 4
 * 900-999 Sensor Calibration Block 5
*/

#define EEPROM_I2C_ADDRESS 0x50
#define EEPROM_RESET_VALUE 255 // max value of a byte

// Waterbear Device Info
#define EEPROM_UUID_ADDRESS_START 0
#define EEPROM_UUID_ADDRESS_END 15
#define UUID_LENGTH 12 // STM32 has a 12 byte UUID, leave extra space for the future 16

#define EEPROM_DEPLOYMENT_IDENTIFIER_ADDRESS_START 16
#define EEPROM_DEPLOYMENT_IDENTIFIER_ADDRESS_END 43
#define DEPLOYMENT_IDENTIFIER_LENGTH 25 // out of 28

#define DEVICE_SERIAL_NUMBER_ADDRESS_START 44
#define DEVICE_SERIAL_NUMBER_ADDRESS_END 63
#define DEVICE_SERIAL_NUMBER_LENGTH 16 // out of 20

//Thermistor Calibration Block
#define TEMPERATURE_C1_ADDRESS_START 64
#define TEMPERATURE_C1_ADDRESS_END 65
#define TEMPERATURE_C1_ADDRESS_LENGTH 2

#define TEMPERATURE_V1_ADDRESS_START 66
#define TEMPERATURE_V1_ADDRESS_END 67
#define TEMPERATURE_V1_ADDRESS_LENGTH 2

#define TEMPERATURE_C2_ADDRESS_START 68
#define TEMPERATURE_C2_ADDRESS_END 69
#define TEMPERATURE_C2_ADDRESS_LENGTH 2

#define TEMPERATURE_V2_ADDRESS_START 70
#define TEMPERATURE_V2_ADDRESS_END 71
#define TEMPERATURE_V2_ADDRESS_LENGTH 2

#define TEMPERATURE_TIMESTAMP_ADDRESS_START 72 // epoch timestamp
#define TEMPERATURE_TIMESTAMP_ADDRESS_END 75
#define TEMPERATURE_TIMESTAMP_ADDRESS_LENGTH 4

#define TEMPERATURE_M_ADDRESS_START 76 // float xxx.xx =*100> unsigned short xxxxx
#define TEMPERATURE_M_ADDRESS_END 77
#define TEMPERATURE_M_ADDRESS_LENGTH 2

#define TEMPERATURE_B_ADDRESS_START 78
#define TEMPERATURE_B_ADDRESS_END 81
#define TEMPERATURE_B_ADDRESS_LENGTH 4

#define TEMPERATURE_SCALER 100 // applies to C1 C2 M B values for storage
#define TEMPERATURE_BLOCK_LENGTH 18//for resetting 64-81

#define TEST_START 82
#define TEST_END 83
#define TEST_LENGTH 2


/*
 * Sensor General Register Map:
 * 0 sensor type
 * 1 calibrated boolean (1 yes, 0 no)
 * 2-29 calibration variables
*/
#define SENSOR_ADDRESS_LENGTH 50
#define SENSOR_1_ADDRESS_START 500
#define SENSOR_2_ADDRESS_START 550
#define SENSOR_3_ADDRESS_START 600
#define SENSOR_4_ADDRESS_START 650
#define SENSOR_5_ADDRESS_START 700
#define SENSOR_6_ADDRESS_START 750
#define SENSOR_7_ADDRESS_START 800
#define SENSOR_8_ADDRESS_START 850
#define SENSOR_9_ADDRESS_START 900
#define SENSOR_10_ADDRESS_START 950

// Sensor Types
#define SENSOR_TYPE_THERMISTOR 0

/*
 * Thermistor Register Map
 * 0 sensor type = 0
 * 1 calibrated boolean (1 yes, 0 no)
 * 2-3 c1
 * 4-5 v1
 * 6-7 c2
 * 8-9 v2
 * 10-11 m
 * 12-15 b
 * 16-19 calTime
*/


void writeEEPROM(TwoWire * wire, int deviceaddress, byte eeaddress, byte data );
byte readEEPROM(TwoWire * wire, int deviceaddress, byte eeaddress );

void readDeploymentIdentifier(char * deploymentIdentifier);
void writeDeploymentIdentifier(char * deploymentIdentifier);

void readUniqueId(unsigned char * uuid); // uuid must point to char[UUID_LENGTH]

void writeEEPROMBytes(byte address, unsigned char * data, uint8_t size);
void readEEPROMBytes(byte address, unsigned char * data, uint8_t size);

//new
void clearEEPROMAddress(byte address, uint8_t length);

#endif