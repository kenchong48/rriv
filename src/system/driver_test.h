#ifndef WATERBEAR_DRIVER_TEST
#define WATERBEAR_DRIVER_TEST

/*
 * EEPROM Partition scheme (24LC01B 1Kbit)
 * 000-359 Waterbear Device Info, Calibration, Significant Values
 * 360-423 Sensor Calibration Block 1
 * 424-487 Sensor Calibration Block 2
 * 488-551 Sensor Calibration Block 3
 * 552-615 Sensor Calibration Block 4
 * 616-679 Sensor Calibration Block 5
 * 680-743 Sensor Calibration Block 6
 * 744-807 Sensor Calibration Block 7
 * 808-873 Sensor Calibration Block 8
 * 872-936 Sensor Calibration Block 9
 * 936-999 Sensor Calibration Block 10
*/

#define SENSOR_SLOT_1 360
#define SENSOR_SLOT_2 424
#define SENSOR_SLOT_3 488
#define SENSOR_SLOT_4 552
#define SENSOR_SLOT_5 616
#define SENSOR_SLOT_6 680
#define SENSOR_SLOT_7 744
#define SENSOR_SLOT_8 808
#define SENSOR_SLOT_9 872
#define SENSOR_SLOT_10 936

#define SENSOR_TYPE_LENGTH 2

#define NTC_10K_THERMISTOR 1
#define AS_CONDUCTIVITY 2
#define AS_RGB 3
#define AS_CO2 4
#define FIG_METHANE 5
#define AF_GPS 6

typedef struct common_config{
    // note needs to be 32 bytes total (multiple of 4)
    // rearrange in blocks of 4bytes for diagram
    // sensor.h
    short sensor_type; // 2 bytes
    char slot; // 1 byte
    char column_prefix[5]; // 5 bytes
    char sensor_burst; // 1 byte
    unsigned short int warmup; // 2 bytes, in seconds? (65535/60=1092)
    char tag[4]; // 4 bytes

    char padding[17]; // 17bytes
}common_config_sensor;

//split into drivers
typedef struct thermistor_type{
    // 10k ohm NTC 3950b thermistor
    common_config_sensor common;
    char calibrated; // 1 byte => bit mask
    char sensor_port; // 1 byte => add into bit mask (4bits)
    // generalize for a generic linear calibrated sensor...? (x,y)
    unsigned short m; // 2bytes, slope
    int b; // 4bytes, y-intercept
    unsigned int cal_timestamp; // 4byte epoch timestamp at calibration
    short int c1; // 2bytes for 2pt calibration
    short int v1; // 2bytes for 2pt calibration
    short int c2; // 2bytes for 2pt calibration
    short int v2; // 2bytes for 2pt calibration

    char padding[13];
}thermistor_sensor;

typedef struct fig_e13_methane_type{
    // Figaro NGM2611-E13 Methane Sensor Module
    common_config_sensor common;

    char padding[32];
}fig_methane_sensor;

typedef struct as_mini_conductivity_k1_type{
    //Atlas Scientific Mini Conductivity Probe K 1.0
    common_config_sensor common;

    char padding[32];
}as_conductivity_sensor;

typedef struct as_rgb_type{
    //Atlas Scientific EZO-RGB Embedded Color Sensor
    common_config_sensor common;

    char padding[32];
}as_rbg_sensor;

typedef struct as_co2_type{
    //Atlas Scientific EZO-CO2 Embedded NDIR Carbon Dioxide Sensor
    common_config_sensor common;

    char padding[32];
}as_co2_sensor;

typedef struct af_gps_type{
    //Adafruit GPS - model?
    common_config_sensor common;

    char padding[32];
}gps_sensor;

// void * for reading struct and sensor type based on first 2 bytes of structs

void init_struct(char sensor_slot); //populate struct with defaults based on sensor type
void new_config(short sensor_slot); // malloc an empty config struct based on sensor type?
void get_sensor_type(char sensor_slot); // return sensor type from eeprom


#endif