//
// 
// 

//typedef struct {
//        uint16_t msecs;
//        int val;
//} knob_event;

uint32_t knobUpdateMillis = 0;

const int max_events = 200;
int knob_events[NUM_KNOBS][max_events];
int event_pos = 0;
uint32_t lastEventMillis = 0;
uint16_t eventMillis = 50;

Adafruit_seesaw ss( &Wire ); // seeknobs I2C is std SDA/SCL (&Wire), StemmaQT port on Wire1 on QTPy RP2040 (&Wire1) 
seesaw_NeoPixel ledsss = seesaw_NeoPixel(8, 8, NEO_GRB + NEO_KHZ800, &Wire);

// seesaw on Attiny8x7, analog in can be 0-3, 6, 7, 18-20
uint8_t seesaw_knob_pins[ NUM_KNOBS ] = {7,6, 3,2, 1,0, 19,18};  // pinout on seeknobs3qtpy board
uint8_t seesaw_butt_pins[ NUM_BUTTS ] = { 5, 9, 13, 14 }; // not 17!  this var not used currently
uint32_t seesaw_butt_mask = ((uint32_t) (1<<5) | (1<<9) | (1<<13) | (1<<14)) ; // fixme?
uint8_t seesaw_knob_i = 0;

float knob_smoothing = 0.3; // 1.0 = all old value

int knob_vals[ NUM_KNOBS ];
int last_knob_vals[ NUM_KNOBS ];
uint32_t butt_vals = 0;  // bitfield, see butt_mask
uint32_t last_butt_vals = 0;

void setupKnobsAndButtons() {
  if(!ss.begin()){
    Serial.println(F("seesaw not found!"));
    while(1) delay(10);
  }
  if(!ledsss.begin()){
    Serial.println("seesaw pixels not found!");
    while(1) delay(10);
  }

  uint32_t version = ((ss.getVersion() >> 16) & 0xFFFF);
  Serial.print("seesaw version:"); Serial.println(version);
  
  uint16_t pid;
  uint8_t year, mon, day;
  ss.getProdDatecode(&pid, &year, &mon, &day);
  Serial.print("seesaw found PID: ");
  Serial.print(pid);
  Serial.print(" datecode: ");
  Serial.print(2000+year); Serial.print("/");
  Serial.print(mon); Serial.print("/");
  Serial.println(day);

  ledsss.setBrightness(32);
  ledsss.setPixelColor(0, 0xff00ff);
  ledsss.setPixelColor(2, 0x00ffff);
  ledsss.show();

  ss.pinModeBulk(seesaw_butt_mask, INPUT_PULLUP);
  
}

// Input a value 0 to 255 to get a color value.
// The colours are a transition r - g - b - back to r.
uint32_t colorwheel(byte WheelPos) {
  WheelPos = 255 - WheelPos;
  if(WheelPos < 85) {
    return seesaw_NeoPixel::Color(255 - WheelPos * 3, 0, WheelPos * 3);
  }
  if(WheelPos < 170) {
    WheelPos -= 85;
    return seesaw_NeoPixel::Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
  WheelPos -= 170;
  return seesaw_NeoPixel::Color(WheelPos * 3, 255 - WheelPos * 3, 0);
}

// read the knobs & buttons from seesaw
// i2c transactions take time, so only do one at a time
// uses global "knob_smoothing"
void readKnobsAndButtons() {
  last_butt_vals = butt_vals;  // save last button state

  // if we've read all knobs, do buttons and get out
  if( seesaw_knob_i == NUM_KNOBS ) { 
    butt_vals = ss.digitalReadBulk(seesaw_butt_mask);
    seesaw_knob_i = 0;
    
    for( int n=0; n<NUM_KNOBS; n++) {
     ledsss.setPixelColor(n, colorwheel(knob_vals[n] / 4) );
    }
    ledsss.show();
    
    return;
  }

  // else, read a knob
  int val = ss.analogRead( seesaw_knob_pins[seesaw_knob_i] );

  // smooth it based on last val
  int last_val = knob_vals[ seesaw_knob_i ];
  val = val + (knob_smoothing * (last_val - val));

  // save the (smoothed) reading
  knob_vals[ seesaw_knob_i ] = val;
  
  seesaw_knob_i++;  // set next knob to be read
}

// buttid is 0,1,2,3 for buttons 1,2,3,4
bool buttonJustPressed(uint8_t buttid) {
   uint8_t pin = seesaw_butt_pins[buttid];
   bool ispressed = !(butt_vals & (1<<pin));
   bool lastpressed = !(last_butt_vals & (1<<pin));
   return ispressed && lastpressed != ispressed;
}

//
bool buttonJustReleased(uint8_t buttid) {
   uint8_t pin = seesaw_butt_pins[buttid];
   bool ispressed = !(butt_vals & (1<<pin));
   bool lastpressed = !(last_butt_vals & (1<<pin));
   return !ispressed && lastpressed != ispressed;
}

// buttid is 0,1,2,3 for buttons 1,2,3,4
bool buttonPressed(uint8_t buttid) {
   uint8_t pin = seesaw_butt_pins[buttid];
   return !(butt_vals & (1<<pin));
}

//
bool buttonLastPressed(uint8_t buttid) {
   uint8_t pin = seesaw_butt_pins[buttid];
   return !(last_butt_vals & (1<<pin));
}
