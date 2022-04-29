/**
 *
 */

#include <Wire.h>
#include <Adafruit_seesaw.h>
#include <seesaw_neopixel.h>

#define NUM_KNOBS 8
#define NUM_BUTTS 4

#include "buttons_and_knobs.h"

uint32_t lastDebugMillis = 0; // debug

// 
void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);   // DEBUG wait until serial port is opened
 
  setupKnobsAndButtons();
   
  Serial.println("qtpy_knob_test started");
}

//
void loop() {
  //delay(10);
  
  readKnobsAndButtons();

  if( buttonJustPressed(0) ) {
    Serial.println("button 1 JUST PRESSED");
  }
  if( buttonJustReleased(0) ) {
    Serial.println("button 1 JUST RELEASED");
  }
  if( buttonJustPressed(1) ) { 
    Serial.println("button 2 JUST PRESSED");
  }
  if( buttonJustReleased(1) ) {
    Serial.println("button 2 JUST RELEASSED");
  }
  if( buttonJustPressed(2) ) { 
    Serial.println("button 3 JUST PRESSED");
  }
  if( buttonJustReleased(2) ) {
    Serial.println("button 3 JUST RELEASSED");
  }
  if( buttonJustPressed(3) ) { 
    Serial.println("button 4 JUST PRESSED");
  }
  if( buttonJustReleased(3) ) {
    Serial.println("button 4 JUST RELEASSED");
  }
  
  // debug 
  if( millis() - lastDebugMillis > 100 ) {
    lastDebugMillis = millis();
    Serial.print("knobs:");
    for( int i=0; i< NUM_KNOBS; i++) { 
      Serial.printf("%4d ", knob_vals[i]);
    }
    Serial.println(butt_vals, BIN);
  } // if millis
  
}
