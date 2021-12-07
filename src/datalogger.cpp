#include "datalogger.h"
#include <Cmd.h>
#include "sensors/sensor.h"
#include "sensors/sensor_map.h"
#include "sensors/sensor_types.h"
#include "system/monitor.h"
#include "system/watchdog.h"
#include "sensors/atlas_rgb.h"
#include "sensors/temperature_analog.h"
#include "system/command.h"
#include "utilities/i2c.h"
#include "utilities/qos.h"

// Settings

// short fieldCount = 22; // number of fields to be logged to SDcard file

// Pin Mappings for Nucleo Board
// BLE USART
//#define D4 PB5
//int bluefruitModePin = D4;
//Adafruit_BluefruitLE_UART ble(Serial1, bluefruitModePin);

// Components
AD7091R * externalADC;

// State
WaterBear_FileSystem *filesystem;
unsigned char uuid[UUID_LENGTH];
char uuidString[25]; // 2 * UUID_LENGTH + 1

char lastDownloadDate[11] = "0000000000";
char **values;
unsigned long lastMillis = 0;

// bool configurationMode = false;
// bool debugValuesMode = false;
// bool clearModes = false;
// bool tempCalMode = false;
// bool tempCalibrated = false;
// short controlFlag = 0;

// static method to read configuration from EEPROM
void Datalogger::readConfiguration(datalogger_settings_type * settings)
{
  byte* buffer = (byte *) malloc(sizeof(byte) * sizeof(datalogger_settings_type));
  for(short i=0; i < sizeof(datalogger_settings_type); i++)
  {
    short address = EEPROM_DATALOGGER_CONFIGURATION_START + i;
    buffer[i] = readEEPROM(&Wire, EEPROM_I2C_ADDRESS, address);
  }

  memcpy(settings, buffer, sizeof(datalogger_settings_type));

  // apply defaults
  if(settings->burstNumber == 0 || settings->burstNumber > 20)
  {
    settings->burstNumber = 1;
  }
  if(settings->interBurstDelay > 300)
  {
    settings->interBurstDelay = 0;
  }
}

Datalogger::Datalogger(datalogger_settings_type * settings)
{
  powerCycle = true;
  Serial2.println("creating datalogger");
  Serial2.println("got mode");
  Serial2.println(settings->mode);

  // defaults
  if(settings->interval < 1) 
  {
    Serial2.println("Setting interval to 1 by default");
    settings->interval = 1;
  }

  memcpy(&this->settings, settings, sizeof(datalogger_settings_type));

  switch(settings->mode)
  {
    case 'i':
      changeMode(interactive);
      strcpy(loggingFolder, reinterpret_cast<const char *> F("INTERACTIVE"));
      break;
    case 'l':
      changeMode(logging);
      strcpy(loggingFolder, settings->siteName);
      break;
    default:
      changeMode(interactive);
      strcpy(loggingFolder, reinterpret_cast<const char *> F("NOT_DEPLOYED"));
      break;
  }

}


void Datalogger::setup()
{
  startCustomWatchDog();
  buildDriverSensorMap();
  loadSensorConfigurations();
  initializeFilesystem();
  setUpCLI();
}

void Datalogger::loop()
{

  if(inMode(deploy_on_trigger)){
    deploy();
    goto SLEEP;
    return;
  }

  if(inMode(logging))
  {
    if(powerCycle)
    {
      Monitor::instance()->writeDebugMessage("Powercycle");
      deploy();
      goto SLEEP;
    }

    if(shouldExitLoggingMode())
    {
      Monitor::instance()->writeSerialMessage("Should exit logging mode");
      changeMode(interactive);
      return;
    }

    if(shouldContinueBursting())
    {
      measureSensorValues();
      writeMeasurementToLogFile();
    } 
    else 
    {
      completedBursts++;
      if(completedBursts < settings.burstNumber)
      {
        notify(F("do another burst"));
        notify(settings.burstNumber);
        notify(completedBursts);
        delay(settings.interBurstDelay * 1000);
        initializeBurst();
        return;
      }

      // go to sleep
      SLEEP: stopAndAwaitTrigger();
      initializeMeasurementCycle();
      return;
    }
    return;
  }

  processCLI();

  // not currently used
  // TODO: do we want to cache dirty config to reduce writes to EEPROM?
  // if (configurationIsDirty())
  // {
  //   debug("Configuration is dirty");
  //   storeConfiguration();
  //   stopLogging();
  // }

  if (inMode(logging) || inMode(deploy_on_trigger))
  {
    // processCLI may have moved logger into a deployed mode
    goto SLEEP;
  }
  else if (inMode(interactive))
  {
    if(interactiveModeLogging){
      measureSensorValues(false);
      writeMeasurementToLogFile();
    }
  }
  else if (inMode(debugging))
  {
    measureSensorValues(false);
    writeMeasurementToLogFile();
    delay(5000); // this value could be configurable, also a step / read from CLI is possible
  }
  else
  {
    // invalid mode!
    Monitor::instance()->writeSerialMessage(F("Invalid Mode!"));
    Serial2.println(mode);
    mode = interactive;
    delay(1000);
  }

  powerCycle = false;
}

#define TOTAL_SLOTS 1

