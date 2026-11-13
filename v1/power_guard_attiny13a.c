/*
 * Power Guard - ATtiny13A
 * -----------------------------------------------------------------------
 * Disconnects the load when mains voltage is too low or too high, and
 * reconnects it only after the voltage has stayed good for START_DELAY_S.
 *
 * Hardware assumptions
 *   - 12 V transformer -> rectifier -> filter cap -> divider -> PB2.
 *     The sense voltage must be smoothed DC (about 3 V at 240 V mains).
 *   - MCU powered from a regulated 5 V (7805); VCC is the ADC reference.
 *   - Active-high outputs. Put a ~10k pull-down on the relay driver base/gate
 *     so the relay stays off while the MCU is in reset.
 *
 * Pins
 *   PB0  relay driver        PB1  green LED (blink = waiting, solid = on)
 *   PB2  voltage sense (ADC1)  PB3  yellow LED (low voltage)
 *   PB4  red LED (high voltage) PB5  RESET (left free for ISP)
 *
 * Fuses (factory default clock: 9.6 MHz RC / 8 = 1.2 MHz)
 *   Set the brown-out detector to 4.3 V so the chip resets (relay off)
 *   when the 5 V rail sags. Verify the exact fuse bytes with a fuse
 *   calculator for your programmer before burning.
 *
 * Calibration
 *   Thresholds are in volts of mains and converted at compile time using
 *   ADC_PER_VOLT_X10000. Default 25575 matches the original design:
 *       ADC = V * 12 * 1023 / (240 * 5 * 4)
 *   Trim the constant per board if your divider/transformer differ.
 *
 * Build:  avr-gcc -mmcu=attiny13a -Os -o pg.elf power_guard_attiny13a.c
 *         avr-objcopy -O ihex -R .eeprom pg.elf pg.hex
 * -----------------------------------------------------------------------
 */

#ifndef F_CPU
#define F_CPU 1200000UL
#endif

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <stdint.h>

/* ---------------- user settings ---------------- */
#define START_DELAY_S        30      /* good voltage needed this long (max 650) */
#define V_LOW_TRIP           200     /* cut off below this (V)                  */
#define V_LOW_RESTORE        210     /* must rise above this to be "good" (V)   */
#define V_HIGH_RESTORE       250     /* must fall below this to be "good" (V)   */
#define V_HIGH_TRIP          260     /* cut off above this (V)                  */
#define TRIP_DEBOUNCE_MS     200     /* out-of-band this long before trip (max 2550) */
#define ADC_PER_VOLT_X10000  25575UL
/* ------------------------------------------------ */

#if !(V_LOW_TRIP < V_LOW_RESTORE && V_LOW_RESTORE < V_HIGH_RESTORE && V_HIGH_RESTORE < V_HIGH_TRIP)
#error "Thresholds must satisfy LOW_TRIP < LOW_RESTORE < HIGH_RESTORE < HIGH_TRIP"
#endif
#if START_DELAY_S > 650
#error "START_DELAY_S too large"
#endif
#if TRIP_DEBOUNCE_MS > 2550
#error "TRIP_DEBOUNCE_MS too large"
#endif

#define V2ADC(v) ((uint16_t)(((uint32_t)(v) * ADC_PER_VOLT_X10000) / 10000UL))

#define ADC_LOW_TRIP     V2ADC(V_LOW_TRIP)
#define ADC_LOW_RESTORE  V2ADC(V_LOW_RESTORE)
#define ADC_HIGH_RESTORE V2ADC(V_HIGH_RESTORE)
#define ADC_HIGH_TRIP    V2ADC(V_HIGH_TRIP)

#define TICKS_PER_SEC    100U                         /* 10 ms tick */
#define START_DELAY_TICKS ((uint16_t)(START_DELAY_S * TICKS_PER_SEC))
#define TRIP_TICKS       ((uint8_t)(TRIP_DEBOUNCE_MS / 10))
#define BLINK_TICKS      50U                          /* 500 ms toggle */

#define PIN_RELAY PB0
#define PIN_GREEN PB1
#define PIN_YELLOW PB3
#define PIN_RED   PB4

