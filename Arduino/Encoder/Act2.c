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
#define PPR         20          // Pulsos Por Revolucon del encoder
#define OCR1A_1S    15624       // Valor de comparacion para periodo de 1s

// variables
volatile uint32_t pulse_count = 0;   // Pulsos acumulados
volatile uint32_t rpm         = 0;   // RPM calculadas (actualizadas cada 1 s)

// Declaracion de funciones
static void INT0_Init(void);
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

// Subrutina de conteo - Interrupcion por el encoder
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
// Subrutina del UART - Interrupcion por UART
ISR(USART_RX_vect) {
    char recieved = UDR0;   // Mensaje recibido
    // Comprueba recepcion de una 'v' o 'V'
    if (received == 'V' || received == 'v') {
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
    INT0_Init();    // Interrupción externa del encoder
    TIMER1_Init();  // Configurar Timer1
    UART_init();    // Configura el UART
    LED_Init(void); // Configura el LED

    sei();          // Enablea el interrupt

    // el void loop jaja
    while (1) {
        // Parpadea el LED
        PORTB ^= (1 << PORTB5);
        // Espera un par de instrucciones
        // Por lo menos un contador de 200k instrucciones(suponemos)
        // Un delay muy rudimentario
        for (volatile uint32_t i = 0; i < 200'000; i++);
    }

    return 0;   // Sino no compila
}