void Datalogger::loadSensorConfigurations()
{

  // load sensor configurations from EEPROM and count them
  sensorCount = 0;
  generic_config * configs[TOTAL_SLOTS];
  for(int i=0; i<TOTAL_SLOTS; i++)
  {
    debug("reading slot");
    generic_config * sensorConfig = (generic_config *) malloc(sizeof(generic_config));

    readEEPROMObject(EEPROM_DATALOGGER_SENSORS_START + i * EEPROM_DATALOGGER_SENSOR_SIZE, sensorConfig, EEPROM_DATALOGGER_SENSOR_SIZE);

    debug(sensorConfig->common.sensor_type);
    if(sensorConfig->common.sensor_type <= MAX_SENSOR_TYPE)
    {
      debug("found configured sensor");
      sensorCount++;
    }
    sensorConfig->common.slot = i;
    configs[i] = sensorConfig;
  }
  if(sensorCount == 0)
  {
    debug("no sensor configurations found");
  }

  // construct the drivers
  debug("construct drivers");
  drivers = (SensorDriver**) malloc(sizeof(SensorDriver*) * sensorCount);
  int j = 0;
  for(int i=0; i<TOTAL_SLOTS; i++)
  {
    if(configs[i]->common.sensor_type > MAX_SENSOR_TYPE)
    {
      debug("no sensor");
      continue;
    }

    debug("getting driver for sensor type");
    debug(configs[i]->common.sensor_type);
    SensorDriver * driver  = driverForSensorType(configs[i]->common.sensor_type);
    debug("got sensor driver");
    checkMemory();

    drivers[j] = driver;
    j++;
    switch(driver->getProtocol()){
      case analog:
        ((AnalogSensorDriver*) driver)->setup();
        break;
      case i2c:
        ((I2CSensorDriver*) driver)->setup(&WireTwo);
        break;
      default:
        break;
    }
    debug("configure sensor driver");
    driver->configure(configs[i]);  //pass configuration struct to the driver
    debug("configured sensor driver");
  }

  for(int i=0; i<TOTAL_SLOTS; i++)
  {
    free(configs[i]);
  }

  // set up bookkeeping for dirty configurations
  if(dirtyConfigurations != NULL)
  {
    free(dirtyConfigurations);
  }
  dirtyConfigurations = (bool *) malloc(sizeof(bool) * (sensorCount + 1));
}

void Datalogger::startLogging()
{
  interactiveModeLogging = true;
}

void Datalogger::stopLogging()
{
  interactiveModeLogging = false;
}


bool Datalogger::shouldExitLoggingMode()
{
  if( Serial2.peek() != -1){
    //attempt to process the command line
    for(int i=0; i<10; i++)
    {
      processCLI();
    }
    if(inMode(interactive))
    {
      return true;
    }
    else
    {
      return false;
    }
  }
  return false;
}

bool Datalogger::shouldContinueBursting()
{
  for(int i=0; i<sensorCount; i++)
  {
    notify(F("check sensor burst"));
    notify(i);
    if(!drivers[i]->burstCompleted())
    {
      return true;
    }
  }
  return false;
}

void Datalogger::initializeBurst()
{
  for(int i=0; i<sensorCount; i++)
  {
    drivers[i]->initializeBurst();
  }
}

void Datalogger::initializeMeasurementCycle()
{
  notify(F("setting base time"));
  currentEpoch = timestamp();
  offsetMillis = millis();

  initializeBurst();

  completedBursts = 0;

  notify(F("Waiting for start up delay"));
  delay(settings.startUpDelay);
  
}

void Datalogger::measureSensorValues(bool performingBurst)
{
  for(int i=0; i<sensorCount; i++){
    if(drivers[i]->takeMeasurement())
    {
      if(performingBurst) 
      {
        drivers[i]->incrementBurst();  // burst bookkeeping
      }
    }
  }
}

void Datalogger::writeStatusFieldsToLogFile()
{
  notify(F("Write status fields"));

  // Fetch and Log time from DS3231 RTC as epoch and human readable timestamps
  uint32 currentMillis = millis();
  double currentTime = (double)currentEpoch + (((double)currentMillis - offsetMillis) / 1000);

  char currentTimeString[20];
  char humanTimeString[20];
  sprintf(currentTimeString, "%10.3f", currentTime); // convert double value into string
  t_t2ts(currentTime, currentMillis-offsetMillis, humanTimeString);        // convert time_t value to human readable timestamp

  // TODO: only do this once
  char deploymentUUIDString[2*16 + 1];
  for (short i = 0; i < 16; i++)
  {
    sprintf(&deploymentUUIDString[2 * i], "%02X", (byte)settings.deploymentIdentifier[i]);
  }
  deploymentUUIDString[2*16] = '\0';

  filesystem->writeString(settings.siteName);
  filesystem->writeString((char *) ",");
  filesystem->writeString(deploymentUUIDString);
  filesystem->writeString((char *) ",");
  char buffer[10]; sprintf(buffer, "%ld,", settings.deploymentTimestamp); filesystem->writeString(buffer);  
  filesystem->writeString(uuidString);
  filesystem->writeString((char *)",");
  filesystem->writeString(currentTimeString);
  filesystem->writeString((char *)",");
  filesystem->writeString(humanTimeString);
  filesystem->writeString((char *)",");
  
}

void Datalogger::writeDebugFieldsToLogFile(){
  // notes and burst count, etc.
}

