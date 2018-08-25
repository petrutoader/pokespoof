#include <Arduino.h>
#include <EEPROM.h>
#include "pokemon.h"
/* #include "output.h" */
#include "data.h"

#define MOSI_ 3
#define MISO_ 4
#define SCLK_ 2

int bytes = 0;
uint8_t shift = 0;
uint8_t in_data = 0;
uint8_t out_data = 0;

connection_state_t connection_state = NOT_CONNECTED;
trade_centre_state_t trade_centre_state = INIT;
int counter = 0;

int trade_pokemon = -1;

unsigned long last_bit;

void printBits(byte myByte){
  for(byte mask = 0x80; mask; mask >>= 1){
    if(mask  & myByte) {
      Serial.print('1');
    } else {
      Serial.print('0');
    }
  }
  Serial.println("");
}

byte getConnectResponse(byte in) {
  Serial.println("State: Not connected");

  if (in == PKMN_CONNECTED) {
    Serial.println("Sending: PKMN_CONNECTED");
    connection_state = CONNECTED;
    return  PKMN_CONNECTED;
  }

  if (in == PKMN_MASTER) {
    Serial.println("Sending: PKMN_SLAVE");
    return PKMN_SLAVE;
  }

  if (in == PKMN_BLANK) {
    Serial.println("Sending: PKMN_BLANK");
    return PKMN_BLANK;
  }

  connection_state = NOT_CONNECTED;
  return PKMN_BREAK_LINK;
}

byte getMenuResponse(byte in) {
  Serial.println("State: Connected");
  byte response = 0x00;

  if (in == PKMN_CONNECTED) {
    response = PKMN_CONNECTED;
  } else if(in == PKMN_TRADE_CENTRE) {
    connection_state = TRADE_CENTRE;
  } else if(in == PKMN_COLOSSEUM) {
    connection_state = COLOSSEUM;
  } else if(in == PKMN_BREAK_LINK || in == PKMN_MASTER) {
    connection_state = NOT_CONNECTED;
    response = PKMN_BREAK_LINK;
  } else {
    response = in;
  }

  return response;
}

byte getTradeCentreResponse(byte in) {
  Serial.println("State: Trade centre");
  byte response = 0x00;

  if(trade_centre_state == INIT && in == 0x00) {
    trade_centre_state = READY_TO_GO;
    response = 0x00;
  } else if(trade_centre_state == READY_TO_GO && in == 0xFD) {
    trade_centre_state = SEEN_FIRST_WAIT;
    response = 0xFD;
  } else if(trade_centre_state == SEEN_FIRST_WAIT && in != 0xFD) {
    // random data of slave is ignored.
    response = in;
    trade_centre_state = SENDING_RANDOM_DATA;
  } else if(trade_centre_state == SENDING_RANDOM_DATA && in == 0xFD) {
    trade_centre_state = WAITING_TO_SEND_DATA;
    response = 0xFD;
  } else if(trade_centre_state == WAITING_TO_SEND_DATA && in != 0xFD) {
    counter = 0;

    // response first byte
    response = pgm_read_byte(&(PARTNER_DATA[counter]));

    // record the data
    INPUT_PARTNER_DATA[counter] = in;

    trade_centre_state = SENDING_DATA;
    counter++;

  } else if(trade_centre_state == SENDING_DATA) {
    response = pgm_read_byte(&(PARTNER_DATA[counter]));
    INPUT_PARTNER_DATA[counter] = in;

    delay(50);

    counter++;

    if(counter == TRAINER_DATA_LENGTH) {
      trade_centre_state = SENDING_PATCH_DATA;
    }
  } else if(trade_centre_state == SENDING_PATCH_DATA && in == 0xFD) {
    counter = 0;
    response = 0xFD;
  } else if(trade_centre_state == SENDING_PATCH_DATA && in != 0xFD) {
    response = in;
    counter++;
    if(counter == 197) {
      trade_centre_state = TRADE_PENDING;
    }
  } else if(trade_centre_state == TRADE_PENDING && (in & 0x60) == 0x60) {
    if (in == 0x6f) {
      trade_centre_state = READY_TO_GO;
      response = 0x6f;
    } else {
      response = 0x60; // first pokemon
      trade_pokemon = in - 0x60;
    }
  } else if(trade_centre_state == TRADE_PENDING && in == 0x00) {
    response = 0;
    trade_centre_state = TRADE_CONFIRMATION;
  } else if(trade_centre_state == TRADE_CONFIRMATION && (in & 0x60) == 0x60) {
    response = in;
    if (in  == 0x61) {
      trade_pokemon = -1;
      trade_centre_state = TRADE_PENDING;
    } else {
      trade_centre_state = DONE;
    }
  } else if(trade_centre_state == DONE && in == 0x00) {
    response = 0;
    trade_centre_state = INIT;
  } else {
    response = in;
  }

  return response;
}

