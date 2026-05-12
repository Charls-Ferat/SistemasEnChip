/**
 * @file    main.c
 * @brief   Lectura de encoder incremental y cálculo de RPM
 *          mediante interrupciones en ATmega328P (Bare-Metal).
 *
 * Hardware:
 *   - MCU  : ATmega328P @ 16 MHz
 *   - Encoder conectado a PD2 (INT0)
 *   - PPR  : 20 pulsos por revolución
 *
 * Lógica general:
 *   1. INT0 captura cada flanco de subida del encoder → incrementa pulse_count.
 *   2. Timer1 en modo CTC genera una interrupción exacta cada 1 segundo.
 *   3. En la ISR de Timer1 se calcula RPM = (pulsos * 60) / PPR y se reinicia
 *      el contador de pulsos.
 *
 * Compilación (ejemplo):
 *   avr-gcc -mmcu=atmega328p -DF_CPU=16000000UL -O2 -o main.elf main.c
 *   avr-objcopy -O ihex main.elf main.hex
 *
 * @author  Bare-Metal Demo
 * @date    2025
 */

#include <avr/io.h>          /* Definiciones de registros del AVR              */
#include <avr/interrupt.h>   /* Macros ISR(), sei(), cli()                     */

/* ─────────────────────────────  Constantes  ─────────────────────────────── */

#define F_CPU       16000000UL  /* Frecuencia del cristal: 16 MHz              */
#define PPR         20          /* Pulsos Por Revolución del encoder            */

/*
 * OCR1A = (F_CPU / (Prescaler * f_deseada)) - 1
 *       = (16 000 000 / (1024 * 1)) - 1
 *       = 15 625 - 1
 *       = 15 624
 *
 * Con este valor, Timer1 genera exactamente 1 interrupción por segundo,
 * ya que el contador incrementa cada (1024 / 16 MHz) ≈ 64 µs y alcanza
 * 15 624 comparaciones antes de reiniciarse → 15 625 × 64 µs = 1 000 000 µs = 1 s.
 */
#define OCR1A_1S    15624       /* Valor de comparación para periodo de 1 s    */

/* ─────────────────────────  Variables compartidas  ──────────────────────── */

/*
 * volatile: Le indica al compilador que esta variable puede ser modificada
 * fuera del flujo normal del programa (desde una ISR). Sin volatile el
 * optimizador podría cachearla en un registro y nunca leer su valor real
 * desde RAM, produciendo comportamiento incorrecto.
 */
volatile uint32_t pulse_count = 0;   /* Pulsos acumulados en el intervalo      */
volatile uint32_t rpm         = 0;   /* RPM calculadas (actualizadas cada 1 s) */

/* ──────────────────────────  Prototipos  ────────────────────────────────── */

static void INT0_Init(void);
static void TIMER1_Init(void);

/* ═══════════════════════════  Inicialización  ═══════════════════════════════
 *
 * INT0_Init — Configura la interrupción externa INT0 (pin PD2).
 *
 *  DDRD  : Data Direction Register D.
 *          Bit = 0 → entrada, Bit = 1 → salida.
 *          Se limpia el bit 2 para configurar PD2 como entrada.
 *
 *  PORTD : Port D Data Register.
 *          Escribir 1 en un pin configurado como entrada activa la
 *          resistencia pull-up interna (~50 kΩ).
 *
 *  EICRA : External Interrupt Control Register A.
 *          ISC01:ISC00 controlan el tipo de disparo de INT0:
 *            00 → Nivel bajo
 *            01 → Cualquier cambio lógico
 *            10 → Flanco de bajada
 *            11 → Flanco de subida  ← elegido
 *          Se escriben los bits ISC01 e ISC00 con valor 1.
 *
 *  EIMSK : External Interrupt Mask Register.
 *          Bit INT0 = 1 habilita la interrupción INT0.
 *          Sin este bit la interrupción no se atiende aunque EICRA esté listo.
 * ═══════════════════════════════════════════════════════════════════════════ */
