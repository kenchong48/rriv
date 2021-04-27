#include "thermistor.h"
#include "system/monitor.h"
#include "utilities/utilities.h"
#include "system/eeprom.h"
#include "system/clock.h"
//#include "system/hardware.h"

// populate struct thermistor from eeprom
void readThermistor(therm * newThermistor, byte address)
{
  unsigned int data;
  unsigned char * dataPtr = (unsigned char *)&data;
  readEEPROMBytes(address+SENSOR_PIN, dataPtr, sizeof(unsigned char));
  newThermistor->sPin = *(unsigned short*)dataPtr;
  readEEPROMBytes(address+SENSOR_TYPE, dataPtr, sizeof(unsigned char));
  newThermistor->sType = *(unsigned short*)dataPtr;
  readEEPROMBytes(address+CALIBRATION_BOOL, dataPtr, sizeof(unsigned char));
  newThermistor->calBool = *(unsigned short*)dataPtr;
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
  readEEPROMBytes(address+THERMISTOR_CALIBRATION_TIME, dataPtr, sizeof(unsigned int));
  newThermistor->calTime = *(unsigned int*)dataPtr;
  return;
}

/*bool checkThermistorCalibration() // redudant with dedicated byte
{
  unsigned int calTime = 0;
  bool thermistorCalibrated = false;

  readEEPROMBytes(TEMPERATURE_TIMESTAMP_ADDRESS_START, (unsigned char*)&calTime, TEMPERATURE_TIMESTAMP_ADDRESS_LENGTH);
  if (calTime > 1617681773 && calTime != 4294967295)
  {
    thermistorCalibrated = true;
  }
  return thermistorCalibrated;
}*/


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

/////QUESTION: pass around the address of the sensor, or the populated struct?
/*
* well, call to calibrate each thermistor only once, so not an issue here
* call to calculate each time we burst
* values will need to be read each time? or just the analog reading?
* if we don't call each time... we'd call at the monitor step once per sensor(thermistor)
*/

// calibrate a thermistor at sensor eeprom address
short calibrateThermistor(byte address)
{
  //v = mc+b    m = (v2-v1)/(c2-c1)    b = (m*-c1)+v1
  //C1 C2 M B are scaled up for storage, V1 V2 are scaled up for calculation
  therm toCalibrate;
  readThermistor(&toCalibrate, address);
  if (toCalibrate.sType != SENSOR_TYPE_THERMISTOR)
  {
    Monitor::instance()->writeDebugMessage(F("sensor type not thermistor"));
    return (-1);
  }
  else
  {
    float m, b;
    unsigned short slope;
    unsigned int intercept, tempCalTime;

    m = ((float)toCalibrate.v2-(float)toCalibrate.v1)/((float)toCalibrate.c2-(float)toCalibrate.c1);
    b = (((m*(0-(float)toCalibrate.c1))+(float)toCalibrate.v1)+((m*(0-(float)toCalibrate.c2))+(float)toCalibrate.v2))/2;

    slope = m * TEMPERATURE_SCALER;
    writeEEPROMBytes(address+THERMISTOR_M, (unsigned char *)&slope, sizeof(unsigned short));
    intercept = b;
    writeEEPROMBytes(address+THERMISTOR_B, (unsigned char *)&intercept, sizeof(unsigned int));
    tempCalTime = timestamp();
    writeEEPROMBytes(address+THERMISTOR_CAL_TIME, (unsigned char*)&tempCalTime, sizeof(unsigned int));
    writeEEPROM(&Wire, EEPROM_I2C_ADDRESS, address+CALIBRATION_BOOL, SENSOR_CALIBRATED);
    Monitor::instance()->writeDebugMessage(F("thermistor calibration complete"));
    return (1);
  }
  Monitor::instance()->writeDebugMessage(F("A GRAVE ERROR HAS BEEN MADE"));
  return (-3); // how did we get here!?
}

// calculate temperature for thermistor at sensor eeprom address
float calculateTemperature(byte address)
{
  therm toCalculate;
  float temperature = -1;
  readThermistor(&toCalculate, address);
  if ((toCalculate.sType != SENSOR_TYPE_THERMISTOR) && (toCalculate.calBool != SENSOR_CALIBRATED))
  {
    Monitor::instance()->writeDebugMessage(F("calculation fail"));
    return temperature;
  }
  else
  {
    float rawData = analogread(toCalculate.sPin)
    if (rawData == 0) // indicates thermistor disconnected
    {
      temperature = -2;
    }
    else
    {
      temperature = (rawData-(toCalculate.b/TEMPERATURE_SCALER))/(toCalculate.m/TEMPERATURE_SCALER);
    }
  }
  return temperature;
}

/*float calculateTemperature()
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
}*/

//print out calibration information & readings for thermistor at eeprom address
void monitorTemperature(byte address)
{
  blink(1,500);
  therm toMonitor;
  readThermistor(&toMonitor, address)
  float temperature = calculateTemperature(address);
  char valuesBuffer[200];
  sprintf(valuesBuffer,"thermistor at %i\n(%i,%i)(%i,%i)\nv=%ic+%i\ncalTime:%i\ntemperature.V:%i\ntemperature.C:%.2fC\n", \
    toMonitor.sPin, toMonitor.c1, toMonitor.v1, toMonitor.c2, toMonitor.v2, toMonitor.m, \
    toMonitor.b, toMonitor.calTime, analogRead(toMonitor.sPin), temperature);
}

/*void monitorTemperature() // print out calibration information & current readings
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
}*/