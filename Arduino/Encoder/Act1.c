#include <avr/io.h>          //Definiciones de registros del AVR
#include <avr/interrupt.h>   //Macros ISR(), sei(), cli()

// Constantes guapas
#define PPR         20          //Pulsos Por Revolución del encoder
#define OCR1A_1S    15624       // Valor de comparación para periodo de 1 s

// variables
volatile uint32_t pulse_count = 0;   // Pulsos acumulados
volatile uint32_t rpm         = 0;   // RPM calculadas (actualizadas cada 1 s)

// Funciones
static void INT0_Init(void);
static void TIMER1_Init(void);


static void INT0_Init(void) {
    DDRD  &= ~(1 << DDD2);      // PD2 como entrada - hardware interrupts
    PORTD |=  (1 << PORTD2);    // Activar pull-up interno en PD2
    EICRA |=  (1 << ISC01) | (1 << ISC00);  // Flanco de subida: ISC01=1, ISC00=1
    EIMSK |=  (1 << INT0);                  // Habilitar INT0 en la máscara de interrupciones
}

// Timer a 1 seg
static void TIMER1_Init(void) {
    TCCR1A = 0x00;
    TCCR1B = (1 << WGM12) | (1 << CS12) | (1 << CS10);  // CTC, Preescaler a 1024
    OCR1A = OCR1A_1S;       // Valor de comparacion
    TIMSK1 = (1 << OCIE1A); // Habilitar interrupción por comparación A del Timer1
}

// ISR
// Subrutina de conteo - Interrumpe el encoder
ISR(INT0_vect) {
    pulse_count++;
}

// Subrutina del timer - Procesa cada 1 seg
ISR(TIMER1_COMPA_vect) {
    uint32_t pulses;

    // Lectura atómica MUAJAJ
    cli();
    pulses = pulse_count;
    pulse_count = 0;
    sei();

    // Fórmula de RPM
    rpm = (pulses * 60UL) / PPR;
}


int main(void) {     // Poderosisimo main
    INT0_Init();     // Interrupción externa del encoder
    TIMER1_Init();   // Configurar Timer1

    sei();          // Enablea el interrupt

    // el void loop jaja
    while (1) {
        /*
         * Aqui se podrian hacer cositas supongo
         */
    }

    return 0;   // Sino no compila
}
