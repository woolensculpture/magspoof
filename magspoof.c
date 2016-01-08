/*
 * MagSpoof - "wireless" magnetic stripe/credit card emulator
*
 * by Samy Kamkar
 *
 * http://samy.pl/magspoof/
 *
 * - Allows you to store all of your credit cards and magstripes in one device
 * - Works on traditional magstripe readers wirelessly (no NFC/RFID required)
 * - Can disable Chip-and-PIN (code not included)
 * - Correctly predicts Amex credit card numbers + expirations from previous card number (code not included)
 * - Supports all three magnetic stripe tracks, and even supports Track 1+2 simultaneously
 * - Easy to build using Arduino or ATtiny
 *
 */
// this code is meant to be compiled with the arduino library and/or environment while still using lower level avr commands
#include <avr/sleep.h> 
#include <avr/interrupt.h>

#define PIN_A 0
#define PIN_B 1
#define ENABLE_PIN 3 // also green LED
#define SWAP_PIN 4 // unused
#define BUTTON_PIN 2
#define CLOCK_US 200

#define BETWEEN_ZERO 53 // 53 zeros between tracks 1 & 2

#define TRACKS 2

// consts get stored in flash as we don't adjust them
const char* tracks[] = {
"%B123456781234567^LASTNAME/FIRST^YYMMSSSDDDDDDDDDDDDDDDDDDDDDDDDD?\0", // Track 1 of 3 on the magnetic stripe
";123456781234567=YYMMSSSDDDDDDDDDDDDDD?\0" // Track 2 of 3 on the magnetic stripe
};
// Track 3 on the magnetic stripe is rarely used and only contains info on the cardholder usually

/**
 * Since the hbridge can only replicate one magnetic stripe at a time, the second stripe is reversed then read out after the 
 * first stripe. This simulates the magnetic card being swiped up then down repeatedly and is enough to fool most card readers.
 */
char revTrack[41]; // the location for storing the reverse of track 2 for the downward swipe

/** there are 3 different formats that a magnetic card can follow, one uses traditional ascii-128 character formatting and is 32
 * characters in length per stripe, the others follows the older DEC format which is only 6 bits compared to 8 and have 48 characters
 * per stripe. See https://en.wikipedia.org/wiki/Magnetic_stripe_card#Financial_cards for a more indepth explaination.
 */
const int sublen[] = { 32, 48, 48 }; // these constants are the number of characters for the card format
const int bitlen[] = { 7, 5, 5 };    // these constants are the encoding size for the characters(8 vs. 6 bits) corresponding the
                                     // formats of sublen.

unsigned int curTrack = 0; // which track is currently being written
int dir; 

