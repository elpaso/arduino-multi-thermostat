/**
*
* Thermostat
*
*/

// Needed for prog_char PROGMEM
//#include <avr/pgmspace.h>

/******************************************************
*
* Main constants, all times in millis
*/

#define DEBUG 0

// Number of rooms
#define ROOMS 2

// Pins
#define BTN_PIN A0 // analog button ladder
#define LCD_PINS 7, 6, 5, 4, 3, 2
#define ONE_WIRE_PIN 8
#define PUMP_PIN 9

// TODO: move to config

#define TEMP_READ_INTERVAL 4000
#define VALVE_OPENING_TIME 120000 // 2 minutes
#define BLOCKED_TIME 3600000
#define RISE_TEMP_TIME 600000
#define RISE_TEMP_DELTA 0.5
#define IDLE_TIME 10000
#define HYSTERESIS 0.5

// Room status
#define OPENING 'V'
#define CLOSED 'C'
#define OPEN 'O'
#define BLOCKED 'B'


#include "Timer.h"

Timer t;

byte room_led_timers[ROOMS];


#include <Wire.h>

#include "RTClib.h"

RTC_DS1307 RTC;
DateTime now;


/** *****************************************************
*
* LCD & menu part
*
*/

// already included before... #include <Wire.h>
#include <LCD.h>
#include <LiquidCrystal.h>
#include <buttons.h>
#include <MENWIZ.h>
#include <EEPROM.h>


#define DEBOUNCE 300

LiquidCrystal lcd(LCD_PINS);

menwiz tree;

/**
 * Buttons processing
 */
int read_button(){
    static unsigned long btn_time = 0;
    float voltage = (float) analogRead(BTN_PIN) / 1023 * 5;

    if(voltage <= 1.80 || voltage > 4.20 || (millis() - btn_time) < DEBOUNCE){
        return MW_BTNULL;
    }

    btn_time = millis();

    if(voltage > 4.00){
        return MW_BTU;
    }

    if(voltage > 3.20){
        return MW_BTD;
    }

    if(voltage > 2.90){
        return MW_BTL;
    }

    if(voltage > 2.60){
        return MW_BTR;
    }

    if(voltage > 2.20){
        return MW_BTE;
    }

    if(voltage > 1.80){
        return MW_BTC;
    }
}


void myfunc(){
   Serial.println("ACTION FIRED");
  }


/** ************************************************
*
* DS18B20 part
*
*/

#include <OneWire.h>
#include <DallasTemperature.h>

// Data wire is plugged into pin 6 on the Arduino
#define ONE_WIRE_BUS ONE_WIRE_PIN

// Setup a oneWire instance to communicate with any OneWire devices
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);

// Assign the addresses of your 1-Wire temp sensors.
// See the tutorial on how to obtain these addresses:
// http://www.hacktronics.com/Tutorials/arduino-1-wire-address-finder.html


/** *************************************
 *
 * Programs
 *
 */

// Global time (minutes from 0)
unsigned int this_time;
byte this_weekday;

// Global OFF
byte off = 0;
byte pump_open = 0;

// Temperatures
// TODO: configurable
float T[] = {5, 15, 18, 28};

// Programs
// 8 slots    6:30  8:00 12:00 13:00 16:00 20:00 22:00
unsigned int slot[] = { 390,  480,  720,  780,  960, 1200, 1320 };
// 6 programs, T level for each slot/pgm tuple
byte daily_program[][8] = {
    //0:00 6:30  8:00 12:00 13:00 16:00 20:00 22:00
    {    0,   0,    0,    0,    0,    0,    0,    0 }, // all T0
    {    1,   1,    1,    1,    1,    1,    1,    1 }, // all T1
    {    2,   2,    2,    2,    2,    2,    2,    2 }, // all T2
    {    3,   3,    3,    3,    3,    3,    3,    3 }, // all T3
    {    1,   3,    1,    1,    1,    3,    2,    1 }, // awakening supper and evening 4
    {    1,   3,    1,    3,    1,    3,    2,    1 },  // awakening, meals and evening 5
    {    1,   3,    1,    3,    3,    3,    2,    1 },  // awakening, meals, afternoon and evening 6
    {    1,   3,    3,    3,    3,    3,    2,    1 },  // all day 7
};

