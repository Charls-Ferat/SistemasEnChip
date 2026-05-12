#include <avr/io.h>
#include <util/delay.h>

#define F_CPU 16000000UL
#define BAUD 9600
#define UBRR_VALUE ((F_CPU / 16 / BAUD) - 1)

void UART_init() {
    // Baud rate
    UBRR0H = (UBRR_VALUE >> 8);
    UBRR0L = UBRR_VALUE;

    // Habilitar transmisión
    UCSR0B = (1 << TXEN0);

    // 8 bits, 1 stop bit
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
}

void UART_sendChar(char c) {
    // Esperar buffer vacío
    while (!(UCSR0A & (1 << UDRE0)));

    // Enviar carácter
    UDR0 = c;
}

void UART_sendString(const char *str) {
    while (*str) {
        UART_sendChar(*str++);
    }
}

int main(void) {
    UART_init();

    while (1) {
        UART_sendString("Mensaje 1\r\n");
        UART_sendString("Mensaje 2\r\n");
        UART_sendString("Mensaje 3\r\n");
        UART_sendString("Mensaje 4\r\n");
        UART_sendString("Mensaje 5\r\n");
        UART_sendString("Mensaje 6\r\n");
        UART_sendString("Mensaje 7\r\n");

        _delay_ms(5000);
    }
}
