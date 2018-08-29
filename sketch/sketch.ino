#include "OneWire.h"
/**
 * Control the output state of the arduino via serial over usb.
 * Care must be taken for relays : their output are inversed : a pin low means the relay is closed
 */


#define NB_OUTPUT   10
#define PIN_START   2
#define SF(a) Serial.println(F(a))
#define EOT() SF("   ")
#define PROMPT() Serial.print(F("> "))

//#define DEBUG

OneWire ds(10);  // on pin 10

void print_help();

// pin 6 - 9 are connected to relay
const int relays[NB_OUTPUT] = {0,0,0,0,0,0,1,1,1,1};

byte state = 0;
byte pin = 0;
char buffer[30];
char str_temp[6];
byte i;
byte present = 0;
byte type_s;
byte data[12];
byte addr[8];
byte signBit = 0;
float temp;
#ifdef DEBUG
int cpt;
#endif

void setup() {
    for (i = PIN_START; i< NB_OUTPUT; i++) {
        pinMode(i, OUTPUT);
        set_pin_state(i, LOW);
    }

    Serial.begin(9600);
    PROMPT();
}

void print_io_status() {
    SF("\r\nI/O Status :");
#ifdef DEBUG
    Serial.print("CPT : ");
    Serial.println(cpt);
    cpt++;
#endif
    for (i = PIN_START; i < NB_OUTPUT; i++) {
        if (relays[i]) {
            sprintf(buffer, "  %d => %d (R)", i, !digitalRead(i));
        } else {
            sprintf(buffer, "  %d => %d", i, digitalRead(i));
        }
        Serial.println(buffer);
    }
    EOT();
    state = 0;
}

void set_pin_state(uint8_t pin, uint8_t val) {
    if (relays[pin] && val == HIGH) {
        digitalWrite(pin, LOW);
    } else if (relays[pin] && val == LOW) {
        digitalWrite(pin, HIGH);
    } else {
        digitalWrite(pin, val);
    }
}

void print_temperature() {
    SF("\r\nTemperature :");

    while ( ds.search(addr)) {

        Serial.print(F("ROM = [ "));
        for( i = 0; i < 8; i++) {
            Serial.print(addr[i], HEX);
            Serial.write(' ');
        }
        Serial.print(F("]"));

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
        ds.write(0x44,1);         // start conversion, with parasite power on at the end

        delay(1000);     // maybe 750ms is enough, maybe not
        // we might do a ds.depower() here, but the reset will take care of it.

        present = ds.reset();
        ds.select(addr);    
        ds.write(0xBE);         // Read Scratchpad

#ifdef DEBUG
        Serial.print("  Data = ");
        Serial.print(present,HEX);
        Serial.print(" ");
#endif
        for ( i = 0; i < 9; i++) {           // we need 9 bytes
            data[i] = ds.read();
        }
#ifdef DEBUG
        Serial.print(" CRC=");
        Serial.print(OneWire::crc8(data, 8), HEX);
        Serial.println();
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
    state = 0;
}

void loop()
{
    if (Serial.available() > 0) {
        // read the incoming byte:
        char r = Serial.read();
        Serial.print(r);
        if (r == 's') {
            print_io_status();
            PROMPT();
        } else if (r == 't') {
            print_temperature();
            PROMPT();
        } else if (r == 'h') {
            print_help();
            PROMPT();
        } else {
            if (state == 2) {
                if (r == '1') {
                    set_pin_state(pin, HIGH);
                    SF("\r\n  OK");
                } else if (r == '0') {
                    set_pin_state(pin, LOW);
                    SF("\r\n  OK");
                } else {
                    SF("\r\n/!\\ Syntax error, format <pin number>=<0,1>");
                }
                PROMPT();
                state = 0;
            } else if (state == 1 && r == '=') {
                state = 2;
            } else if (state == 0) {
                pin = r - '0';
                if (pin >= PIN_START && pin < NB_OUTPUT) {
                    state = 1;
                }
            } else {
                SF("\r\n/!\\ Syntax error, format <pin number>=<0,1>");
                PROMPT();
                state = 0;
            }
        }
    }
}

void print_help() {
    SF("\r\n    ___          _       _");
    SF("   / _ \\        | |     (_)");
    SF("  / /_\\ \\_ __ __| |_   _ _ _ __   ___");
    SF("  |  _  | '__/ _` | | | | | '_ \\ / _ \\");
    SF("  | | | | | | (_| | |_| | | | | | (_) |");
    SF("  \\_| |_/_|  \\__,_|\\__,_|_|_| |_|\\___/");

    SF("\r\n  Build " __DATE__ " : " __TIME__ );
    SF("\r\nHelp\r\nCommand available :");
    SF("      <pin number [2-9]>=<0,1>");
    SF("      h - help");
    SF("      s - i/o status");
    SF("      t - temperature\r\n");
    state = 0;
}

// vim: syntax=c