// Weekly programs, 0 is monday
byte weekly_program[][7] = {
    //  Mo Tu Th We Fr Sa Su
        {0, 0, 0, 0, 0, 0, 0}, // always off
        {1, 1, 1, 1, 1, 1, 1}, // Always 1
        {2, 2, 2, 2, 2, 2, 2}, // Always 2
        {3, 3, 3, 3, 3, 3, 3}, // Always 3
        {4, 4, 4, 4, 4, 7, 7}, // 4 (5+2)
        {4, 4, 4, 4, 4, 4, 7}, // 4 (6+1)
        {5, 5, 5, 5, 5, 7, 7}, // 5 (5+2)
        {5, 5, 5, 5, 5, 5, 7}, // 5 (6+1)
        {6, 6, 6, 6, 6, 7, 7}, // 6 (5+2)
        {6, 6, 6, 6, 6, 6, 7} // 6 (6+1)
};


// Array of rooms
struct room_t {
  int name;
  DeviceAddress address;
  byte pin;
  byte program;
  char status;
  byte last_status;
  float temperature;
  float old_temperature;
  unsigned long last_status_change;
} rooms[ROOMS] = {
    {1, { 0x28, 0xAD, 0x4C, 0xC4, 0x03, 0x00, 0x00, 0x13},  10, 3},
    {2, { 0x28, 0x6C, 0x41, 0xC4, 0x03, 0x00, 0x00, 0x57}, 11, 8, 2}
};


/**
 * Read temps
 */
void read_temperatures(){
#if DEBUG
    Serial.println("Reading t...");
#endif
  sensors.requestTemperatures();
  for (int i=0; i<ROOMS; i++){
    float tempC = sensors.getTempC(rooms[i].address);
    if (tempC == -127.00) {
#if DEBUG
       Serial.println("Error getting temperature");
#endif
    } else {
       rooms[i].old_temperature = rooms[i].temperature;
       rooms[i].temperature = tempC;
#if DEBUG
       Serial.print("Temp R");
       Serial.print(i);
       Serial.print(": ");
       Serial.println(rooms[i].temperature);
#endif
    }
  }
}



/**
 * Returns true if the room needs heat
 */
bool needs_heating(byte room){
    if(rooms[room].status == OPENING){
        return TRUE;
    }
    // Get slot
    byte _slot = 0;
    while(_slot <= 6 && this_time > slot[_slot]){
        _slot++;
    }
    byte wkly = weekly_program[rooms[room].program][this_weekday];
    float t = T[daily_program[wkly][_slot]];
#if DEBUG
    Serial.print("heating: slot for room ");
    Serial.print(room);
    Serial.print(": ");
    Serial.println(_slot);
    Serial.print("daily pgm: ");
    Serial.print(rooms[room].program);
    Serial.print(" target t: ");
    Serial.println(t);
#endif
    // Take hysteresis into account
    return rooms[room].temperature < (rooms[room].status == OPEN ? t + HYSTERESIS : t - HYSTERESIS);
}

/**
 * Change status
 */
void change_status(byte room, byte status){
    rooms[room].last_status_change = millis();
    rooms[room].status = status;
#if DEBUG
    Serial.print("status R");
    Serial.print(room);
    Serial.print(": ");
    Serial.println(status);
#endif    
}

/**
 * Check temperatures and perform actions
 */
