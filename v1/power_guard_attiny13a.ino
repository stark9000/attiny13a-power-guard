// Power Guard - ATtiny13A (MicroCore core, Arduino IDE)
// Tools: Board ATtiny13, Clock 1.2 MHz internal, BOD 4.3V -> Burn Bootloader once.
// PB0 relay | PB1 green | PB2 sense (ADC1, filtered DC) | PB3 yellow (low) | PB4 red (high)
// 5 V from a 7805 is the ADC reference. Put ~10k pull-down on the relay driver.
#include <avr/wdt.h>
#include <util/delay.h>

#define V(x)    ((x) * 64 / 100)   // mains volts -> 8-bit ADC count (12V xfmr, 5V ref)
#define DELAY_S 30                 // good voltage needed this long before load is on

uint8_t run, bad, blink;
uint16_t wait = DELAY_S * 50;      // 20 ms ticks

void setup() {
  wdt_reset(); MCUSR = 0; wdt_enable(WDTO_500MS);
  DDRB   = 0b00011011;                       // PB0,1,3,4 outputs
  ADMUX  = _BV(ADLAR) | _BV(MUX0);           // VCC ref, ADC1, read ADCH only
  DIDR0  = _BV(ADC1D);
  ADCSRA = _BV(ADEN) | (F_CPU > 2000000UL ? 6 : 3);   // ADC clock ~150 kHz
}

void loop() {
  _delay_ms(20);
  wdt_reset();
  ADCSRA |= _BV(ADSC);
  while (ADCSRA & _BV(ADSC));
  uint8_t v = ADCH;

  if (run) {                                 // load on: trip on 200 ms out of band
    if (v < V(200) || v > V(260)) {
      if (++bad > 9) { PORTB = 0; run = 0; wait = DELAY_S * 50; blink = 0; }
    } else bad = 0;
  } else {                                   // waiting: voltage must be good for DELAY_S
    uint8_t lo = v < V(210), hi = v > V(250);
    PORTB = (PORTB & _BV(PB1)) | lo << PB3 | hi << PB4;
    if (lo | hi) wait = DELAY_S * 50; else if (wait) wait--;
    if (++blink > 24) { blink = 0; PORTB ^= _BV(PB1); }
    if (!(lo | hi) && !wait) { PORTB = 3; run = 1; bad = 0; }   // relay + green on
  }
}
