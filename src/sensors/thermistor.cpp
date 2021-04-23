#include "thermistor.h"
#include "system/monitor.h"
#include "utilities/utilities.h"
#include "system/eeprom.h"
#include "system/clock.h"

// populate struct thermistor from eeprom
short readThermistor(thermistor * newThermistor, byte address)
{
  if (readEEPROM(&Wire, EEPROM_I2C_ADDRESS, address+SENSOR_TYPE) != 0)
  {
    Monitor::instance()->writeDebugMessage(F("sensor not a thermistor"));
    return (-1);
  }
  else if (readEEPROM(&Wire, EEPROM_I2C_ADDRESS, address+CALIBRATION_BOOL) != 1)
  {
    Monitor::instance()->writeDebugMessage(F("sensor not calibrated"));
    return (-2);
  }
  else
  {
    unsigned int data;
    unsigned char * dataPtr = (unsigned char *)&data;
    readEEPROMBytes(address+THERMISTOR_C1, dataPtr, sizeof(unsigned short));
    newThermistor->c1 = *(unsigned short*)dataPtr;
    readEEPROMBytes(address+THERMISTOR_V1, dataPtr, sizeof(unsigned short));
    newThermistor->v1 = *(unsigned short*)dataPtr;
    readEEPROMBytes(address+THERMISTOR_C2, dataPtr, sizeof(unsigned short));
    newThermistor->c2 = *(unsigned short*)dataPtr;
    readEEPROMBytes(address+THERMISTOR_V2, dataPtr, sizeof(unsigned short));
    newThermistor->v2 = *(unsigned short*)dataPtr;
    readEEPROMBytes(address+THERMISTOR_M, dataPtr, sizeof(unsigned short));
    newThermistor->m = *(unsigned short*)dataPtr;
    readEEPROMBytes(address+THERMISTOR_B, dataPtr, sizeof(unsigned int));
    newThermistor->b = *(unsigned int*)dataPtr;
    readEEPROMBytes(address+THERMISTOR_CAL_TIME, dataPtr, sizeof(unsigned int));
    newThermistor->calTime = *(unsigned int*)dataPtr;
    return (1);
  }
  Monitor::instance()->writeDebugMessage(F("A GRAVE ERROR HAS BEEN MADE"));
  return (-3); // how did we get here!?
}

bool checkThermistorCalibration() // redudant with dedicated byte
{
  unsigned int calTime = 0;
  bool thermistorCalibrated = false;

  readEEPROMBytes(TEMPERATURE_TIMESTAMP_ADDRESS_START, (unsigned char*)&calTime, TEMPERATURE_TIMESTAMP_ADDRESS_LENGTH);
  if (calTime > 1617681773 && calTime != 4294967295)
  {
    thermistorCalibrated = true;
  }
  return thermistorCalibrated;
}


/*void calibrateThermistor() // calibrate using linear slope equation, log time
{
  //v = mc+b    m = (v2-v1)/(c2-c1)    b = (m*-c1)+v1
  //C1 C2 M B are scaled up for storage, V1 V2 are scaled up for calculation
  float c1,v1,c2,v2,m,b;
  unsigned short slope, read;
  unsigned int intercept;
  unsigned char * dataPtr = (unsigned char *)&read;
  readEEPROMBytes(TEMPERATURE_C1_ADDRESS_START, dataPtr, TEMPERATURE_C1_ADDRESS_LENGTH);
  c1 = *(unsigned short *)dataPtr;
  readEEPROMBytes(TEMPERATURE_V1_ADDRESS_START, dataPtr, TEMPERATURE_V1_ADDRESS_LENGTH);
  v1 = *(unsigned short *)dataPtr * TEMPERATURE_SCALER;
  readEEPROMBytes(TEMPERATURE_C2_ADDRESS_START, dataPtr, TEMPERATURE_C2_ADDRESS_LENGTH);
  c2 = *(unsigned short *)dataPtr;
  readEEPROMBytes(TEMPERATURE_V2_ADDRESS_START, dataPtr, TEMPERATURE_V2_ADDRESS_LENGTH);
  v2 = *(unsigned short *)dataPtr * TEMPERATURE_SCALER;
  m = (v2-v1)/(c2-c1);
  b = (((m*(0-c1)) + v1) + ((m*(0-c2)) + v2))/2; //average at two points

  slope = m * TEMPERATURE_SCALER;
  writeEEPROMBytes(TEMPERATURE_M_ADDRESS_START, (unsigned char*)&slope, TEMPERATURE_M_ADDRESS_LENGTH);
  intercept = b;
  writeEEPROMBytes(TEMPERATURE_B_ADDRESS_START, (unsigned char*)&intercept, TEMPERATURE_B_ADDRESS_LENGTH);
  unsigned int tempCalTime= timestamp();
  writeEEPROMBytes(TEMPERATURE_TIMESTAMP_ADDRESS_START, (unsigned char*)&tempCalTime, TEMPERATURE_TIMESTAMP_ADDRESS_LENGTH);
  Monitor::instance()->writeDebugMessage(F("thermistor calibration complete"));
}*/

