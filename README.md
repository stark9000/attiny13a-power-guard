# ATtiny13A Power Guard

A mains under/over-voltage cutoff with a start delay, built around an ATtiny13A. It keeps the load switched off until mains has been stable for a set time, and drops it again if the voltage leaves the safe band.

Inspired by the Scorpionz 12F683 Power Guard (https://scopionz.blogspot.com/2015/09/simple-power-guard-using-12f683.html). The firmware here is a fresh implementation.

> **⚠️ Not tested.** The firmware, circuit and calculations in this repo have not been tested on real hardware. Treat everything as a starting point, verify it yourself, and calibrate before trusting it with any load.

> **⚠️ Mains voltage is dangerous.** Use a fuse, a varistor (MOV), proper insulation, a suitable enclosure and correct mains clearances. Work from an isolation transformer where possible. You build and use this at your own risk.

## Features

- Relay is **off at power-up**; a green LED blinks while the guard waits
- Relay switches on only after mains has stayed in the good band for **30 s** (configurable `DELAY_S`)
- Trips if mains stays out of band for **200 ms**
- **Hysteresis** to avoid chattering at the edges
- Yellow LED = low voltage, red LED = high voltage
- 500 ms watchdog; after any reset or blackout the full start delay runs again
- Tiny footprint: about 350 bytes of flash, no timers or interrupts, 8-bit ADC only

## Thresholds (230 V mains example)

| | Low side | High side |
|---|---|---|
| Trip (relay drops) | below 200 V | above 260 V |
| Good again (countdown runs) | above 210 V | below 250 V |

## Pin map

| Pin | Function |
|---|---|
| PB0 | Relay driver |
| PB1 | Green LED (blink = waiting, solid = load on) |
| PB2 | Voltage sense (ADC1, filtered DC) |
| PB3 | Yellow LED (low voltage) |
| PB4 | Red LED (high voltage) |
| PB5 | RESET (left free for ISP programming) |

## How it works

Mains goes through a fuse into a small centre-tapped transformer (9-0-9 V in the reference circuit), a two-diode full-wave rectifier and a bulk capacitor. That DC rail feeds three things:

- a 5 V regulator (7805) that powers the ATtiny13A and also acts as the ADC reference
- a resistor divider plus 10 µF filter capacitor that goes to PB2 for voltage sensing
- the relay coil, switched by an NPN transistor from PB0

The firmware reads the sense voltage every 20 ms using the left-adjusted 8-bit ADC result and compares it with limits calculated from a single calibration constant `K` (ADC counts per mains volt × 1000). Two states are used: RUN (load on, checks the trip limits) and WAIT (load off, checks the stricter restore limits and counts down the start delay).

The full tutorial document covers the circuit, component values, transformer selection, threshold maths, relay voltage dip compensation, fuses and troubleshooting.

## Versions

- **v1**: first version, assumes a 12 V transformer and a 7805 supply, fixed scaling for 8-bit ADC counts.
- **v2**: updated for the 9-0-9 V centre-tapped transformer circuit, adds the `K` calibration constant and `DIP` relay-dip compensation (`DIP` defaults to 0, meaning no compensation).

Each version has an Arduino IDE sketch (`.ino`) and a plain AVR C version (`.c`).

## Build and upload (Arduino IDE)

1. In **File → Preferences → Additional Boards Manager URLs**, add:
   `https://mcudude.github.io/MicroCore/package_MCUdude_MicroCore_index.json`
2. **Boards Manager**: install **MicroCore**
3. **Tools**: Board = ATtiny13, Clock = 1.2 MHz internal, BOD = 4.3 V
4. **Tools → Burn Bootloader** once (with an ISP programmer this only writes the fuses)
5. **Sketch → Upload Using Programmer**

### Fuses

| Fuse | Value | Meaning |
|---|---|---|
| Low | `0x6A` | 9.6 MHz internal RC divided by 8 = 1.2 MHz, SPI programming on (use `0x7A` for 9.6 MHz) |
| High | `0xF9` | Brown-out detector at 4.3 V, reset pin kept, debugWIRE off |

Example with avrdude (adjust the programmer to yours):

```
avrdude -c usbasp -p t13 -U lfuse:w:0x6A:m -U hfuse:w:0xF9:m
```

**Never program RSTDISBL (high fuse `0xF8`).** It turns PB5 into a normal pin and you would then need a high-voltage programmer to recover the chip. Always check the fuse bytes against a fuse calculator and the datasheet before burning.

## Calibration

Calibrate once per finished board, using the real regulator and divider.

1. With normal mains and the relay on, measure the real mains voltage, the voltage at PB2 and the 5 V rail at the MCU.
2. Compute `K = 256 × Vpin / (Vcc × Vmains) × 1000` and set it in the sketch (default 640).
3. Measure the relay dip (reading with relay off vs on) and set `DIP` in per-mille (35 = 3.5 %).
4. Verify by temporarily moving a threshold close to the present mains voltage and checking that the LEDs and relay behave as expected.
5. If you have a variac, confirm the real trip points at 200 V and 260 V.

## Safety notes

- The guard reacts in about 200 ms. It protects against sags, surges and bad restores, but cannot clip fast spikes, which is what the MOV is for.
- Choose relay contacts rated for the load type (motors and compressors need a higher rating).
- Check the relay coil voltage at the highest mains you expect.

## License

Add your preferred license here.