bool Datalogger::writeMeasurementToLogFile()
{

  writeStatusFieldsToLogFile();

  // write out the raw battery reading
  int batteryValue = analogRead(PB0);
  char buffer[50];
  sprintf(buffer, "%d,", batteryValue);
  filesystem->writeString(buffer);

  // and write out the sensor data
  notify(F("Write out sensor data"));
  for(int i=0; i<sensorCount; i++)
  {
    notify(i);
    // get values from the sensor
    char * dataString = drivers[i]->getDataString();
    filesystem->writeString(dataString);
    if(i < sensorCount - 1)
    {
      filesystem->writeString((char *)",");
    }
  }
  sprintf(buffer, ",%s,", userNote);
  filesystem->writeString(buffer);
  if(userValue != INT_MIN){
    sprintf(buffer, "%d", userValue);
    filesystem->writeString(buffer);
  }
  filesystem->endOfLine();
  return true;
}

void Datalogger::setUpCLI()
{
  cli = CommandInterface::create(Serial2, this);
  cli->setup();
}
void Datalogger::processCLI()
{
  cli->poll();
}

// not currently used
// bool Datalogger::configurationIsDirty()
// {
//   for(int i=0; i<sensorCount+1; i++)
//   {
//     if(dirtyConfigurations[i])
//     {
//       return true;
//     }
//   } 

//   return false;
// }

// not curretnly used
// void Datalogger::storeConfiguration()
// {
//   for(int i=0; i<sensorCount+1; i++)
//   {
//     if(dirtyConfigurations[i]){
//       //store this config block to EEPROM
//     }
//   }
// }

void Datalogger::getConfiguration(datalogger_settings_type * dataloggerSettings)
{
  memcpy(dataloggerSettings, &settings, sizeof(datalogger_settings_type));
}

void Datalogger::setSensorConfiguration(char * type, cJSON * json)
{
  if(strcmp(type, "generic_analog") == 0) // make generic for all types
  {
    SensorDriver * driver = new GenericAnalog();
    driver->configureFromJSON(json);
    generic_config configuration = driver->getConfiguration();
    storeSensorConfiguration(&configuration);

    debug(F("updating slots"));
    bool slotReplacement = false;
    debug(sensorCount);
    for(int i=0; i<sensorCount; i++){
      if(drivers[i]->getConfiguration().common.slot == driver->getConfiguration().common.slot)
      {
        slotReplacement = true;
        debug(F("slot replacement"));
        SensorDriver * replacedDriver = drivers[i];
        drivers[i] = driver;
        delete(replacedDriver);
        debug(F("OK"));
        break;
      }
    }
    if(!slotReplacement)
    {
      sensorCount = sensorCount + 1;
      SensorDriver ** driverAugmentation = (SensorDriver**) malloc(sizeof(SensorDriver*) * sensorCount);
      for(int i=0; i<sensorCount-2; i++)
      {
        driverAugmentation[i] = drivers[i];
      }
      driverAugmentation[sensorCount-1] = driver;
      free(drivers);
      drivers = driverAugmentation;
    }
    notify(F("OK"));

  }
}

void Datalogger::clearSlot(unsigned short slot)
{
  byte empty[SENSOR_CONFIGURATION_SIZE];
  for(int i=0; i<SENSOR_CONFIGURATION_SIZE; i++)
  {
    empty[i] = 0xFF;
  }
  writeSensorConfigurationToEEPROM(slot, empty);
  sensorCount--;

  SensorDriver ** updatedDrivers = (SensorDriver**) malloc(sizeof(SensorDriver*) * sensorCount);
  int j=0;
  for(int i=0; i<sensorCount+1; i++){
    generic_config configuration = this->drivers[i]->getConfiguration();
    if(configuration.common.slot != slot)
    {
      updatedDrivers[j] = this->drivers[i];
      j++;
    }
    else
    {
      delete(this->drivers[i]);
    }
  }
}


cJSON ** Datalogger::getSensorConfigurations() // returns unprotected **
{
  cJSON ** sensorConfigurationsJSON = (cJSON **) malloc(sizeof(cJSON *) * sensorCount);
  for(int i=0; i<sensorCount; i++)
  {
    sensorConfigurationsJSON[i] = drivers[i]->getConfigurationJSON();
  }
  return sensorConfigurationsJSON;
}


void Datalogger::setInterval(int interval)
{
  settings.interval = interval;
  storeDataloggerConfiguration();
}

void Datalogger::setBurstNumber(int number)
{
  settings.burstNumber = number;
  storeDataloggerConfiguration();
}

void Datalogger::setStartUpDelay(int delay)
{
  settings.startUpDelay = delay;
  storeDataloggerConfiguration();
}

void Datalogger::Datalogger::setIntraBurstDelay(int delay)
{
  settings.interBurstDelay = delay;
  storeDataloggerConfiguration();
}

void Datalogger::setUserNote(char * note)
{
  strcpy(userNote, note);
}

void Datalogger::setUserValue(int value)
{
  userValue = value;
}

SensorDriver * Datalogger::getDriver(unsigned short slot)
{
  for(int i=0; i<sensorCount; i++)
  {
    if(this->drivers[i]->getConfiguration().common.slot == slot)
    {
      return this->drivers[i];
    }
  }
}

void Datalogger::calibrate(unsigned short slot, char * subcommand, int arg_cnt, char ** args)
{
  SensorDriver * driver = getDriver(slot);
  if(strcmp(subcommand, "init") == 0)
  {
    driver->initCalibration();
  }
  else
  {
    notify(args[0]);
    driver->calibrationStep(subcommand, atoi(args[0]));
  }
}



void Datalogger::storeMode(mode_type mode)
{
  char modeStorage = 'i';
  switch(mode){
    case logging:
      modeStorage = 'l';
      break;
    case deploy_on_trigger:
      modeStorage = 't';
      break;
    default:
      modeStorage = 'i';
      break;
  }
  settings.mode = modeStorage;
  storeDataloggerConfiguration();
}