static void INT0_Init(void)
{
    /* PD2 como entrada */
    DDRD  &= ~(1 << DDD2);

    /* Activar pull-up interno en PD2 */
    PORTD |=  (1 << PORTD2);

    /* Flanco de subida: ISC01=1, ISC00=1 */
    EICRA |=  (1 << ISC01) | (1 << ISC00);

    /* Habilitar INT0 en la máscara de interrupciones externas */
    EIMSK |=  (1 << INT0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *
 * TIMER1_Init — Configura Timer1 en modo CTC con periodo de 1 segundo.
 *
 *  TCCR1A : Timer/Counter1 Control Register A.
 *           WGM11:WGM10 = 00 → parte baja del selector de modo CTC.
 *           Se escribe 0x00 (modo CTC usa WGM12 en TCCR1B).
 *
 *  TCCR1B : Timer/Counter1 Control Register B.
 *           WGM12 = 1    → activa modo CTC (Clear Timer on Compare match).
 *                          En CTC, TCNT1 se reinicia a 0 al igualar OCR1A.
 *           CS12:CS10 = 101 → Prescaler = 1024.
 *
 *  OCR1A  : Output Compare Register 1A.
 *           Valor de comparación; al coincidir con TCNT1 se dispara la ISR
 *           y el contador se reinicia automáticamente (modo CTC).
 *
 *  TIMSK1 : Timer/Counter1 Interrupt Mask Register.
 *           OCIE1A = 1 → habilita la interrupción por comparación con OCR1A.
 *           Sin este bit la interrupción no se genera aunque TCNT1 = OCR1A.
 * ═══════════════════════════════════════════════════════════════════════════ */
static void TIMER1_Init(void)
{
    /* Modo CTC: WGM12=1 | Prescaler 1024: CS12=1, CS10=1 */
    TCCR1A = 0x00;
    TCCR1B = (1 << WGM12) | (1 << CS12) | (1 << CS10);

    /* Valor de comparación para 1 segundo exacto */
    OCR1A = OCR1A_1S;

    /* Habilitar interrupción por comparación A del Timer1 */
    TIMSK1 = (1 << OCIE1A);
}

/* ══════════════════════════════  ISR  ══════════════════════════════════════
 *
 * ISR(INT0_vect) — Se ejecuta en cada flanco de subida detectado en PD2.
 * Incrementa el contador global de pulsos del encoder.
 * ══════════════════════════════════════════════════════════════════════════ */
ISR(INT0_vect)
{
    pulse_count++;
}

/* ══════════════════════════════════════════════════════════════════════════
 *
 * ISR(TIMER1_COMPA_vect) — Se ejecuta exactamente cada 1 segundo.
 *
 * Calcula las RPM a partir de los pulsos acumulados en el último segundo:
 *
 *   RPM = (pulsos_en_1s × 60) / PPR
 *
 * Multiplicar por 60 convierte de "pulsos/segundo" a "pulsos/minuto".
 * Dividir por PPR convierte "pulsos/minuto" en "revoluciones/minuto".
 *
 * Se deshabilitan las interrupciones globales brevemente para leer y
 * reiniciar pulse_count de forma atómica, evitando condición de carrera
 * con INT0_vect.
 * ══════════════════════════════════════════════════════════════════════════ */
ISR(TIMER1_COMPA_vect)
{
    uint32_t pulses;

    /* Lectura atómica: deshabilitar INT0 momentáneamente */
    cli();
    pulses      = pulse_count;
    pulse_count = 0;
    sei();

    /* Fórmula de RPM */
    rpm = (pulses * 60UL) / PPR;
}

/* ═══════════════════════════════  main  ════════════════════════════════════
 *
 * Punto de entrada del programa.
 * Inicializa periféricos, habilita interrupciones globales y queda en
 * bucle de bajo consumo. Todo el trabajo real ocurre en las ISR.
 * ═══════════════════════════════════════════════════════════════════════════ */
int main(void)
{
    INT0_Init();     /* Configurar interrupción externa del encoder */
    TIMER1_Init();   /* Configurar Timer1 para ventana de 1 segundo */

    /*
     * sei() — Set Enable Interrupts.
     * Establece el bit I (Global Interrupt Enable) en el registro SREG.
     * Sin esta instrucción ninguna ISR se ejecuta, aunque los periféricos
     * estén configurados correctamente.
     */
    sei();

    /* Bucle principal: el procesamiento ocurre íntegramente en las ISR */
    while (1)
    {
        /*
         * La variable rpm contiene el valor calculado más reciente.
         * Aquí se podría transmitir por UART, mostrar en un display,
         * controlar un actuador, etc.
         *
         * Ejemplo de uso:
         *   uint32_t velocidad_actual = rpm;   (lectura atómica recomendada)
         */
    }

    return 0;   /* Nunca se alcanza; requerido por el estándar C            */
}
