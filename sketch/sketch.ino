/**
   Control the output state of the arduino via serial over usb.
   Care must be taken for relays : their output are inversed : a pin low means the relay is closed
*/

#include "OneWire.h"
#include "Cmd.h"
#include <Wire.h>
#include "Adafruit_BMP085.h"

#define SFln(a) Serial.println(F(a))
#define SF(a) Serial.print(F(a))
#define EOT() SFln("   ")
#define PROMPT() SF("> ")

//#define DEBUG

#define PIN_TEMP 2

#define PIN_LOAD1 9
#define PIN_LOAD5 10
#define PIN_DISK 11

#define PIN_START 4
#define PIN_END  8

#define SENSOR_ADDRESS_SIZE 8
#define NB_MAX_SENSORS 10 // change this to allow more sensors

OneWire ds(PIN_TEMP);

Adafruit_BMP085 bmp;

// pin connected to relays
const int relays[PIN_END - PIN_START + 1] = {0, 1, 1, 1, 1};
static char buffer[15];

int nb_sensors = 0;
byte addrs[NB_MAX_SENSORS][SENSOR_ADDRESS_SIZE];


void setup() {
  byte i = 0;
  
  Serial.begin(9600);

  for (i = PIN_START; i <= PIN_END; i++) {
    pinMode(i, OUTPUT);
    set_pin_state(i, !relays[i - PIN_START]);
  }

  pinMode(PIN_LOAD1, OUTPUT);
  pinMode(PIN_LOAD5, OUTPUT);
  pinMode(PIN_DISK, OUTPUT);

  if (!bmp.begin()) {
    SFln("Could not find a valid BMP1080 sensor, check wiring!");
  }

  SFln("Scan 1-Wire bus");
  
  byte addr[SENSOR_ADDRESS_SIZE];

  while ( ds.search(addr)) {
    SF("@ :");
    printArray(addr);
    SF(" = ");

    if (isDeviceValid(addr)) {
      SFln("DS18B20");

      for ( i = 0; i < SENSOR_ADDRESS_SIZE; i++) {
        addrs[nb_sensors][i] = addr[i];
      }
      
      nb_sensors++;
    }
  }
  if (nb_sensors > 0) {
    SFln("");
  } else {
    SFln("Could not find a valid DS18B20 sensor, check wiring!");
  }
  ds.reset_search();

  cmdInit(&Serial);

  cmdAdd("h", print_help);
  cmdAdd("help", print_help);
  cmdAdd("load1", set_load1);
  cmdAdd("load5", set_load5);
  cmdAdd("disk", set_disk);
  cmdAdd("s", print_io_status);
  cmdAdd("status", print_io_status);
  cmdAdd("io", print_io_status);
  cmdAdd("t", print_temperature);
  cmdAdd("temp", print_temperature);
  cmdAdd("pressure", print_pressure);
  cmdAdd("p", print_pressure);
  cmdAdd("pin", parse_pin_state);

  PROMPT();
}

void print_io_status(int arg_cnt, char **args) {
  SFln("\r\nI/O Status :");
  byte i;
  for (i = PIN_START; i <= PIN_END; i++) {
    if (relays[i - PIN_START]) {
      sprintf(buffer, "  %d => %d (R)", i, !digitalRead(i));
    } else {
      sprintf(buffer, "  %d => %d", i, digitalRead(i));
    }
    Serial.println(buffer);
  }
  EOT();
}

void parse_pin_state(int arg_cnt, char **args) {
  if (arg_cnt == 3) {
    int pin = atoi(args[1]);
    int value = atoi(args[2]);
    set_pin_state(pin, value);
    SFln("OK");
  } else {
    print_help(0, NULL);
  }
}
void set_pin_state(byte pin, byte value) {
  if (pin < PIN_START || pin > PIN_END) {
    SFln("Invalid pin number");
    print_pin_range();
  }
  if (relays[pin-PIN_START]) {
    // for relay pin, the state must be inversed
    if (value == HIGH) {
      digitalWrite(pin, LOW);
    } else {
      digitalWrite(pin, HIGH);
    }
  } else {
    digitalWrite(pin, value);
  }
}

