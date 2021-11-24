#include <Wire_slave.h> // Communicate with I2C/TWI devices
#include <SPI.h>
#include "SdFat.h"

#include "configuration.h"
#include "utilities/utilities.h"

#include "system/ble.h"
#include "system/clock.h"
#include "system/control.h"
#include "system/eeprom.h"
#include "system/filesystem.h"
#include "system/hardware.h"
#include "system/interrupts.h"
#include "system/low_power.h"
#include "system/monitor.h"
#include "system/switched_power.h"
#include "system/adc.h"

#include <sensors/atlas_oem.h>

// Settings

extern short interval;     // minutes between loggings when not in short sleep
extern short burstLength; // how many readings in a burst
extern short burstDelay; // minutes before starting a burst
extern short burstLoops; // how many iterations of bursts

extern short fieldCount; // number of fields to be logged to SDcard file


// State
extern WaterBear_FileSystem *filesystem;
extern unsigned char uuid[UUID_LENGTH];
extern char uuidString[25]; // 2 * UUID_LENGTH + 1
extern char **values;

extern unsigned long lastMillis;
extern uint32_t awakeTime;
extern uint32_t lastTime;
extern short burstCount;
extern short burstLoopCount;
extern bool configurationMode;
extern bool debugValuesMode;
extern bool clearModes;
extern bool tempCalMode;
// extern AtlasRGB rgbSensor;
//extern bool thermistorCalibrated;

extern bool figMethane; // whether methane sensor is to be used or not
extern bool firstBurst; //keep track of the first burst for timestamping purposes

extern AD7091R * externalADC;

void enableI2C1();

void enableI2C2();

void powerUpSwitchableComponents();

void powerDownSwitchableComponents();

void startSerial2();

void setupHardwarePins();

void blinkTest();

void initializeFilesystem();

void allocateMeasurementValuesMemory();

void prepareForTriggeredMeasurement();

void prepareForUserInteraction();

void setNotBursting();

void setNotBurstLooping();

void measureSensorValues();

bool checkBursting();

bool checkBurstLoop();

bool checkFirstBurst(bool bursting, bool awakeForUserInteraction);

bool checkDebugLoop();

bool checkAwakeForUserInteraction(bool debugLoop);

bool checkTakeMeasurement(bool bursting, bool awakeForUserInteraction);

void stopAndAwaitTrigger();

void handleControlCommand();

void monitorConfiguration();

void takeNewMeasurement();

void trackBurst(bool bursting);

void trackBurstLoop(bool burstLooping);

void monitorValues();

void calibrateThermistor();

void monitorTemperature();

bool checkThermistorCalibration();

int measureMethaneSensorValues();

void clearThermistorCalibration();

float calculateTemperature();

void processControlFlag(char *flag);