void check_temperatures(){
    read_temperatures();

    now = RTC.now();
    this_time = now.hour() * 60 + now.minute();
    this_weekday = now.dayOfWeek();
#if DEBUG
    Serial.print(now.year(), DEC);
    Serial.print('/');
    Serial.print(now.month(), DEC);
    Serial.print('/');
    Serial.print(now.day(), DEC);
    Serial.print(' ');
    Serial.print(now.hour(), DEC);
    Serial.print(':');
    Serial.print(now.minute(), DEC);
    Serial.print(':');
    Serial.print(now.second(), DEC);
    Serial.println();
#endif
    pump_open = 0;
    for(int i=0; i<ROOMS; i++){
        byte new_status = rooms[i].status;
        if(!needs_heating(i)){
            new_status = CLOSED;
        } else {
            switch(rooms[i].status){
                case OPENING:
                    if(VALVE_OPENING_TIME < millis() - rooms[i].last_status_change){
                        new_status = OPEN;
                    }
                    break;
                case OPEN:
                    if(RISE_TEMP_TIME <  millis() - rooms[i].last_status_change){
                        if(rooms[i].temperature - rooms[i].old_temperature < RISE_TEMP_DELTA){
                            new_status = BLOCKED;
                        }
                    } else {
                        pump_open = 1;
                    }
                    break;
                case BLOCKED:
                    if(BLOCKED_TIME <  millis() - rooms[i].last_status_change){
                        new_status = CLOSED;
                    }
                    break;
                default:
                case CLOSED:
                    new_status = OPENING;
            }
        }
        if(new_status != rooms[i].status){
            change_status(i, new_status);
        }
        digitalWrite(rooms[i].pin, new_status == OPENING || new_status == OPEN); 
    }
    digitalWrite(PUMP_PIN, pump_open);    
}


/**
 * Update user screen
 */
void show_room_status(){
    char buf[34]; // 16*2 display
    char buf2[17];
    strcpy(buf, "\0");
    for(int i=0; i<ROOMS; i++){
        sprintf(buf2, "R%d %c %d", i, rooms[i].status, (int)rooms[i].temperature);
        strcat(buf, buf2);
        strcat(buf, (i % 2) ? "\n" : " ");
    }
    // Time line
    sprintf(buf2, "%d:%d:%d %d %c\n", now.hour(), now.minute(), now.second(), this_weekday, pump_open ? '*' : 'X');
    strcat(buf, buf2);
    tree.drawUsrScreen(buf);
}



/** ******************************************************
*
* Setup & loop
*
*/

// TODO
void read_config(){
    // Do nothing
#if DEBUG
    Serial.print("Reading EEPROM");
    Serial.println("");
#endif
}


// Menu vars
int  list, sp=110;


/**
 * Set up
 *
 */ 
void setup(){

#if DEBUG
  Serial.begin(9600);
#endif

    Wire.begin();
    RTC.begin();
    if (! RTC.isrunning()) {
#if DEBUG
        Serial.println("RTC is NOT running!");
#endif
        // following line sets the RTC to the date & time this sketch was compiled
        RTC.adjust(DateTime(__DATE__, __TIME__));
    }

  pinMode(BTN_PIN, INPUT);
  pinMode(PUMP_PIN, OUTPUT);
  // Read config from EEPROM
  read_config();

  t.every(TEMP_READ_INTERVAL, check_temperatures);

  _menu *r,*s1,*s2;

  tree.begin(&lcd, 16, 2); //declare lcd object and screen size to menwiz lib

  r=tree.addMenu(MW_ROOT,NULL,F("Setup"));
    s1=tree.addMenu(MW_SUBMENU,r, F("Rooms"));
      s2=tree.addMenu(MW_VAR, s1, F("PGM"));
        s2->addVar(MW_LIST,&list);
        s2->addItem(MW_LIST, F("P0"));
        s2->addItem(MW_LIST, F("P1"));
        s2->addItem(MW_LIST, F("P2"));
      s2=tree.addMenu(MW_VAR, s1, F("Timer"));
        s2->addVar(MW_AUTO_INT,&sp,0,120,10);
    s1=tree.addMenu(MW_VAR,r, F("Time"));
      s1->addVar(MW_ACTION,myfunc);
       tree.addUsrNav(read_button);

    tree.addSplash("ItOpen Thermo\n2012\n", 2000);
    tree.addUsrScreen(show_room_status, IDLE_TIME);

    // Sensors
    // set the resolution to 12 bit (maximum)
    sensors.begin();
    for (int i=0; i<ROOMS; i++){
        sensors.setResolution(rooms[i].address, 12);  
        pinMode(rooms[i].pin, OUTPUT);
    }
}


void loop(){
  tree.draw();
  t.update();
  }