void Datalogger::changeMode(mode_type mode)
{
  char message[100];
  sprintf(message, reinterpret_cast<const char *> F("Moving to mode %d"), mode);
  Monitor::instance()->writeSerialMessage(message);
  this->mode = mode;
}

bool Datalogger::inMode(mode_type mode)
{
  return this->mode == mode;
}


void Datalogger::deploy()
{
  Monitor::instance()->writeSerialMessage(F("Deploying now!"));

  setDeploymentIdentifier();
  setDeploymentTimestamp(timestamp());
  strcpy(loggingFolder, settings.siteName);
  filesystem->closeFileSystem(); 
  initializeFilesystem();
  changeMode(logging);
  storeMode(logging);
  powerCycle = false; // not a powercycle loop
}




void Datalogger::initializeFilesystem()
{
  SdFile::dateTimeCallback(dateTime);

  filesystem = new WaterBear_FileSystem(loggingFolder, SD_ENABLE_PIN);
  Monitor::instance()->filesystem = filesystem;
  Monitor::instance()->Monitor::instance()->writeDebugMessage(F("Filesystem started OK"));

  time_t setupTime = timestamp();
  char setupTS[21];
  sprintf(setupTS, "unixtime: %lld", setupTime);
  Monitor::instance()->Monitor::instance()->writeSerialMessage(setupTS);


  char header[200];
  const char * statusFields = "site,deployment,deployed_at,uuid,time.s,time.h,battery.V";
  strcpy(header, statusFields);
  debug(header);
  for(int i=0; i<sensorCount; i++){
    debug(i);
    debug(drivers[i]->getCSVColumnNames());
    strcat(header, ",");
    strcat(header, drivers[i]->getCSVColumnNames());
  }
  strcat(header, ",user_note,user_value");

  filesystem->setNewDataFile(setupTime, header); // name file via epoch timestamps
}



void powerUpSwitchableComponents()  
{

  cycleSwitchablePower();
  delay(500);
  enableI2C1();

  delay(1); // delay > 50ns before applying ADC reset
  digitalWrite(PC5,LOW); // reset is active low
  delay(1); // delay > 10ns after starting ADC reset
  digitalWrite(PC5,HIGH);
  delay(100); // Wait for ADC to start up
  
  Monitor::instance()->writeDebugMessage(F("Set up external ADC"));
  externalADC = new AD7091R();
  externalADC->configure();
  externalADC->enableChannel(0);
  externalADC->enableChannel(1);
  externalADC->enableChannel(2);
  externalADC->enableChannel(3);

  if(USE_EC_OEM){
    enableI2C2();
    setupEC_OEM(&WireTwo);
  } else {
    Monitor::instance()->writeDebugMessage(F("Skipped EC_OEM"));
  }
  
  Monitor::instance()->writeDebugMessage(F("Switchable components powered up"));

}

void powerDownSwitchableComponents() // called in stopAndAwaitTrigger
{
  if(USE_EC_OEM){
    hibernateEC_OEM();
    i2c_disable(I2C2);
    Monitor::instance()->writeDebugMessage(F("Switchable components powered down"));
  }
}

void startSerial2()
{
  // Start up Serial2
  // TODO: Need to do an if(Serial2) after an amount of time, just disable it
  Serial2.begin(SERIAL_BAUD);
  while (!Serial2)
  {
    delay(100);
  }
  Monitor::instance()->writeSerialMessage(F("Begin Setup"));
}

void setupHardwarePins()
{
  Monitor::instance()->writeDebugMessage(F("setting up hardware pins"));
  //pinMode(BLE_COMMAND_MODE_PIN, OUTPUT); // Command Mode pin for BLE
  
  pinMode(INTERRUPT_LINE_7_PIN, INPUT_PULLUP); // This the interrupt line 7
  pinMode(ANALOG_INPUT_1_PIN, INPUT_ANALOG);
  pinMode(ANALOG_INPUT_2_PIN, INPUT_ANALOG);
  pinMode(ANALOG_INPUT_3_PIN, INPUT_ANALOG);
  pinMode(ANALOG_INPUT_4_PIN, INPUT_ANALOG);
  pinMode(ANALOG_INPUT_5_PIN, INPUT_ANALOG);
  pinMode(ONBOARD_LED_PIN, OUTPUT); // This is the onboard LED ? Turns out this is also the SPI1 clock.  niiiiice.

  // pinMode(PA4, INPUT_PULLDOWN); // mosfet for battery measurement - should be OUTPUT ??

  // redundant?
  //pinMode(PA2, OUTPUT); // USART2_TX/ADC12_IN2/TIM2_CH3
  //pinMode(PA3, INPUT); // USART2_RX/ADC12_IN3/TIM2_CH4

  pinMode(PC5, OUTPUT); // external ADC reset
  digitalWrite(PC5, HIGH);
}

void blinkTest()
{
  //Logger::instance()->writeDebugMessage(F("blink test:"));
  //blink(10,250);
}

void Datalogger::prepareForUserInteraction()
{
  char humanTime[26];
  time_t awakenedTime = timestamp();

  t_t2ts(awakenedTime, millis(), humanTime);
  Monitor::instance()->writeDebugMessage(F("Awakened by user"));
  Monitor::instance()->writeDebugMessage(F(humanTime));

  awakenedByUser = false;
  awakeTime = awakenedTime;
}

