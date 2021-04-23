#ifndef WATERBEAR_THERMISTOR
#define WATERBEAR_THERMISTOR

/*
 * thermistor.h
 *
 * Struct and function declarations for dealing with thermistor
 * current thermistor: [OCR] NTC 10k Ohm 1% 3950beta
 */

//register offsets
#define SENSOR_TYPE 0
#define CALIBRATION_BOOL 1
#define THERMISTOR_C1 2
#define THERMISTOR_V1 4
#define THERMISTOR_C2 6
#define THERMISTOR_V2 8
#define THERMISTOR_M 10
#define THERMISTOR_B 12
#define THERMISTOR_CAL_TIME 16


typedef struct thermistor
{
  //sensor pin
  unsigned short pin;

  //calibration info
  unsigned short c1, v1, c2, v2, m;
  unsigned int b;
  unsigned int calTime;
} therm;

short readThermistor(thermistor * newThermistor, byte address);
short calibrateThermistor(byte address);
#endif