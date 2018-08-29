#include "OneWire.h"
#include "Cmd.h"
/**
 * Control the output state of the arduino via serial over usb.
 * Care must be taken for relays : their output are inversed : a pin low means the relay is closed
*/

#define SF(a) Serial.println(F(a))
#define SpF(a) Serial.print(F(a))
#define EOT() SF("   ")
#define PROMPT() Serial.print(F("> "))

//#define DEBUG

#define PIN_TEMP 2

#define PIN_LOAD  9
#define PIN_MEMORY  10
#define PIN_DISK 11

#define PIN_START 4
#define PIN_END  8

OneWire ds(PIN_TEMP);

// pin connected to relays
const int relays[PIN_END - PIN_START + 1] = {0, 1, 1, 1, 1};
static char buffer[30];

void setup() {
  Serial.begin(9600);

  for (byte i = PIN_START; i <= PIN_END; i++) {
    pinMode(i, OUTPUT);
    set_pin_state(i, !relays[i - PIN_START]);
  }

  pinMode(PIN_LOAD, OUTPUT);
  pinMode(PIN_DISK, OUTPUT);

  cmdInit(&Serial);

  cmdAdd("h", print_help);
  cmdAdd("help", print_help);
  cmdAdd("load", set_load);
  cmdAdd("disk", set_disk);
  cmdAdd("memory", set_memory);
  cmdAdd("s", print_io_status);
  cmdAdd("status", print_io_status);
  cmdAdd("io", print_io_status);
  cmdAdd("t", print_temperature);
  cmdAdd("temp", print_temperature);
  cmdAdd("pin", parse_pin_state);

  PROMPT();
}

void print_io_status(int arg_cnt, char **args) {
  SF("\r\nI/O Status :");
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
    SF("OK");
  } else {
    print_help(0, NULL);
  }
}
void set_pin_state(byte pin, byte value) {
  if (pin < PIN_START || pin > PIN_END) {
    SF("Invalid pin number");
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
  char str_temp[6];
  byte i;
  byte present = 0;
  byte type_s;
  byte data[12];
  byte addr[8];
  byte signBit = 0;
  float temp;


  SF("\r\nTemperature :");
  
  while ( ds.search(addr)) {

    SpF("ROM = [ ");
    
    for ( i = 0; i < 8; i++) {
      Serial.print(addr[i], HEX);
      Serial.write(' ');
    }
    SpF("]");

    if (OneWire::crc8(addr, 7) != addr[7]) {
      SF("CRC is not valid!");
      continue;
    }

    // the first ROM byte indicates which chip
    switch (addr[0]) {
      case 0x10:

#ifdef DEBUG
        SF("  Chip = DS18S20");  // or old DS1820
#endif
        type_s = 1;
        break;
      case 0x28:
#ifdef DEBUG
        SF("  Chip = DS18B20");
#endif
        type_s = 0;
        break;
      case 0x22:
#ifdef DEBUG
        SF("  Chip = DS1822");
#endif
        type_s = 0;
        break;
      default:
        SF("Device is not a DS18x20 family device.");
        continue;
    }

    ds.reset();
    ds.select(addr);
    ds.write(0x44, 1);        // start conversion, with parasite power on at the end

    delay(1000);     // maybe 750ms is enough, maybe not
    // we might do a ds.depower() here, but the reset will take care of it.

    present = ds.reset();
    ds.select(addr);
    ds.write(0xBE);         // Read Scratchpad

#ifdef DEBUG
    SpF("  Data = ");
    Serial.print(present, HEX);
    SpF(" ");
#endif
    for ( i = 0; i < 9; i++) {           // we need 9 bytes
      data[i] = ds.read();
    }
#ifdef DEBUG
    SpF(" CRC=");
    SpF(OneWire::crc8(data, 8), HEX);
    SF("");
#endif

    // convert the data to actual temperature
    if (type_s) {

      //            unsigned int raw = (data[1] << 8) | data[0];
      //            raw = raw << 3; // 9 bit resolution default
      //            if (data[7] == 0x10) {
      //                // count remain gives full 12 bit resolution
      //                raw = (raw & 0xFFF0) + 12 - data[6];
      //            }
      //            sprintf(buffer, " - Temperature = %d °C", raw/16);
    } else {
      temp = ((data[1] << 8) | data[0]) * 0.0625;
      // /!\ le sprintf d'un float n'est pas supporté, il faut passer par dtostrf()
      dtostrf(temp, 4, 2, str_temp);
      sprintf(buffer, " - Temperature : %s °C", str_temp);

    }
    Serial.println(buffer);
  }

  ds.reset_search();

  EOT();
}

void loop()
{
  cmdPoll();
}

void set_load(int arg_cnt, char **args) {
  vumeter(arg_cnt, args, PIN_LOAD);
}

void set_disk(int arg_cnt, char **args) {  
  vumeter(arg_cnt, args, PIN_DISK);
}

void set_memory(int arg_cnt, char **args) {  
  vumeter(arg_cnt, args, PIN_MEMORY);
}

void vumeter(int arg_cnt, char **args, byte pin) {
  if (arg_cnt == 2) {
    int val = atoi(args[1]); // value in %
    analogWrite(pin, val * 255 / 100);
    SF("OK");
  }
}

void print_pin_range() {
  SpF("<pin number [");
  Serial.print(PIN_START);
  SpF("-");
  Serial.print(PIN_END);
  SpF("]> <0,1>");
}

void print_help(int arg_cnt, char **args) {
  SF("\r\n    ___          _       _");
  SF("   / _ \\        | |     (_)");
  SF("  / /_\\ \\_ __ __| |_   _ _ _ __   ___");
  SF("  |  _  | '__/ _` | | | | | '_ \\ / _ \\");
  SF("  | | | | | | (_| | |_| | | | | | (_) |");
  SF("  \\_| |_/_|  \\__,_|\\__,_|_|_| |_|\\___/");

  SF("\r\nCommands available :");
  SpF("  ");
  print_pin_range();
  SF(" - set pin value");
  SF("  h|help                   - help");
  SF("  s|io|status              - i/o status");
  SF("  t|temp                   - temperature");
  SF("  load <val>               - set load value");
  SF("  disk <val>               - set disk value");
  SF("  memory <val>             - set memory value");
}

// vim: syntax=c
