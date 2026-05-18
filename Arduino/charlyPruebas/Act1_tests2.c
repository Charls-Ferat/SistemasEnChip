#include <avr/io.h>          //Definiciones de registros del AVR
#include <avr/interrupt.h>   //Macros ISR(), sei(), cli()
#include <stdlib.h>

// Constantes - sustituidas al compilar
// Encoder
#define PPR         4       // Pulsos Por Revolución del encoder
#define OCR1A_1S    15624   // Valor de comparación para periodo de 1 s
#define OCR0A_1S    249     // Valor de comparación para periodo de 1 ms
#define DEBOUNCE_MS    3    // Tiempo minimo entre pulsos validos

// variables
volatile uint32_t pulse_count = 0;      // Pulsos acumulados
volatile uint32_t rpm         = 0;      // RPM calculadas (actualizadas cada 1 s)
volatile uint32_t millis_counter = 0;   // Contador de tiempo global
volatile uint32_t last_pulse_time = 0;  // Ultimo pulso con referencia al global

// Funciones
static void INT0_Init(void);
static void TIMER0_Init(void);
static void TIMER1_Init(void);

// Configura el Puerto 2 para el encoder
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

// Otro timer
static void TIMER0_Init(void) {
    TCCR0A = (1 << WGM01);
    TCCR0B = (1 << CS01) | (1 << CS00); // CTC, Preescaler a 64
    OCR0A = OCR0A_1S;                // Compara a 1 mili segundo
    TIMSK0 |= (1 << OCIE0A);    // Habilitar interrupcion por comparacion
}

// ISR
// Subrutina de conteo - Interrumpe el encoder
ISR(INT0_vect) {
    uint32_t now;
    now = millis_counter;   // Pulso actual con referencia al timer global
    // Filtrado de pulsos validos - Elimina ruido
    if ((now - last_pulse_time) >= DEBOUNCE_MS) {
        // Aumenta conteo de pulsos
        pulse_count++;
        // Ultimo pulso valido con referencia al timer global
        last_pulse_time = now;
    }
}

// Subrutina del "timer global"
ISR(TIMER0_COMPA_vect) {
    millis_counter++;
}

// Subrutina del timer - Procesa cada 1 seg
ISR(TIMER1_COMPA_vect) {
    uint32_t pulses;
    pulses = pulse_count;   // Variable local con el conteo
    pulse_count = 0;        // Resetea la global con el conteo

    // Calculo de las revs por minuto
    rpm = (pulses * 60UL) / PPR;
}


// Poderosisimo main
int main(void) {
    INT0_Init();     // Inicializacion de perifericos
    TIMER0_Init();
    TIMER1_Init();

    sei();          // Habilita el interrupt

    // el void loop jaja
    while (1) {
        /*
         * VACIO
         */
    }

    return 0;   // Sino no compila
}
