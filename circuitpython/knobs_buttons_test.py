# knobs_buttons_test.py -- test seeknobs board in circuitpython
#  27 Apr 2022 - @todbot / Tod Kurt
# Unfortunately this is too slow to be usable
#
import time
import board,busio
import rainbowio 
from adafruit_seesaw import seesaw, neopixel

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
knob_vals = [0] * 8  # holder for all knob positions
button_vals = 0  # bitmask for now
knob_i = 0  # which knob we're currenly working on

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
    print(f'  0b{button_vals:08.8b}')

last_time = 0
while True:
    st = time.monotonic()
    update_knobs_and_buttons()
    if time.monotonic() - last_time > 0.1:
        last_time = time.monotonic()
        print("update time: %2d:" % int( (time.monotonic()-st)*1000),end='')  # == ~14 ms on RP2040
        print_knobs_and_buttons()
    #time.sleep(0.001) # emulate work being done elsewhere
