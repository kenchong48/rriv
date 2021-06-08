#ifndef WATERBEAR_EEPROM
#define WATERBEAR_EEPROM

#include <Arduino.h>
#include <Wire.h> // Communicate with I2C/TWI devices

/*
 * Partition scheme (24LC01B 1Kbit)
 * 000-099 Waterbear Device Info
 * 100-199
 * 200-249
 * 250-299 Waterbear Device Calibration / Significant Values
 * 300-399
 * 400-499
 * 500-549 Sensor 1 Calibration
 * 550-599 Sensor 2 Calibration
 * 600-649 Sensor 3 Calibration
 * 650-699 Sensor 4 Calibration
 * 700-749 Sensor 5 Calibration
 * 750-799 Sensor 6 Calibration
 * 800-849 Sensor 7 Calibration
 * 850-899 Sensor 8 Calibration
 * 900-949 Sensor 9 Calibration
 * 950-999 Sensor 10 Calibration
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

// Waterbear Device Calibration / Significant Values
#define BURST_INTERVAL_ADDRESS 250
#define BURST_LENGTH_ADDRESS 251
#define USER_WAKE_TIMEOUT_ADDRESS 252 // timeout after wakeup from user interaction, seconds->min?
#define SD_FIELDCOUNT_ADDRESS 253

// Debug settings bit register map {outdated with various serial command modes?}
/*
 * 0 measurements: enable log messages related to measurements & bursts
 * 1 loop: don't sleep
 * 2 short sleep: sleep for a hard coded short amount of time
 * 3 to file: also send debug messages to the output file
 * 4 to serial: send debug messages to the serial interface
*/

#define DEBUG_SETTINGS_ADDRESS 404 // 8 booleans {measurements, loop, short sleep, to file, to serial}


// Thermistor Calibration Block
#define TEMPERATURE_C1_ADDRESS_START 950 //64
#define TEMPERATURE_C1_ADDRESS_END 951 //65
#define TEMPERATURE_C1_ADDRESS_LENGTH 2

#define TEMPERATURE_V1_ADDRESS_START 952 //66
#define TEMPERATURE_V1_ADDRESS_END 953 //67
#define TEMPERATURE_V1_ADDRESS_LENGTH 2

#define TEMPERATURE_C2_ADDRESS_START 954 //68
#define TEMPERATURE_C2_ADDRESS_END 955 //69
#define TEMPERATURE_C2_ADDRESS_LENGTH 2

#define TEMPERATURE_V2_ADDRESS_START 956 //70
#define TEMPERATURE_V2_ADDRESS_END 957 //71
#define TEMPERATURE_V2_ADDRESS_LENGTH 2

#define TEMPERATURE_TIMESTAMP_ADDRESS_START 958//72 // epoch timestamp
#define TEMPERATURE_TIMESTAMP_ADDRESS_END 961//75
#define TEMPERATURE_TIMESTAMP_ADDRESS_LENGTH 4

#define TEMPERATURE_M_ADDRESS_START 962 //76 // float xxx.xx =*100> unsigned short xxxxx
#define TEMPERATURE_M_ADDRESS_END 963 //77
#define TEMPERATURE_M_ADDRESS_LENGTH 2

#define TEMPERATURE_B_ADDRESS_START 964 //78
#define TEMPERATURE_B_ADDRESS_END 967 //81
#define TEMPERATURE_B_ADDRESS_LENGTH 4

#define TEMPERATURE_SCALER 100 // applies to C1 C2 M B values for storage
#define TEMPERATURE_BLOCK_LENGTH 18//for resetting 64-81

void writeEEPROM(TwoWire * wire, int deviceaddress, byte eeaddress, byte data );
byte readEEPROM(TwoWire * wire, int deviceaddress, byte eeaddress );

void readDeploymentIdentifier(char * deploymentIdentifier);
void writeDeploymentIdentifier(char * deploymentIdentifier);

void readUniqueId(unsigned char * uuid); // uuid must point to char[UUID_LENGTH]

void writeEEPROMBytes(byte address, unsigned char * data, uint8_t size);
void readEEPROMBytes(byte address, unsigned char * data, uint8_t size);

void clearEEPROMAddress(byte address, uint8_t length);

#endif