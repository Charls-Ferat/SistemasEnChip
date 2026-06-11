#include <avr/io.h>
#include <stdio.h>
#include <Arduino_FreeRTOS.h>

// Defines & types
#define F_CPU 16000000UL
#define USART_BAUDRATE 9600
#define UBRR_VALUE (((F_CPU / (USART_BAUDRATE * 16UL))) - 1)

// Task profilers & handler
typedef int TaskProfiler;
volatile TaskProfiler TASK1Profiler = 0, TASK2Profiler = 0, TASK3Profiler = 0;
TaskHandle_t Task2Handle = NULL;

// UART config
void UART_setup() {
    // configuración del puerto serial
    UBRR0H = (uint8_t)(UBRR_VALUE >> 8);
    UBRR0L = (uint8_t)UBRR_VALUE;
    UCSR0C = 0x06;       // Set frame format: 8data, 1stop bit
    UCSR0B |= (1 << RXEN0) | (1 << TXEN0);   // TX y RX habilitados
}

// Arduino setup()
void setup() {

    // Configura el UART
    UART_setup();

    // Creacion de tareas
    xTaskCreate(vTaskProfiler1, "PROFILER1 TASK", 256, NULL, 1, NULL);
    xTaskCreate(vTaskProfiler2, "PROFILER2 TASK", 256, NULL, 1, &Task2Handle);
    xTaskCreate(vTaskProfiler3, "PROFILER3 TASK", 256, NULL, 1, NULL);

    vTaskStartScheduler();
}

// Funciones de transmision
// Chars
static inline void UART_sendChar(char c) {
    // Esperar buffer vacio
    while (!(UCSR0A & (1 << UDRE0)));
    // Enviar caracter
    UDR0 = c;
}
// Strings
static inline void UART_sendString(const char *str) {
    while (*str) {
        UART_sendChar(*str++);
    }
}
// Enteros
static inline void UART_sendInt(int n) {
    char buf[6];
    itoa(n, buf, 10);
    UART_sendString(buf);
}

// Tasks
void vTaskProfiler1(void* pvParameters) {

    while (1) {
        TASK1Profiler++;    // Incrementa el profiler

        // Envia el string por UART
        UART_sendString("T1,Prof: ");
        UART_sendInt(TASK1Profiler);
        UART_sendString("\n");

        if (TASK1Profiler == 10) {
            UART_sendString("S2\n");
            vTaskDelay(2);
            vTaskSuspend(Task2Handle);
        }

        vTaskDelay(1000 / portTICK_PERIOD_MS);     // Delay de 1000ms
    }
}

void vTaskProfiler2(void* pvParameters) {

    while (1) {
        TASK2Profiler++;    // Incrementa el profiler

        // Envia el string por UART
        UART_sendString("T2,Prof: ");
        UART_sendInt(TASK2Profiler);
        UART_sendString("\n");

        vTaskDelay(1000 / portTICK_PERIOD_MS);    // Delay
    }
}

void vTaskProfiler3(void* pvParameters) {

    while (1) {
        TASK3Profiler++;    // Incrementa el profiler

        // Envia el string por UART
        UART_sendString("T3,Prof: ");
        UART_sendInt(TASK3Profiler);
        UART_sendString("\n");

        if (TASK3Profiler == 20) {
            UART_sendString("R2\n");
            vTaskDelay(2);
            vTaskResume(Task2Handle);
        }

        vTaskDelay(1000 / portTICK_PERIOD_MS);    // Delay
    }
}


// Arduino loop()
void loop() {
    /*Vacio*/
}



int x = 3;
int y = 4;

int z = x * y + 2;
