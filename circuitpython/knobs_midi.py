# knobs_midi.py -- Send on MIDI CCs with the knobs
#  30 Apr 2022 - @todbot / Tod Kurt
# Just barely usable, but usable!
#
import time
import board,busio
import rainbowio 
from adafruit_seesaw import seesaw, neopixel

import usb_midi
import adafruit_midi
from adafruit_midi.control_change   import ControlChange

# setup Seesaw
#i2c = busio.I2C(scl=board.SCL1,sda=board.SDA1,frequency=400_000)
i2c = board.STEMMA_I2C()  # seems frequency can't be changed 
ss = seesaw.Seesaw(i2c)

# setup LEDs 
leds = neopixel.NeoPixel(ss, 8, 8) # pinout on seeknobs3qtpy board
leds.brightness = 0.2

# setup knobs & buttons 
knob_pins = (7,6, 3,2, 1,0, 19,18) # pinout on seeknobs3qtpy board
button_mask = (1<<5) | (1<<9) | (1<<13) | (1<<14) # pinout on seeknobs3qtpy board
ss.pin_mode_bulk(button_mask, ss.INPUT_PULLUP)
knob_vals = [0] * len(knob_pins)  # holder for all knob positions
button_vals = 0  # bitmask for now
knob_i = 0  # which knob we're currenly working on

# setup usb midi
cc_nums = ( 21, 22, 23, 24, 25, 26, 27, 28 )  # which CCs to send on
last_cc_vals = [0] * len(knob_vals)  # range from 0-127
midi = adafruit_midi.MIDI(midi_out=usb_midi.ports[1])

# seesaw i2c transactions are slow, so only do one each update
def update_knobs_and_buttons():
    global knob_i, button_vals
    if knob_i == len(knob_pins):  # read all knobs, now read buttons
        button_vals = ss.digital_read_bulk(button_mask, delay=0)
        knob_i = 0
    else:
        val = ss.analog_read(knob_pins[knob_i], delay=0)
        knob_vals[knob_i] = val
        leds[knob_i] = rainbowio.colorwheel(val // 4)  # temp visual feedback
        knob_i = knob_i+1

def print_knobs_and_buttons():
    for i in range(len(knob_pins)):
        print(f'{knob_vals[i]:4d} ', end='')
    print(f'  0b{button_vals:016b}')

# work on "current" knob to minize work done here too
def send_midi_ccs():
    if knob_i == len(knob_pins): return  # invalid value for knobs
    cc_num = cc_nums[ knob_i ] # get current knob
    cc_val = knob_vals[ knob_i ] // 8 # map to 0-127
    if cc_val != last_cc_vals[ knob_i ]: # if different, send MIDI CC!
        last_cc_vals[knob_i] = cc_val
        midi.send(ControlChange(cc_num, cc_val))
    
last_time = 0
while True:
    st = time.monotonic()
    update_knobs_and_buttons()
    send_midi_ccs()
    if time.monotonic() - last_time > 0.1:
        last_time = time.monotonic()
        print("update time: %2d:" % int( (time.monotonic()-st)*1000),end='')  # == ~14 ms on RP2040
        print_knobs_and_buttons()
    #time.sleep(0.001) # emulate work being done elsewhere