byte handleIncomingByte(byte in) {
  byte response = 0x00;

  if (connection_state == NOT_CONNECTED) {
    return getConnectResponse(in);
  } else if (connection_state == CONNECTED) {
    return getMenuResponse(in);
  } else if (connection_state == TRADE_CENTRE) {
    return getTradeCentreResponse(in);
  } else {
    response = in;
  }

  return response;
}


void transferBit(void) {
  byte raw_data = digitalRead(MISO_);

  in_data |= raw_data << (7-shift);

  if(++shift > 7) {
    shift = 0;
    bytes++;

    out_data = handleIncomingByte(in_data);

    Serial.print("Variables:");
    Serial.print(" trade_centre_state:");
    Serial.print(trade_centre_state);
    Serial.print(" in_data:");
    Serial.print(in_data, HEX);
    Serial.print(" out_data:");
    Serial.print(out_data, HEX);
    Serial.println("");
    Serial.println("--------------");

    in_data = 0;
  }
  
  while(!digitalRead(SCLK_));

  digitalWrite(MOSI_, out_data & 0x80 ? HIGH : LOW);
  out_data <<= 1;
}

void setup() {
    /* for (int i = 0; i <= INIT_LENGTH; i++) { */
    /*   EEPROM.write(i, INIT_BLOCK[i]); */
    /* } */

    Serial.begin(115200);

    pinMode(SCLK_, INPUT);
    pinMode(MISO_, INPUT);
    pinMode(MOSI_, OUTPUT);
    
    /* for (int i=0; i<44+11+11; i++) { */
    /*   Serial.print(" "); */
    /*   Serial.print(EEPROM.read(i), HEX); */
    /* } */
    /* Serial.print("\n"); */
    
    digitalWrite(MOSI_, LOW);
    out_data <<= 1;

    Serial.println("[+] Ready");
}

void loop() {
    last_bit = micros();

    while(digitalRead(SCLK_)) {
      if (micros() - last_bit > 1000000) {
        Serial.println("[/] Idle");
        last_bit = micros();

        shift = 0;
        in_data = 0;

        /* if(trade_pokemon >= 0 && trade_centre_state < TRADE_PENDING) { */
        /*   // a trade has been confrimed */
        /*   int i; */
        /*   int start = 19 + (trade_pokemon * 44); */
        /*   for (i=0; i<44; i++) { */
        /*     EEPROM.write(i, PARTNER_DATA[start+i]); */
        /*     Serial.print(" "); */
        /*     Serial.print(PARTNER_DATA[start+i], HEX); */
        /*   } */
        /*   Serial.print("\nOT\n"); */
        /*   start = 283 + (trade_pokemon * 11); */
        /*   for (i=0; i<11; i++) { */
        /*     EEPROM.write(i+44, PARTNER_DATA[start+i]); */
        /*     Serial.print(" "); */
        /*     Serial.print(PARTNER_DATA[start+i], HEX); */
        /*   } */
        /*   Serial.print("\nnick\n"); */
        /*   start = 349 + (trade_pokemon * 11); */
        /*   for (i=0; i<11; i++) { */
        /*     EEPROM.write(i+44+11, PARTNER_DATA[start+i]); */
        /*     Serial.print(" "); */
        /*     Serial.print(PARTNER_DATA[start+i], HEX); */
        /*   } */
        /*   trade_pokemon = -1; */
        /*   Serial.print("\ntrade saved\n"); */
        /* } */
      }
    }
    transferBit();
}