// void Datalogger::captureInternalADCValues()
// {
//   // Measure the data on internal ADC pins
//   short sensorCount = 6;
//   short sensorPins[6] = {PB0, PB1, PC0, PC1, PC2, PC3};
//   for (short i = 0; i < sensorCount; i++)
//   {
//     int value = analogRead(sensorPins[i]);
//     sprintf(values[4 + i], "%4d", value);
//   }

//   // Measure and log temperature data and calibration info -> move to seperate function?
//   unsigned int uiData = 0;
//   unsigned short usData = 0;

//   readEEPROMBytes(TEMPERATURE_TIMESTAMP_ADDRESS_START, (unsigned char *)&uiData, TEMPERATURE_TIMESTAMP_ADDRESS_LENGTH);
//   sprintf(values[11], "%i", uiData);
//   readEEPROMBytes(TEMPERATURE_C1_ADDRESS_START, (unsigned char *)&usData, TEMPERATURE_C1_ADDRESS_LENGTH);
//   sprintf(values[12], "%i", usData);
//   readEEPROMBytes(TEMPERATURE_V1_ADDRESS_START, (unsigned char *)&usData, TEMPERATURE_V1_ADDRESS_LENGTH);
//   sprintf(values[13], "%i", usData);
//   readEEPROMBytes(TEMPERATURE_C2_ADDRESS_START, (unsigned char *)&usData, TEMPERATURE_C2_ADDRESS_LENGTH);
//   sprintf(values[14], "%i", usData);
//   readEEPROMBytes(TEMPERATURE_V2_ADDRESS_START, (unsigned char *)&usData, TEMPERATURE_V2_ADDRESS_LENGTH);
//   sprintf(values[15], "%i", usData);
//   readEEPROMBytes(TEMPERATURE_M_ADDRESS_START, (unsigned char *)&usData, TEMPERATURE_M_ADDRESS_LENGTH);
//   sprintf(values[16], "%i", usData);
//   readEEPROMBytes(TEMPERATURE_B_ADDRESS_START, (unsigned char *)&uiData, TEMPERATURE_B_ADDRESS_LENGTH);
//   sprintf(values[17], "%i", uiData);
//   sprintf(values[18], "%.2f", calculateTemperature());
// }

void captureExternalADCValues(){
  debug("captureExternalADCValues");
  externalADC->convertEnabledChannels();
  Serial2.println("channel 0,1,2,3 value");
  Serial2.println(externalADC->channel0Value());
  Serial2.println(externalADC->channel1Value());
  Serial2.println(externalADC->channel2Value());
  Serial2.println(externalADC->channel3Value());
  Serial2.flush();

  sprintf(values[9], "%d", externalADC->channel0Value()); // stuff ADC0 into values[9] for the moment.
}


bool checkDebugLoop()
{
  // Debug debugLoop
  // this should be a jumper
  bool debugLoop = false;
  if (debugLoop == false)
  {
    debugLoop = DEBUG_LOOP;
  }
  return debugLoop;
}

bool checkThermistorCalibration()
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

// bool Datalogger::checkAwakeForUserInteraction(bool debugLoop)
// {
//   // Are we awake for user interaction?
//   bool awakeForUserInteraction = false;
//   if (timestamp() < awakeTime + USER_WAKE_TIMEOUT)
//   {
//     Monitor::instance()->writeDebugMessage(F("Awake for user interaction"));
//     awakeForUserInteraction = true;
//   }
//   else
//   {
//     if (!debugLoop)
//     {
//       Monitor::instance()->writeDebugMessage(F("Not awake for user interaction"));
//     }
//   }
//   if (!awakeForUserInteraction)
//   {
//     awakeForUserInteraction = debugLoop;
//   }
//   return awakeForUserInteraction;
// }

bool checkTakeMeasurement(bool bursting, bool awakeForUserInteraction)
{
  // See if we should send a measurement to an interactive user
  // or take a bursting measurement
  bool takeMeasurement = false;
  if (bursting)
  {
    takeMeasurement = true;
  }
  else if (awakeForUserInteraction)
  {
    unsigned long currentMillis = millis();
    unsigned int interactiveMeasurementDelay = 1000;
    if (currentMillis - lastMillis >= interactiveMeasurementDelay)
    {
      lastMillis = currentMillis;
      takeMeasurement = true;
    }
  }
  return takeMeasurement;
}

