#ifndef WATERBEAR_THERMISTOR
#define WATERBEAR_THERMISTOR

/*
 * thermistor.h
 *
 * Struct and function declarations for dealing with thermistor
 * current thermistor: [OCR] NTC 10k Ohm 1% 3950beta
 */

//register offsets
#define SENSOR_PIN 0
#define SENSOR_TYPE 1
#define CALIBRATION_BOOL 2
#define THERMISTOR_C1 3
#define THERMISTOR_V1 5
#define THERMISTOR_C2 7
#define THERMISTOR_V2 9
#define THERMISTOR_M 11
#define THERMISTOR_B 13
#define THERMISTOR_CALIBRATION_TIME 17


typedef struct thermistor
{
  unsigned char sPin, sType, calBool;
  unsigned short c1, v1, c2, v2, m;
  unsigned int b, calTime;
} therm;

void readThermistor(therm * newThermistor, byte address);
short calibrateThermistor(byte address);
float calculateTemperature(byte address);
void monitorTemperature(byte address);

#endif