// calibrate a thermistor at sensor address
short calibrateThermistor(byte address)
{
  thermistor toCalibrate;
  if (readThermistor(&toCalibrate, address) != 1)
  {
    Monitor::instance()->writeDebugMessage(F("thermistor needs to be setup"));
    return (-1);
  }
  else
  {
    float m, b;
    //float x1, y1, x2, y2;
    unsigned short slope;
    unsigned int intercept, tempCalTime;
    //x1 = toCalibrate.c1;
    //y1 = toCalibrate.v1;
    //x2 = toCalibrate.c2;
    //y2 = toCalibrate.v2;

    m = ((float)toCalibrate.v2-(float)toCalibrate.v1)/((float)toCalibrate.c2-(float)toCalibrate.c1);
    b = (((m*(0-(float)toCalibrate.c1)) + (float)toCalibrate.v1) + ((m*(0-(float)toCalibrate.c2))+ (float)toCalibrate.v2))/2;

    //m = (y2-y1)/(x2-x1);
    //b = (((m*(0-x1)) + y1) + ((m*(0-x2)) + y2))/2; //average at two points

    slope = m * TEMPERATURE_SCALER;
    writeEEPROMBytes(address+THERMISTOR_M, (unsigned char *)&slope, sizeof(unsigned short));
    intercept = b;
    writeEEPROMBytes(address+THERMISTOR_B, (unsigned char *)&intercept, sizeof(unsigned int));
    tempCalTime = timestamp();
    writeEEPROMBytes(address+THERMISTOR_CAL_TIME, (unsigned char*)&tempCalTime, sizeof(unsigned int));
    writeEEPROM(&Wire, EEPROM_I2C_ADDRESS, address+CALIBRATION_BOOL, 1);
    Monitor::instance()->writeDebugMessage(F("thermistor calibration complete"));
    return (1);
  }
  Monitor::instance()->writeDebugMessage(F("A GRAVE ERROR HAS BEEN MADE"));
  return (-3); // how did we get here!?
}

float calculateTemperature()
{
  //v = mx+b  =>  x = (v-b)/m
  //C1 C2 M B are scaled up for storage, V1 V2 are scaled up for calculation
  float temperature = -1;
  if (checkThermistorCalibration() == true)
  {
    unsigned short m = 0;
    unsigned int b = 0;
    float rawData = analogRead(PB1);
    if (rawData == 0) // indicates thermistor disconnect
    {
      temperature = -2;
    }
    else
    {
      readEEPROMBytes(TEMPERATURE_M_ADDRESS_START, (unsigned char*)&m, TEMPERATURE_M_ADDRESS_LENGTH);
      readEEPROMBytes(TEMPERATURE_B_ADDRESS_START, (unsigned char *)&b, TEMPERATURE_B_ADDRESS_LENGTH);
      temperature = (rawData-(b/TEMPERATURE_SCALER))/(m/TEMPERATURE_SCALER);
    }
  }
  else
  {
    Monitor::instance()->writeDebugMessage(F("Thermistor not calibrated"));
  }
  return temperature;
}

void monitorTemperature() // print out calibration information & current readings
{
  blink(1,500);
  unsigned short c1, v1, c2, v2, m;
  unsigned int b;
  unsigned int calTime;
  unsigned char data = 0;
  unsigned char * dataPtr = &data;

  //C1 C2 M B are scaled up for storage, V1 V2 are scaled up for calculation
  readEEPROMBytes(TEMPERATURE_C1_ADDRESS_START, dataPtr, TEMPERATURE_C1_ADDRESS_LENGTH);
  c1 = *(unsigned short *)dataPtr; //4
  readEEPROMBytes(TEMPERATURE_V1_ADDRESS_START, dataPtr, TEMPERATURE_V1_ADDRESS_LENGTH);
  v1 = *(unsigned short *)dataPtr; //4
  readEEPROMBytes(TEMPERATURE_C2_ADDRESS_START, dataPtr, TEMPERATURE_C2_ADDRESS_LENGTH);
  c2 = *(unsigned short *)dataPtr; //4
  readEEPROMBytes(TEMPERATURE_V2_ADDRESS_START, dataPtr, TEMPERATURE_V2_ADDRESS_LENGTH);
  v2 = *(unsigned short *)dataPtr; //4
  readEEPROMBytes(TEMPERATURE_M_ADDRESS_START, dataPtr, TEMPERATURE_M_ADDRESS_LENGTH);
  m = *(unsigned short *)dataPtr; //4
  readEEPROMBytes(TEMPERATURE_B_ADDRESS_START, dataPtr, TEMPERATURE_B_ADDRESS_LENGTH);
  b = *(unsigned int *)dataPtr; // 5
  readEEPROMBytes(TEMPERATURE_TIMESTAMP_ADDRESS_START, dataPtr, TEMPERATURE_TIMESTAMP_ADDRESS_LENGTH);
  calTime = *(unsigned int *)dataPtr; // 10

  float temperature = calculateTemperature();

  char valuesBuffer[200];
  sprintf(valuesBuffer,"EEPROM thermistor block\n(%i,%i)(%i,%i)\nv=%ic+%i\ncalTime:%i\ntemperature.V:%i\ntemperature.C:%.2fC\n", c1, v1, c2, v2, m, b, calTime, analogRead(PB1), temperature);
  Monitor::instance()->writeDebugMessage(F(valuesBuffer));
}