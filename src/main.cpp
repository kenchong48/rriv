#include <Arduino.h>
#include <libmaple/libmaple.h>
#include <libmaple/pwr.h> // necessary?
#include <string.h>
#include <Ezo_i2c.h>

#include "datalogger.h"
#include "scratch/dbgmcu.h"
#include "system/watchdog.h"
#include "sensors/atlas_rgb.h"
#include "utilities/qos.h"
#include "system/hardware.h"

// Setup and Loop

// void setupSensors(){

//   // read sensors types from EEPROM
//   // malloc configuration structs
//   // read configuration structs from EEPROM for each sensor type
//   // run setup for each sensor

  
//   // Setup RGB Sensor
//   AtlasRGB::instance()->setup(&WireTwo);

// }

void copyBytesToRegister(byte * registerPtr, byte msb, byte lsb){
  memcpy( registerPtr + 1, &msb, 1);
  memcpy( registerPtr, &lsb, 1);
}


void setup(void)
{
  startSerial2();

  startCustomWatchDog();
  printWatchDogStatus();


  // disable unused components and hardware pins //
  componentsAlwaysOff();
  //hardwarePinsAlwaysOff(); // TODO are we turning off I2C pins still, which is wrong

  setupSwitchedPower();

  enableSwitchedPower();

  setupHardwarePins();

  //blinkTest();

  // Set up the internal RTC
  RCC_BASE->APB1ENR |= RCC_APB1ENR_PWREN;
  RCC_BASE->APB1ENR |= RCC_APB1ENR_BKPEN;
  PWR_BASE->CR |= PWR_CR_DBP; // Disable backup domain write protection, so we can write


  allocateMeasurementValuesMemory();

  setupManualWakeInterrupts();

  powerUpSwitchableComponents();

  // Don't respond to interrupts during setup
  disableManualWakeInterrupt();
  clearManualWakeInterrupt();

  // Clear the alarms so they don't go off during setup
  clearAllAlarms();

  initializeFilesystem();

  //initBLE();

  readUniqueId(uuid);
  uuidString[2 * UUID_LENGTH] = '\0';
  for (short i = 0; i < UUID_LENGTH; i++)
  {
    sprintf(&uuidString[2 * i], "%02X", (byte)uuid[i]);
  }
  Serial2.println(uuidString);

  setNotBursting(); // prevents bursting during first loop
  setNotBurstLooping(); // prevent bursting during first loop

  /* We're ready to go! */
  Monitor::instance()->writeDebugMessage(F("done with setup"));
  Serial2.flush();

  // setupSensors();
  // Monitor::instance()->writeDebugMessage(F("done with sensor setup"));
  // Serial2.flush();


  disableCustomWatchDog();
  print_debug_status(); // delays for 10s with user message, don't want watchdog to trigger
  startCustomWatchDog();
}

/* main run loop order of operation: */
void loop(void)
{
  startCustomWatchDog();
  printWatchDogStatus();

  // Get reading from RGB Sensor
  // char * data = AtlasRGB::instance()->mallocDataMemory();
  // AtlasRGB::instance()->takeMeasurement(data);
  // free(data);
 
  checkMemory();

  // allocate and free the ram to test if there is enough?
  //nvic_sys_reset - what does this do?
  char debugBuffer[100];
  sprintf(debugBuffer, "burstLoopCount=%d, burstCount=%d, timestamp=%d", burstLoopCount,burstCount, (int)timestamp());
  Monitor::instance()->writeDebugMessage(F(debugBuffer));

  bool burstLooping = checkBurstLoop();
  bool bursting = checkBursting();
  bool debugLoop = checkDebugLoop();
  bool awakeForUserInteraction = checkAwakeForUserInteraction(debugLoop);
  bool takeMeasurement = checkTakeMeasurement(bursting, awakeForUserInteraction);

  // Should we sleep until a measurement is triggered?
  bool awaitMeasurementTrigger = false;
  if (!bursting && !awakeForUserInteraction)
  {
    Monitor::instance()->writeDebugMessage(F("Not bursting or awake"));
    awaitMeasurementTrigger = true;
  }

  if (awaitMeasurementTrigger) // Go to sleep
  {
    stopAndAwaitTrigger();
    return; // Go to top of loop
  }

  if (WaterBear_Control::ready(Serial2))
  {
    handleControlCommand();
    return;
  }

  if (WaterBear_Control::ready(getBLE()))
  {
    Monitor::instance()->writeDebugMessage(F("BLE Input Ready"));
    awakeTime = timestamp(); // Push awake time forward
    WaterBear_Control::processControlCommands(getBLE());
    return;
  }

  if (takeMeasurement)
  { 
    if (figMethane == true)
    {
      Serial2.println("turning on 5v boost");
      Serial2.flush();
      digitalWrite(GPIO_PIN_3, HIGH);
    }
    checkFirstBurst(bursting, awakeForUserInteraction);
    if (burstLooping && burstCount == 0) // delay once at the start of each new burst
    {
      Serial2.print("delay (min):");
      Serial2.println(burstDelay);
      Serial2.flush();
      if (figMethane == true)
      {
        warmup(burstDelay);
      }
    }
    takeNewMeasurement();
    trackBurst(bursting);
    trackBurstLoop(burstLooping);
    if (DEBUG_MEASUREMENTS)
    {
      monitorValues();
    }
  }

  if (configurationMode)
  {
    monitorConfiguration();
  }

  if (debugValuesMode)
  {
    if (burstCount == burstLength) // will cause loop() to continue until mode turned off
    {
      prepareForTriggeredMeasurement();
    }
    monitorValues();
  }

  if (tempCalMode)
  {
    monitorTemperature();
  }
}