void print_temperature(int arg_cnt, char **args) {
  SFln("\r\nTemperature :");
  byte i = 0;
  for (i = 0; i<nb_sensors; i++) {
    startTemperatureMeasure(addrs[i]);
  }
  
  delay(800);
  
  for (i = 0; i<nb_sensors; i++) {
    readTemperature(addrs[i]);
  }

  EOT();
}

void startTemperatureMeasure(byte addr[]) {
  ds.reset();
  ds.select(addr);
  ds.write(0x44, 1);
}

void readTemperature(byte addr[]) {
  char str_temp[6];
  byte i;
  byte present = 0;
  byte type_s;
  byte data[12];
  byte signBit = 0;
  float temp;

  SF("ROM [");
  printArray(addr);
  SF(" ]");
  
  ds.reset();
  ds.select(addr);
  ds.write(0xBE);         // Read Scratchpad

#ifdef DEBUG
  SF("  Data = ");
  Serial.print(present, HEX);
  SF(" ");
#endif
  for ( i = 0; i < 9; i++) {           // we need 9 bytes
    data[i] = ds.read();
  }
#ifdef DEBUG
  SF(" CRC=");
  SF(OneWire::crc8(data, 8), HEX);
  SFln("");
#endif

  temp = ((data[1] << 8) | data[0]) * 0.0625;
  // /!\ le sprintf d'un float n'est pas supporté, il faut passer par dtostrf()
  dtostrf(temp, 4, 2, str_temp);
  sprintf(buffer, " = %s °C", str_temp);

  Serial.println(buffer);

}

void print_pressure(int arg_cnt, char **args) {
  SF("Temperature = ");
  Serial.print(bmp.readTemperature());
  SFln(" *C");
  
  SF("Pressure = ");
  Serial.print(bmp.readPressure());
  SFln(" Pa");

  EOT();
}

void loop()
{
  cmdPoll();
}

void set_load1(int arg_cnt, char **args) {
  vumeter(arg_cnt, args, PIN_LOAD1);
}

void set_load5(int arg_cnt, char **args) {  
  vumeter(arg_cnt, args, PIN_LOAD5);
}

void set_disk(int arg_cnt, char **args) {  
  vumeter(arg_cnt, args, PIN_DISK);
}

void vumeter(int arg_cnt, char **args, byte pin) {
  if (arg_cnt == 2) {
    int val = atoi(args[1]); // value in %
    val = max(min(val, 100), 0); // check range 0-100
    analogWrite(pin, val * 255 / 100);
    SFln("OK");
  }
}

void print_pin_range() {
  SF("<pin number [");
  Serial.print(PIN_START);
  SF("-");
  Serial.print(PIN_END);
  SF("]> <0,1>");
}

void print_help(int arg_cnt, char **args) {
  SFln("\r\n    ___          _       _");
  SFln("   / _ \\        | |     (_)");
  SFln("  / /_\\ \\_ __ __| |_   _ _ _ __   ___");
  SFln("  |  _  | '__/ _` | | | | | '_ \\ / _ \\");
  SFln("  | | | | | | (_| | |_| | | | | | (_) |");
  SFln("  \\_| |_/_|  \\__,_|\\__,_|_|_| |_|\\___/");

  SFln("\r\nCommands available :");
  SF("  pin ");
  print_pin_range();
  SFln(" - set pin value");
  SFln("  h|help                       - help");
  SFln("  s|io|status                  - i/o status");
  SFln("  t|temp                       - temperature");
  SFln("  p|pressure                   - pressure");
  SFln("  load1 <val>                  - set load (1 min) value (log scaled)");
  SFln("  load5 <val>                  - set load (5 min) value (log scaled)");
  SFln("  disk <val>                   - set disk value");
}

void printArray(byte x[]) {
   char buffer[10];
   for (int i = 0; i < SENSOR_ADDRESS_SIZE; i++) {
       sprintf(buffer, " %02X", x[i]); // Convert hex byte to ascii 0Xvv
       Serial.print(buffer);
   }
}

bool isDeviceValid(byte addr[]) {
  if (OneWire::crc8(addr, 7) != addr[7]) {
    SFln("CRC is not valid!");
    return false;
  }

  if (addr[0] != 0x28) {
    SFln("unknown device");
    return false;
  }

  return true;
}

// vim: syntax=c