void Datalogger::stopAndAwaitTrigger()
{
  Monitor::instance()->writeDebugMessage(F("Await measurement trigger"));

  if (Clock.checkIfAlarm(1))
  {
    Monitor::instance()->writeDebugMessage(F("Alarm 1"));
  }

  printInterruptStatus(Serial2);
  Monitor::instance()->writeDebugMessage(F("Going to sleep"));

  // save enabled interrupts
  int iser1, iser2, iser3;
  storeAllInterrupts(iser1, iser2, iser3);

  clearManualWakeInterrupt();
  setNextAlarmInternalRTC(settings.interval); 


  powerDownSwitchableComponents();
  filesystem->closeFileSystem(); // close file, filesystem
  disableSwitchedPower();

  awakenedByUser = false; // Don't go into sleep mode with any interrupt state


  componentsStopMode();

  disableCustomWatchDog();
  Monitor::instance()->writeDebugMessage(F("disabled watchdog"));
  disableSerialLog(); // TODO
  hardwarePinsStopMode(); // switch to input mode
  
  clearAllInterrupts();
  clearAllPendingInterrupts();

  enableManualWakeInterrupt(); // The button, which is not powered during stop mode on v0.2 hardware
  nvic_irq_enable(NVIC_RTCALARM); // enable our RTC alarm interrupt

  enterStopMode();
  //enterSleepMode();

  reenableAllInterrupts(iser1, iser2, iser3);
  disableManualWakeInterrupt();
  nvic_irq_disable(NVIC_RTCALARM);  

  enableSerialLog(); 
  enableSwitchedPower();
  setupHardwarePins(); // used from setup steps in datalogger

  Monitor::instance()->writeDebugMessage(F("Awakened by interrupt"));

  startCustomWatchDog(); // could go earlier once working reliably
  // delay( (WATCHDOG_TIMEOUT_SECONDS + 5) * 1000); // to test the watchdog


  if(awakenedByUser == true){

    Monitor::instance()->writeDebugMessage(F("USER TRIGGERED INTERRUPT"));
  }

 

  // We have woken from the interrupt
  // printInterruptStatus(Serial2);

  powerUpSwitchableComponents();
   /////turn components back on
  componentsBurstMode();
  filesystem->reopenFileSystem();


  if(awakenedByUser == true)
  {
    awakeTime = timestamp();
  }

  // We need to check on which interrupt was triggered
  if (awakenedByUser)
  {
    prepareForUserInteraction();
  }
 
}

