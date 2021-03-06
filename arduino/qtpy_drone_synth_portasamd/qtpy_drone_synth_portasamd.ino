/**
 * qtpy_drone_synth_portasmd.ino - 8 knobs control (at least) 8 oscillators, knobs via seesaw
 *                                 portamento and works on QTPy M0 SAMD21
 * 
 *  Circuit:
 *  - 8 pots hook up to seesaw, seesaw connected to QTPy RP2040
 *  - Copy the PWM cleanup RC filter from schematic figure 3.4.1 "PWM Audio" in
 *     https://datasheets.raspberrypi.com/rp2040/hardware-design-with-rp2040.pdf
 *     also see: https://www.youtube.com/watch?v=rwPTpMuvSXg
 *  - Wire "A0" of QTPy RP2040 to input of this circuit, output to TRRS Tip & Ring1
 *  
 *  Compiling:
 *  - Use the RP2040 Arduino core at: https://github.com/earlephilhower/arduino-pico
 *  - Use the Mozzi fork at https://github.com/pschatzmann/Mozzi
 *  - For slightly better audio quality, add the following after "#define PWM_RATE" 
 *    in "Mozzi/AudioConfigRP2040.h"
 *      #define AUDIO_BITS 10
 *      #define AUDIO_BIAS ((uint16_t) 512)
 *      #define PWM_RATE (60000*2)
 *    
 *  Notes:
 *  - Seems to not start up sometimes? or if all the pots are set to zero?
 *  
 * 15 Feb 2022 - @todbot
 */

//#define CONTROL_RATE 128   // sets update rate of Mozzi's updateControl() function
#include <MozziGuts.h>
#include <Oscil.h>
#include <tables/saw_analogue512_int8.h> // oscillator waveform
#include <tables/cos2048_int8.h> // filter modulation waveform
#include <LowPassFilter.h>
#include <Portamento.h>
#include <mozzi_rand.h>  // for rand()
#include <mozzi_midi.h>  // for mtof()

#include <Wire.h>
#include <Adafruit_seesaw.h>
#include <Adafruit_NeoPixel.h>

#define NUM_KNOBS 8
#define NUM_VOICES 16
#define NUM_BUTTS 4

#define LED_PIN 11
#define NUM_LEDS 1

Adafruit_seesaw ss( &Wire ); // seeknobs I2C is std SDA/SCL, StemmaQT port on Wire1 on QTPy RP2040 

// seesaw on Attiny8x7, analog in can be 0-3, 6, 7, 18-20
uint8_t seesaw_knob_pins[ NUM_KNOBS ] = {7,6, 3,2, 1,0, 19,18};  // pinout on seeknobs3qtpy board
uint8_t seesaw_butt_pins[ NUM_BUTTS ] = { 5, 9, 13, 14 }; // not 17!
uint32_t seesaw_butt_mask = ((uint32_t) (1<<5) | (1<<9) | (1<<13) | (1<<14)) ;

Adafruit_NeoPixel leds(NUM_LEDS,LED_PIN, NEO_GRB + NEO_KHZ800);

uint8_t seesaw_knob_i = 0;
float knob_smoothing = 0.3; // 1.0 = all old value
int knob_vals[ NUM_KNOBS ];
int last_knob_vals[NUM_KNOBS];
//int butt_vals[ NUM_BUTTS ];
uint32_t butt_vals = 0;

Oscil<SAW_ANALOGUE512_NUM_CELLS, AUDIO_RATE> aOscs [NUM_VOICES];
//Oscil<COS2048_NUM_CELLS, CONTROL_RATE> kFilterMod(COS2048_DATA);
Portamento <CONTROL_RATE> portamentos[NUM_VOICES];

LowPassFilter lpf;
uint8_t resonance = 120; // range 0-255, 255 is most resonant
uint8_t cutoff = 70;

uint32_t lastDebugMillis = 0; // debug
uint32_t knobUpdateMillis = 0;
uint32_t lastScatterMillis = 0;

bool scatterMode = false;
bool noteMode = false;

// 
void setup() {
  // RP2040 defaults to GP0, from https://github.com/pschatzmann/Mozzi/
  #ifdef ARDUINO_ARCH_RP2040
  Mozzi.setPin(0,29);  // this sets RP2040 GP29 / QT Py RP2040 "A0"
  #endif
  // on SAMD21 output is A0 always
  
  Serial.begin(115200);
  //while (!Serial) delay(10);   // debug: wait until serial port is opened

  lpf.setCutoffFreqAndResonance(cutoff, resonance);
  for( int i=0; i<NUM_VOICES; i++) { 
     aOscs[i].setTable(SAW_ANALOGUE512_DATA);
     portamentos[i].setTime(100);
  }

  #ifndef ARDUINO_ARCH_RP2040
  setupKnobs();
  #endif
  
  leds.begin();
  leds.setBrightness(32);
  leds.fill(0xff00ff);
  leds.show();
  
  startMozzi();
  
  Serial.println("qtpy_drone_synth_portasmd started");
}

