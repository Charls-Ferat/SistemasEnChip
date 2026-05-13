#include <avr/io.h>          //Definiciones de registros del AVR
#include <util/delay.h>
#include <avr/interrupt.h>   //Macros ISR(), sei(), cli()

// Constantes guapas
#define F_CPU   16000000UL
#define PPR     20
#define OCR1A_1S    15624   // Valor de comp para 1 seg

static void pin_setup(void) {
    DDRB |= (1 << 5);   // Pone el pin13 como digital

    // Configuracion del reloj
    TCCR1B |= (1 << WGM12);     //CTC mode
    TCCR1B |= (1 << CS12) | (1 << CS10);    // Prescaler 1024

    // Enable compare interrupt
    TIMSK1 |= (1 << OCIE1A);

    // Set compare value
    OCR1A = OCR1A_1S;
}

int main (void) {

    pin_setup();    // call setup

    // Global interrupts
    sei();

    while(1){


    }
}

// Interrupt routine
ISR(TIMER1_COMPA_vect){
    PORTB ^= (1 << 5);
}