void handleControlCommand()
{
  // Monitor::instance()->writeDebugMessage(F("SERIAL2 Input Ready"));
  // awakeTime = timestamp(); // Push awake time forward
  // int command = WaterBear_Control::processControlCommands(Serial2);
  
  // Monitor::instance()->writeDebugMessage(command);
  // switch (command)
  // {
  // case WT_CLEAR_MODES:
  // {
  //   Monitor::instance()->writeDebugMessage(F("Clearing Config, Debug, & TempCal modes"));
  //   configurationMode = false;
  //   debugValuesMode = false;
  //   tempCalMode = false;
  //   controlFlag = 0;
  //   break;
  // }
  // case WT_CONTROL_CONFIG:
  // {
  //   Monitor::instance()->writeDebugMessage(F("Entering Configuration Mode"));
  //   Monitor::instance()->writeDebugMessage(F("Reset device to enter normal operating mode"));
  //   Monitor::instance()->writeDebugMessage(F("Or >WT_CLEAR_MODES<"));
  //   configurationMode = true;
  //   char *flagPtr = (char *)WaterBear_Control::getLastPayload();
  //   char logMessage[30];
  //   sprintf(&logMessage[0], "%s%s", reinterpret_cast<const char *> F("ConfigMode: "), flagPtr);
  //   Monitor::instance()->writeDebugMessage(logMessage);
  //   processControlFlag(flagPtr);
  //   break;
  // }
  // case WT_DEBUG_VAlUES:
  // {
  //   Monitor::instance()->writeDebugMessage(F("Entering Value Debug Mode"));
  //   Monitor::instance()->writeDebugMessage(F("Reset device to enter normal operating mode"));
  //   Monitor::instance()->writeDebugMessage(F("Or >WT_CLEAR_MODES<"));
  //   debugValuesMode = true;
  //   break;
  // }
  // case WT_CONTROL_CAL_DRY:
  //   Monitor::instance()->writeDebugMessage(F("DRY_CALIBRATION"));
  //   clearECCalibrationData();
  //   setECDryPointCalibration();
  //   break;
  // case WT_CONTROL_CAL_LOW:
  // {
  //   Monitor::instance()->writeDebugMessage(F("LOW_POINT_CALIBRATION"));
  //   int *lowPointPtr = (int *)WaterBear_Control::getLastPayload();
  //   int lowPoint = *lowPointPtr;
  //   char logMessage[30];
  //   sprintf(&logMessage[0], "%s%i", reinterpret_cast<const char *> F("LOW_POINT_CALIBRATION: "), lowPoint);
  //   Monitor::instance()->writeDebugMessage(logMessage);
  //   setECLowPointCalibration(lowPoint);
  //   break;
  // }
  // case WT_CONTROL_CAL_HIGH:
  // {
  //   Monitor::instance()->writeDebugMessage(F("HIGH_POINT_CALIBRATION"));
  //   int *highPointPtr = (int *)WaterBear_Control::getLastPayload();
  //   int highPoint = *highPointPtr;
  //   char logMessage[31];
  //   sprintf(&logMessage[0], "%s%i", reinterpret_cast<const char *> F("HIGH_POINT_CALIBRATION: "), highPoint);
  //   setECHighPointCalibration(highPoint);
  //   break;
  // }
  // case WT_SET_RTC: // DS3231
  // {
  //   Monitor::instance()->writeDebugMessage(F("SET_RTC"));
  //   time_t *RTCPtr = (time_t *)WaterBear_Control::getLastPayload();
  //   time_t RTC = *RTCPtr;
  //   char logMessage[24];
  //   sprintf(&logMessage[0], "%s%lld", reinterpret_cast<const char *> F("SET_RTC_TO: "), RTC);
  //   setTime(RTC);
  //   break;
  // }
  // case WT_DEPLOY: // Set deployment identifier via serial
  // {
  //   Monitor::instance()->writeDebugMessage(F("SET_DEPLOYMENT_IDENTIFIER"));
  //   char *deployPtr = (char *)WaterBear_Control::getLastPayload();
  //   char logMessage[46];
  //   sprintf(&logMessage[0], "%s%s", reinterpret_cast<const char *> F("SET_DEPLOYMENT_TO: "), deployPtr);
  //   Monitor::instance()->writeDebugMessage(logMessage);
  //   writeDeploymentIdentifier(deployPtr);
  //   break;
  // }
  // case WT_CAL_TEMP: // display raw temperature readings, and calibrated if available
  // {
  //   Monitor::instance()->writeDebugMessage(F("Entering Temperature Calibration Mode"));
  //   Monitor::instance()->writeDebugMessage(F("Reset device to enter normal operating mode"));
  //   Monitor::instance()->writeDebugMessage(F("Or >WT_CLEAR_MODES<"));
  //   tempCalMode = true;
  //   break;
  // }
  // case WT_TEMP_CAL_LOW:
  // {
  //   clearThermistorCalibration();
  //   Monitor::instance()->writeDebugMessage(F("LOW_TEMP_CALIBRATION")); // input in xxx.xxC
  //   unsigned short *lowTempPtr = (unsigned short *)WaterBear_Control::getLastPayload();
  //   unsigned short lowTemp = *lowTempPtr;
  //   char logMessage[30];
  //   sprintf(&logMessage[0], "%s%i", reinterpret_cast<const char *> F("LOW_TEMP_CAL: "), lowTemp);
  //   Monitor::instance()->writeDebugMessage(logMessage);
  //   writeEEPROMBytes(TEMPERATURE_C1_ADDRESS_START, (unsigned char*)&lowTemp, TEMPERATURE_C1_ADDRESS_LENGTH);

  //   unsigned short voltage = analogRead(PB1);
  //   sprintf(&logMessage[0], "%s%i", reinterpret_cast<const char *> F("LOW_TEMP_VOLTAGE: "), voltage);
  //   Monitor::instance()->writeDebugMessage(logMessage);
  //   writeEEPROMBytes(TEMPERATURE_V1_ADDRESS_START, (unsigned char*)&voltage, TEMPERATURE_V1_ADDRESS_LENGTH);
  //   break;
  // }
  // case WT_TEMP_CAL_HIGH:
  // {
  //   Monitor::instance()->writeDebugMessage(F("HIGH_TEMP_CALIBRATION")); // input in xxx.xxC
  //   unsigned short *highTempPtr = (unsigned short *)WaterBear_Control::getLastPayload();
  //   unsigned short highTemp = *highTempPtr;
  //   char logMessage[30];
  //   sprintf(&logMessage[0], "%s%i", reinterpret_cast<const char *> F("HIGH_TEMP_CAL: "), highTemp);
  //   Monitor::instance()->writeDebugMessage(logMessage);
  //   writeEEPROMBytes(TEMPERATURE_C2_ADDRESS_START, (unsigned char*)&highTemp, TEMPERATURE_C2_ADDRESS_LENGTH);

  //   unsigned short voltage = analogRead(PB1);
  //   sprintf(&logMessage[0], "%s%i", reinterpret_cast<const char *> F("HIGH_TEMP_VOLTAGE: "), voltage);
  //   Monitor::instance()->writeDebugMessage(logMessage);
  //   writeEEPROMBytes(TEMPERATURE_V2_ADDRESS_START, (unsigned char*)&voltage, TEMPERATURE_V2_ADDRESS_LENGTH);
  //   calibrateThermistor();
  //   break;
  // }
  // case WT_USER_VALUE:
  // {
  //   Monitor::instance()->writeDebugMessage(F("USER_VALUE"));
  //   char *userValuePtr = (char *)WaterBear_Control::getLastPayload();
  //   char logMessage[24];
  //   sprintf(&logMessage[0], "%s%s", reinterpret_cast<const char *> F("USER_VALUE: "), userValuePtr);
  //   Monitor::instance()->writeDebugMessage(logMessage);
  //   sprintf(values[20], "%s", userValuePtr);
  //   break;
  // }
  // case WT_USER_NOTE:
  // {
  //   Monitor::instance()->writeDebugMessage(F("USER_NOTE"));
  //   char *userNotePtr = (char *)WaterBear_Control::getLastPayload();
  //   char logMessage[42];
  //   sprintf(&logMessage[0], "%s%s", reinterpret_cast<const char *> F("USER_NOTE: "), userNotePtr);
  //   Monitor::instance()->writeDebugMessage(logMessage);
  //   sprintf(values[21], "%s", userNotePtr);
  //   break;
  // }
  // case WT_USER_INPUT:
  // {
  //   Monitor::instance()->writeDebugMessage(F("USER_INPUT"));
  //   char *userInputPtr = (char *)WaterBear_Control::getLastPayload();
  //   char logMessage[55];
  //   sprintf(&logMessage[0], "%s%s", reinterpret_cast<const char *> F("USER_INPUT: "), userInputPtr);
  //   Monitor::instance()->writeDebugMessage(logMessage);
  //   for (size_t i = 0; i < 42 ; i++)
  //   {
  //     if (userInputPtr[i] == '&')
  //     {
  //       sprintf(values[21], "%s", &userInputPtr[i+1]);
  //       userInputPtr[i] = '\0';
  //       sprintf(values[20], "%s", userInputPtr);
  //       break;
  //     }
  //     else if (userInputPtr[i] == '\0')
  //     {
  //       Monitor::instance()->writeDebugMessage(F("incorrect format, delimiter is &"));
  //       break;
  //     }
  //   }
  //   break;
  // }
  // default:
  //   Monitor::instance()->writeDebugMessage(F("Invalid command code"));
  //   break;
  // }
}