//
void loop() {
  audioHook();
}

// we are multicore on RP2040!
#ifdef ARDUINO_ARCH_RP2040
void setup1() {
  setupKnobs();  
}
void loop1() {
  readKnobs();
}
#endif

void setupKnobs() {
  if(!ss.begin()){
    Serial.println(F("seesaw not found!"));
    while(1) delay(10);
  }
  ss.pinModeBulk(seesaw_butt_mask, INPUT_PULLUP);
}

// buttid is 0,1,2,3 for buttons 1,2,3,4
bool isButtPressed(uint8_t buttid) {
   uint8_t pin = seesaw_butt_pins[buttid];
   return !(butt_vals & (1<<pin));
}

// read the knobs & buttons from seesaw
// i2c transactions take time, so only do one at a time
void readKnobs() {

  // if we've read all knobs, do buttons and get out
  if( seesaw_knob_i == NUM_KNOBS ) { 
    butt_vals = ss.digitalReadBulk(seesaw_butt_mask);
    seesaw_knob_i = 0;
    return;
  }
  
  // read a knob
  int val = ss.analogRead( seesaw_knob_pins[seesaw_knob_i] );

  // smooth it based on last val
  int last_val = knob_vals[ seesaw_knob_i ];
  val = val + (knob_smoothing * (last_val - val));

  // save the (smoothed) reading 
  knob_vals[ seesaw_knob_i ] = val;

  seesaw_knob_i++;  // set next knob to be read
  
}

//
void setOscs() {
  
  for(int i=0; i<NUM_KNOBS; i++) {
    float note = knob_vals[i] / 8;
    float note2 = note + 12;
    Q15n16 r = Q7n8_to_Q15n16(rand(50)); // random for oscillator "drift". 
    bool is_zero = knob_vals[i] < 2;
    if( is_zero ) { // if "zero", turn oscillators off
      note = 0;
      note2 = 0;
      r = 0;
    }
    // need to fix this logic
//    if( last_knob_vals[i] != knob_vals[i] || is_zero ) { // this is_zero thing seems a hack
      if( noteMode ) {
        portamentos[i].start( (uint8_t)(note) );
        portamentos[i+8].start( (uint8_t)(note2) );
      }
      else {
        portamentos[i].start( float_to_Q16n16( note ) );
        portamentos[i+8].start( float_to_Q16n16( note2 + r ) );
      }
//    }
    last_knob_vals[i] = knob_vals[i];
  }  
}

// Mozzi function, called every CONTROL_RATE
void updateControl() {
  
  // filter range (0-255) corresponds with 0-8191Hz
  // oscillator & mods run from -128 to 127
  lpf.setCutoffFreqAndResonance(cutoff, resonance);

  #ifndef ARDUINO_ARCH_RP2040
  readKnobs();
  #endif

  int ptime = 300;
  scatterMode = false;
    
  if( isButtPressed(0) ) { 
    // don't update oscs
  } 
  else {
    setOscs();
  }

  if( isButtPressed(1) ) { 
    scatterMode = true;
    ptime = 3500;
  }
  else {
    scatterMode = false;
    // for portamento reset
    for( int i=0; i<NUM_KNOBS; i++) { last_knob_vals[i]=0; }
  }

  if( isButtPressed(3) ) { 
    noteMode = true;
  }
  else { 
    noteMode = false;
  }
  
  for(int i=0; i<NUM_VOICES; i++) {
    portamentos[i].setTime( ptime );
    Q16n16 f = portamentos[i].next();
    aOscs[i].setFreq_Q16n16(f);
  }

  if( scatterMode ) {
    if( millis() - lastScatterMillis > ptime ) {
      lastScatterMillis = millis();
      int randamt = 100;
      for(int i=0; i<NUM_VOICES; i++) {
        if( knob_vals[i] != 0 ) { 
          knob_vals[i] = knob_vals[i] + (rand(randamt) - (randamt/2));
         }
      }
    } // if scatterMillis 
  } // scatterMode
  
  // debug 
  if( millis() - lastDebugMillis > 100 ) {
    lastDebugMillis = millis();
    for( int i=0; i< NUM_KNOBS; i++) { 
      Serial.printf("%4d ", knob_vals[i]);
    }
    Serial.println(butt_vals, BIN);
  } // if millis
  
}

// mozzi function, called every AUDIO_RATE to output sample
AudioOutput_t updateAudio() {
  int16_t asig = (long) 0;
  //uint8_t b = 0; // count number of active voices
  for( int i=0; i<NUM_VOICES; i++) {
    int8_t a = aOscs[i].next();
    asig += a;
    //if(a) { b++; }
  }
  asig = lpf.next(asig);
  // how to programmitcally determine bits, 11 bits ok for 16 voices
  //if( b<4 ) {
  //  return MonoOutput::fromAlmostNBit(9, asig);
  //}
  //else if( b < 8 ) { 
  //  return MonoOutput::fromAlmostNBit(10, asig);
  //}
  // 
  return MonoOutput::fromAlmostNBit(12, asig); // should be 12 
}
