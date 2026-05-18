/*
 * Proyecto Integrador Modulo 2 - Parte B
 * Equipo: Carlos A. Ferat, Carlos Leon, Vian Gamiño
 * En esencia el codigo se interrumpe cada que hace falta,
 * con la peculiaridad que no aumenta el conteo hasta pasados
 * ciertos milisegundos para "limpiar" la señal
 * Ya que el motor jamas pasara de las 1000 RPM, fue lo mejor
 * posible para limpiar la señal del sensor, aunque hubo
 * que implementar un segundo timer
 */

/*
 * Calculo para el registro configurador del USART
 *
 * El UBRR0
 *
 * Se definio como UBRR0 = (F_CPU / (16 * BAUD)) - 1
 *
 * Donde F_CPU se refiere a la frequencia del CPU
 * D_CPU = 16,000,000 Hz
 * y BAUD se refiere a los baudios con los que se desea transmitir
 * BAUD = 9600 - Dados por la actividad
 *
 * El datasheet prove la formula donde al final sustituyendo queda:
 * UBRR0 = (16,000,000 / (16 * 9600)) - 1
 * UBRR0 ≈ 103
 */

#include <avr/io.h>          // Definiciones de registros del AVR
#include <avr/interrupt.h>   // Macros ISR(), sei(), cli()
#include <stdlib.h>          // La libreria estandar de C

// Constantes - sustituidas al compilar
// UART
#define F_CPU 16000000UL                        // Velocidad del reloj
#define BAUD 9600                               // Baudios
#define UBRR_VALUE ((F_CPU / 16 / BAUD) - 1)    // Usart Baud Rate - Baud prescaler para el UART
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

// Declaracion de Funciones
static void INT0_Init(void);
static void TIMER0_Init(void);
static void TIMER1_Init(void);
static void UART_init(void);
static void LED_Init(void);
void UART_sendChar(char c);
void UART_sendString(const char *str);


/*
 * Funciones de configuracion de registros
 * Sustituyen el void setup() {}
 */

// Configuracion del Puerto 2 para el encoder
static void INT0_Init(void) {
    DDRD  &= ~(1 << DDD2);      // PD2 como entrada - hardware interrupts
    PORTD |=  (1 << PORTD2);    // Activar pull-up interno en PD2
    EICRA |=  (1 << ISC01) | (1 << ISC00);  // Flanco de subida: ISC01=1, ISC00=1
    EIMSK |=  (1 << INT0);                  // Habilitar INT0 en la mascara de interrupciones
}
// Timer a 1 seg
static void TIMER1_Init(void) {
    TCCR1A = 0x00;
    TCCR1B = (1 << WGM12) | (1 << CS12) | (1 << CS10);  // CTC, Preescaler a 1024
    OCR1A = OCR1A_1S;       // Valor de comparacion
    TIMSK1 = (1 << OCIE1A); // Habilitar interrupción por comparación A del Timer1
}
// Timer con el global counter
static void TIMER0_Init(void) {
    TCCR0A = (1 << WGM01);
    TCCR0B = (1 << CS01) | (1 << CS00); // CTC, Preescaler a 64
    OCR0A = OCR0A_1S;                // Compara a 1 mili segundo
    TIMSK0 |= (1 << OCIE0A);    // Habilitar interrupcion por comparacion
}
//Configuracion del UART
static void UART_init(void) {
    // Baud rate
    UBRR0H = (UBRR_VALUE >> 8);
    UBRR0L = UBRR_VALUE;

    // Habilitar transmision y recepcion
    UCSR0B = (1 << TXEN0) | (1 << RXEN0) | (1 << RXCIE0);

    // 8 bits, 1 stop bit
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
}
// Configuracion del blinking led
static void LED_Init(void) {
    DDRB |= (1 << DDB5);    // Usa el led del arduino
}


/*
 * Funciones para enviar mensajes del UART
 */

// Chars
void UART_sendChar(char c) {
    // Esperar buffer vacio
    while (!(UCSR0A & (1 << UDRE0)));
    // Enviar caracter
    UDR0 = c;
}
// Strings
void UART_sendString(const char *str) {
    while (*str) {
        UART_sendChar(*str++);
    }
}

/*
 * ISRs
 * Definicion de las multiples interrupciones del procesador
 * Causadas por: Pin fisico (encoder), Timer(cada 1s), UART(recibe 'V')
 */

// Subrutina de conteo - Interrumpe el encoder
ISR(INT0_vect) {
    uint32_t now;
    now = millis_counter;   // Pulso actual con referencia al timer global
    // Filtrado de pulsos validos - Elimina ruido
    if ((now - last_pulse_time) >= DEBOUNCE_MS) {   // No bloqueante (creemos)
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
// Subrutina del UART - Interrupcion por UART
ISR(USART_RX_vect) {
    char recieved = UDR0;   // Mensaje recibido
    // Comprueba recepcion de una 'v' o 'V'
    if (recieved == 'V' || recieved == 'v') {
        char buffer[32];

        ultoa(rpm, buffer, 10);             // Convierte el calculo a ASCII
        UART_sendString("RPM actuales: ");  // Envia el ASCII de las RPM
        UART_sendString(buffer);
        UART_sendString("\r\n");
    }
    else UART_sendString("Comando no reconocido\r\n");  // Por si falla
}


/*
 * Funcion main()
 * Declara la incilizacion de registros
 * Y el loop principal del programa
 * Usa un while(true)
 * Para indicar el uso de interrupciones tiene
 *  un LED parpadeante
 */

int main(void) {    // Poderosisimo main
    // Inicializacion de perifericos
    INT0_Init();
    TIMER0_Init();
    TIMER1_Init();
    UART_init();
    LED_Init();

    // Habilita el interrupt
    sei();

    // El "void loop"
    while (1) {
        // Parpadea el LED
        PORTB ^= (1 << PORTB5);
        /*
         * Espera un par de instrucciones
         * Por lo menos un contador de 200k instrucciones(suponemos)
         * Un delay muy rudimentario - no bloqueante
         * Muestra que el procesador no hace "polling"
         */
        for (volatile uint32_t i = 0; i < 200000; i++);


    }

    return 0;   // Sino no compila
}