void clearThermistorCalibration()
{
  Monitor::instance()->writeDebugMessage(F("clearing thermistor EEPROM registers"));
  for (size_t i = 0; i < TEMPERATURE_BLOCK_LENGTH; i++)
  {
    writeEEPROM(&Wire, EEPROM_I2C_ADDRESS, TEMPERATURE_C1_ADDRESS_START+i, 255);
  }
}


// void Datalogger::takeNewMeasurement()
// {
//   if (DEBUG_MEASUREMENTS)
//   {
//     Monitor::instance()->writeDebugMessage(F("Taking new measurement"));
//   }
//   writeStatusFields();

//   captureInternalADCValues();

//   // OEM EC
//   float ecValue = -1;
//   if(USE_EC_OEM){
//     bool newDataAvailable = readECDataIfAvailable(&ecValue);
//     if (!newDataAvailable)
//     {
//       Monitor::instance()->writeDebugMessage(F("New EC data not available"));
//     }
//   }


//   // Get reading from RGB Sensor
//   // char * data = AtlasRGB::instance()->mallocDataMemory();
//   // AtlasRGB::instance()->takeMeasurement(data);
//   // free(data);

//   captureExternalADCValues();

//   //Serial2.print(F("Got EC value: "));
//   //Serial2.print(ecValue);
//   //Serial2.println();
//   sprintf(values[10], "%4f", ecValue); // stuff EC value into values[10] for the moment.

//   sprintf(values[19], "%i", burstCount); // log burstCount


//   Monitor::instance()->writeDebugMessage(F("writeLog"));
//   filesystem->writeLog(values, fieldCount);
//   Monitor::instance()->writeDebugMessage(F("writeLog done"));
  
// }

// displays relevant readings based on controlFlag
void monitorConfiguration()
{
  blink(1,500); //slow down rate of responses to 1/s
  if (controlFlag == 0)
  {
    Monitor::instance()->writeDebugMessage(F("Error: no control Flag"));
  }
  if (controlFlag == 1) // time stamps
  {
    printDS3231Time();
  }
  if (controlFlag == 2) // conductivity readings
  {
    float ecValue = -1;
    bool newDataAvailable = readECDataIfAvailable(&ecValue);
    if (newDataAvailable)
    {
      char message[100];
      sprintf(message, "Got EC value: %f", ecValue);
      Monitor::instance()->writeDebugMessage(message);
    }
  }
  if (controlFlag == 3) // thermistor readings
  {
    char valuesBuffer[35];
    sprintf(valuesBuffer, "raw voltage: %i", analogRead(PB1));
    Monitor::instance()->writeDebugMessage(valuesBuffer);
  }
  //test code simplified calls to write and read eeprom
  /*
  int test = 1337;
  writeExposedBytes(TEST_START, (unsigned char *)&test, TEST_LENGTH);

  unsigned short read = 0;
  readExposedBytes(TEST_START,(unsigned char *)&read, TEST_LENGTH);
  */
}

void processControlFlag(char *flag)
{
  if (strcmp(flag, "time") == 0)
  {
    controlFlag = 1;
  }
  else if(strcmp(flag, "conduct") == 0)
  {
    controlFlag = 2;
  }
  else if(strcmp(flag, "therm") == 0)
  {
    controlFlag = 3;
  }
  else
  {
    controlFlag = 0;
  }
}

void monitorValues()
{
  // print content being logged each second
  blink(1, 500);
  char valuesBuffer[300]; // 51+25+11+24+(7*5)+33
  sprintf(valuesBuffer, ">WT_VALUES: %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s<",
      values[0], values[1], values[2], values[3], values[4], values[5],
      values[6],values[7], values[8], values[9], values[10], values[20], values[21]);
  Monitor::instance()->writeDebugMessage(F(valuesBuffer));

  //sprintf(valuesBuffer, "burstcount = %i current millis = %i\n", burstCount, (int)millis());
  //Monitor::instance()->writeDebugMessage(F(valuesBuffer));

  printToBLE(valuesBuffer);
}

void monitorTemperature() // print out calibration information & current readings
{
  blink(1,500);
  unsigned short c1, v1, c2, v2, m;
  unsigned int b;
  unsigned int calTime;
  unsigned char data[4];
  unsigned char * dataPtr = data;

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

  char valuesBuffer[150];
  sprintf(valuesBuffer,"EEPROM thermistor block\n(%i,%i)(%i,%i)\nv=%ic+%i\ncalTime:%i\ntemperature:%.2fC\n", c1, v1, c2, v2, m, b, calTime, temperature);
  Monitor::instance()->writeDebugMessage(F(valuesBuffer));
}


void Datalogger::storeDataloggerConfiguration()
{
  writeDataloggerSettingsToEEPROM(&this->settings);
}

void Datalogger::storeSensorConfiguration(generic_config * configuration){
  notify(F("Storing sensor configuration"));
  // notify(configuration->common.slot);
  // notify(configuration->common.sensor_type);
  writeSensorConfigurationToEEPROM(configuration->common.slot, configuration);



}

void Datalogger::setSiteName(char * siteName)
{
  strcpy(this->settings.siteName, siteName);
  storeDataloggerConfiguration();
}

void Datalogger::setDeploymentIdentifier()
{
  byte uuidNumber[16];
  // TODO need to generate this UUID number
  // https://www.cryptosys.net/pki/Uuid.c.html
  memcpy(this->settings.deploymentIdentifier, uuidNumber, 16);
  storeDataloggerConfiguration();
}

void Datalogger::setDeploymentTimestamp(int timestamp)
{
  this->settings.deploymentTimestamp = timestamp;
}