void setup(){
  // assigns each pin to it's intended purpose using arduino functionallity
  pinMode(PIN_A, OUTPUT);
  pinMode(PIN_B, OUTPUT);
  pinMode(ENABLE_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // blink to show we started up
  blink(ENABLE_PIN, 200, 3);
  
  // store reverse track 2 to play later
  storeRevTrack(2);
}

void blink(int pin, int msdelay, int times){
  for (int i = 0; i < times; i++){
    digitalWrite(pin, HIGH);
    delay(msdelay);
    digitalWrite(pin, LOW);
    delay(msdelay);
  }
}

// send a single bit out(need to check, if so then sendbit should probably be boolean)
// TODO determine the purpose and output of dir ^= 1 and !dir
void playBit(int sendBit){
  dir ^= 1; // xor with 1 and assignment to dir, either makes dir all zeros or more likely makes dir alter its first bit, needs further research.
  digitalWrite(PIN_A, dir); // write the digital output of dir to the hbridge
  digitalWrite(PIN_B, !dir); // write the logical inverse (hopefully) increasing the output of the hbridge
  delayMicroseconds(CLOCK_US);

  if (sendBit){
    dir ^= 1;
    digitalWrite(PIN_A, dir);
    digitalWrite(PIN_B, !dir);
  }
  delayMicroseconds(CLOCK_US);
}

// plays reverse of the track given to it
void reverseTrack(int track){
  int i = 0;
  track--; // index 0
  dir = 0;

  while (revTrack[i++] != '\0'); // finds the end of the track denoted by a null character
  i--; // goes back one space to the last character of the stripe
  while (i--){
    for (int j = bitlen[track]-1; j >= 0; j--){
      playBit((revTrack[i] >> j) & 1);
    }
  }
}

// plays out a full track, calculating CRCs and LRC
void playTrack(int track){
  int tmp, crc, lrc = 0;
  track--; // index 0
  dir = 0;

  // enable H-bridge and LED
  digitalWrite(ENABLE_PIN, HIGH);

  // First put out a bunch of leading zeros.
  for (int i = 0; i < 25; i++)
    playBit(0);

  for (int i = 0; tracks[track][i] != '\0'; i++){
    crc = 1;
    tmp = tracks[track][i] - sublen[track];

    for (int j = 0; j < bitlen[track]-1; j++){
      crc ^= tmp & 1;
      lrc ^= (tmp & 1) << j;
      playBit(tmp & 1);
      tmp >>= 1;
    }
    playBit(crc);
  } 

  // finish calculating and send last "byte" (LRC)
  tmp = lrc;
  crc = 1;
  for (int j = 0; j < bitlen[track]-1; j++){
    crc ^= tmp & 1;
    playBit(tmp & 1);
    tmp >>= 1;
  }
  playBit(crc);

  // if track 1, play 2nd track in reverse (like swiping back?)
  if (track == 0)
  {
    // if track 1, also play track 2 in reverse
    // zeros in between
    for (int i = 0; i < BETWEEN_ZERO; i++){
      playBit(0);
   }
    // send second track in reverse
    reverseTrack(2);
  }

  // finish with 0's
  for (int i = 0; i < 5 * 5; i++){
    playBit(0);
  }
  
  digitalWrite(PIN_A, LOW);
  digitalWrite(PIN_B, LOW);
  digitalWrite(ENABLE_PIN, LOW);
}



// stores track for reverse usage later
void storeRevTrack(int track){
  int i, tmp, crc, lrc = 0;
  track--; // index 0
  dir = 0;

  for (i = 0; tracks[track][i] != '\0'; i++)
  {
    crc = 1;
    tmp = tracks[track][i] - sublen[track];

    for (int j = 0; j < bitlen[track]-1; j++)
    {
      crc ^= tmp & 1;
      lrc ^= (tmp & 1) << j;
      tmp & 1 ?
        (revTrack[i] |= 1 << j) :
        (revTrack[i] &= ~(1 << j));
      tmp >>= 1;
    }
    crc ?
      (revTrack[i] |= 1 << 4) :
      (revTrack[i] &= ~(1 << 4));
  } 

  // finish calculating and send last "byte" (LRC)
  tmp = lrc;
  crc = 1;
  for (int j = 0; j < bitlen[track]-1; j++)
  {
    crc ^= tmp & 1;
    tmp & 1 ?
      (revTrack[i] |= 1 << j) :
      (revTrack[i] &= ~(1 << j));
    tmp >>= 1;
  }
  crc ?
    (revTrack[i] |= 1 << 4) :
    (revTrack[i] &= ~(1 << 4));

  i++;
  revTrack[i] = '\0';
}

void sleep()
{
  GIMSK |= _BV(PCIE);                     // Enable Pin Change Interrupts
  PCMSK |= _BV(PCINT2);                   // Use PB3 as interrupt pin
  ADCSRA &= ~_BV(ADEN);                   // ADC off
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);    // replaces above statement

  MCUCR &= ~_BV(ISC01);
  MCUCR &= ~_BV(ISC00);       // Interrupt on rising edge
  sleep_enable();                         // Sets the Sleep Enable bit in the MCUCR Register (SE BIT)
  sei();                                  // Enable interrupts
  sleep_cpu();                            // sleep

  cli();                                  // Disable interrupts
  PCMSK &= ~_BV(PCINT2);                  // Turn off PB3 as interrupt pin
  sleep_disable();                        // Clear SE bit
  ADCSRA |= _BV(ADEN);                    // ADC on

  sei();                                  // Enable interrupts
}

// XXX move playtrack in here?
ISR(PCINT0_vect) {
  /*  noInterrupts();
   while (digitalRead(BUTTON_PIN) == LOW);
   
   delay(50);
   while (digitalRead(BUTTON_PIN) == LOW);
   playTrack(1 + (curTrack++ % 2)); 
   delay(400);
   
   interrupts();*/

}

void loop()
{

  //for(int i=0;i<10;i++){playTrack(1+(curTrack++%2));delay(3000);}

  sleep();

  noInterrupts();
  while (digitalRead(BUTTON_PIN) == LOW);

  delay(50);
  while (digitalRead(BUTTON_PIN) == LOW);
  playTrack(1 + (curTrack++ % 2)); 
  delay(400);

  interrupts();
  //playTrack(1 + (curTrack++ % 2));
}