/* ADC clock should be 50..200 kHz; Timer0 gives a ~100 Hz tick */
#if F_CPU == 1200000UL
  #define ADC_PRESCALE ((1 << ADPS1) | (1 << ADPS0))          /* /8  -> 150 kHz */
  #define T0_PRESCALE  ((1 << CS01) | (1 << CS00))            /* /64            */
  #define T0_TOP       187                                    /* ~99.7 Hz       */
#elif F_CPU == 9600000UL
  #define ADC_PRESCALE ((1 << ADPS2) | (1 << ADPS1))          /* /64 -> 150 kHz */
  #define T0_PRESCALE  ((1 << CS02) | (1 << CS00))            /* /1024          */
  #define T0_TOP       93                                     /* ~99.7 Hz       */
#else
  #error "Unsupported F_CPU: use 1.2 MHz or 9.6 MHz"
#endif

enum { ST_WAIT, ST_RUN };

static volatile uint8_t tick;

ISR(TIM0_COMPA_vect)
{
    tick = 1;
}

static uint16_t adc_read(void)
{
    ADCSRA |= (1 << ADSC);
    while (ADCSRA & (1 << ADSC))
        ;
    return ADC;
}

int main(void)
{
    uint8_t  state = ST_WAIT;
    uint8_t  bad = 0;
    uint8_t  blink = 0;
    uint16_t remaining = START_DELAY_TICKS;
    uint16_t acc = 0;
    uint16_t v, i;

    wdt_reset();
    MCUSR = 0;
    wdt_enable(WDTO_500MS);

    /* outputs: relay and LEDs, all off at start */
    DDRB  = (1 << PIN_RELAY) | (1 << PIN_GREEN) | (1 << PIN_YELLOW) | (1 << PIN_RED);
    PORTB = 0;

    /* ADC: VCC reference, ADC1 (PB2), right-adjusted */
    ADMUX  = (1 << MUX0);
    DIDR0  = (1 << ADC1D);
    ADCSRA = (1 << ADEN) | ADC_PRESCALE;
    (void)adc_read();                       /* discard first conversion */
    for (i = 0; i < 8; i++)                 /* prefill the filter */
        acc += adc_read();

    /* Timer0: CTC, 10 ms tick */
    TCCR0A = (1 << WGM01);
    OCR0A  = T0_TOP;
    TIMSK0 = (1 << OCIE0A);
    TCCR0B = T0_PRESCALE;
    sei();

    for (;;) {
        while (!tick)
            ;
        tick = 0;
        wdt_reset();

        /* smoothed reading: IIR filter, time constant ~80 ms */
        acc = acc - (acc >> 3) + adc_read();
        v = acc >> 3;

        if (state == ST_RUN) {
            if (v < ADC_LOW_TRIP || v > ADC_HIGH_TRIP) {
                if (++bad >= TRIP_TICKS) {
                    PORTB &= ~((1 << PIN_RELAY) | (1 << PIN_GREEN));   /* load off */
                    remaining = START_DELAY_TICKS;
                    blink = 0;
                    state = ST_WAIT;
                }
            } else {
                bad = 0;
            }
        } else { /* ST_WAIT */
            uint8_t low  = (v < ADC_LOW_RESTORE);
            uint8_t high = (v > ADC_HIGH_RESTORE);
            uint8_t good = !(low || high);

            /* one fault LED at a time */
            if (low)       { PORTB |=  (1 << PIN_YELLOW); PORTB &= ~(1 << PIN_RED);    }
            else if (high) { PORTB |=  (1 << PIN_RED);    PORTB &= ~(1 << PIN_YELLOW); }
            else           { PORTB &= ~((1 << PIN_YELLOW) | (1 << PIN_RED)); }

            if (!good)           remaining = START_DELAY_TICKS;  /* restart countdown */
            else if (remaining)  remaining--;

            if (++blink >= BLINK_TICKS) {
                blink = 0;
                PORTB ^= (1 << PIN_GREEN);
            }

            if (good && remaining == 0) {
                PORTB |= (1 << PIN_RELAY) | (1 << PIN_GREEN);    /* load on */
                bad = 0;
                state = ST_RUN;
            }
        }
    }
}
