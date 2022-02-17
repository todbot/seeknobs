/**
 * qtpy_drone_synth.ino  -  8 knobs control (at least) 8 oscillators, knobs via seesaw
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
 *  - For slightly better audio quality, add the following after "#define PWM_RATE" in "Mozzi/AudioConfigRP2040.h"
 *      #define AUDIO_BITS 10
 *      #define AUDIO_BIAS ((uint16_t) 512)
 *      #define PWM_RATE (60000*2)
 *    
 *  Notes:
 *  - Seems to not start up sometimes? or if all the pots are set to zero?
 *  
 * 15 Feb 2022 - @todbot
 */

#define CONTROL_RATE 128 
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

#define NUM_KNOBS 8
#define NUM_VOICES 16

Adafruit_seesaw ss( &Wire ); // seeknobs I2C is std SDA/SCL
//Adafruit_seesaw ss( &Wire1 );  // StemmaQT port is on Wire1 on QTPy RP2040 

// seesaw on Attiny8x7, analog in can be 0-3, 6, 7, 18-20
uint8_t seesaw_knob_pins[ NUM_KNOBS ] = {7,6, 3,2, 1,0, 18,19};  // pinout on seeknobs3qtpy board
uint8_t seesaw_knob_i = 0;
float knob_smoothing = 0.5; // 1.0 = all old value
int knob_vals[ NUM_KNOBS ];

Oscil<SAW_ANALOGUE512_NUM_CELLS, AUDIO_RATE> aOscs [NUM_VOICES];
Oscil<COS2048_NUM_CELLS, CONTROL_RATE> kFilterMod(COS2048_DATA);
//Portamento <CONTROL_RATE> portamentos[NUM_VOICES];

LowPassFilter lpf;
uint8_t resonance = 120; // range 0-255, 255 is most resonant

uint32_t lastDebugMillis = 0; // debug
uint32_t knobUpdateMillis = 0;

// 
void setup() {
  // RP2040 defaults to GP0, from https://github.com/pschatzmann/Mozzi/
  // Mozzi.setPin(0,16); // RP2040 GP16 / Trinkey QT2040 GP16 
  // Mozzi.setPin(0,20); // RP2040 GP20 / QT Py RP2040"RX"
  Mozzi.setPin(0,29);  // this sets RP2040 GP29 / QT Py RP2040 "A0"
  
  Serial.begin(115200);

  //while (!Serial) delay(10);   // debug: wait until serial port is opened

  startMozzi();
  
  kFilterMod.setFreq(0.08f);
  lpf.setCutoffFreqAndResonance(20, resonance);
  for( int i=0; i<NUM_VOICES; i++) { 
     aOscs[i].setTable(SAW_ANALOGUE512_DATA);
  }
  //  for ( int i = 0; i < NUM_VOICES; i++) {
  //    portamentos[i].start(Q8n0_to_Q16n16(knob_vals[i]));
  //    portamentos[i].setTime(100);
  //  }

  setupKnobs();
  
  Serial.println("qtpy_drone_synth_testmulticore started");
}

//
void loop() {
  audioHook();
}

void setup1() {
  setupKnobs();  
}
void loop1() {
  readKnobs();
}

void setupKnobs() {
  if(!ss.begin()){
    Serial.println(F("seesaw not found!"));
    while(1) delay(10);
  }
}


// i2c transactions take time, so only do one at a time
void readKnobs() {
  
  const int KNOBS_PER_READ = 2;
  
  int end_i = (seesaw_knob_i + KNOBS_PER_READ) % NUM_KNOBS;
  int done = false;

  while( seesaw_knob_i != end_i ) {
    
    // get a reading
    int val = ss.analogRead( seesaw_knob_pins[seesaw_knob_i] );

    // smooth it based on last val
    int last_val = knob_vals[ seesaw_knob_i ];
    val = val + (knob_smoothing * (last_val - val));
 
    // save the (smoothed) reading 
    knob_vals[ seesaw_knob_i ] = val;
  
    // prepare for next knob to be read
    seesaw_knob_i = (seesaw_knob_i + 1) % NUM_KNOBS ;
  } 
  // debug
  //Serial.printf("    readKnobs:%d %d %d\n", seesaw_knob_i, knob_vals[0], knob_vals[1]);
}

//
void setOscs() {
  
  for(int i=0; i<NUM_KNOBS; i++) {
    aOscs[i].setFreq( knob_vals[ i ] );
  }

  for(int i=0; i<NUM_KNOBS; i++) {
    aOscs[i+8].setFreq( knob_vals[ i ] / 2 );
  }
  
   //    aOscs[i].setFreq( knob_vals[ i%NUM_KNOBS ] );
  //    aOscs[i+4].setFreq( knob_vals[ i%NUM_KNOBS ]*2 );
  // Q16n16 note = (float)(knob_vals[i] * 127 / 1023);
  //portamentos[i].start( Q8n0_to_Q16n16(note) ); // but portamentos update at CONTROL_RATE too;
  //  }
}


// Mozzi function, called every CONTROL_RATE
void updateControl() {
  // filter range (0-255) corresponds with 0-8191Hz
  // oscillator & mods run from -128 to 127

//  readKnobs();

  setOscs();

  // debug 
  if( millis() - lastDebugMillis > 100 ) {
    lastDebugMillis = millis();
    for( int i=0; i< NUM_KNOBS; i++) { 
      Serial.printf("%4d ", knob_vals[i]);
    }
    Serial.println();
  }
  
}

// mozzi function, called every AUDIO_RATE to output sample
AudioOutput_t updateAudio() {
  int16_t asig = (long) 0;
  for( int i=0; i<NUM_VOICES; i++) {
    asig += aOscs[i].next();
  }
  asig = lpf.next(asig);
  return MonoOutput::fromAlmostNBit(13, asig); // how to programmitcally determine bits, 11 bits ok for 16 voices
}
