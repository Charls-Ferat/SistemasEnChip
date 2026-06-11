// TopMAN
// Rev 8
// FreeRTOS implementation with UART && timeout handling.

#include <Arduino_FreeRTOS.h>
#include <task.h>
#include <event_groups.h>
#include <semphr.h>

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <util/atomic.h>

#include "src/MeNewRGBLed.h"
#include "src/MeCollisionSensor.h"
#include <MeMegaPi.h>

// ---- Constantes ----
// UART protocol: [SYNC][SPEED][STEER][FLAGS][CRC]
#define BAUD_RATE               115200
#define SYNC_BYTE               0xAA
#define PACKET_SIZE             5
#define COMM_TIMEOUT_MS         500UL
// Motor control
#define MAX_TURN_CORRECTION     120
#define TURN_SPEED              120
#define TURN_180_TIME           2900
#define STOP_TIME               300
#define MIN_MOTOR_SPEED         60
// Event indicators
#define EVENT_COLLISION         (1 << 0)
#define EVENT_COMMS_OK          (1 << 1)

// ---- Struct ----
typedef struct {
  uint8_t speed;
  int8_t steer;
  uint8_t flags;
} Packet;

// ---- Hardware objects ----
MeNewRGBLed rgbled_67(67, 4);
MeNewRGBLed rgbled_68(68, 4);

MeMegaPiDCMotor motor_1(1);
MeMegaPiDCMotor motor_9(9);
MeMegaPiDCMotor motor_2(2);
MeMegaPiDCMotor motor_10(10);

MeCollisionSensor collision_65(65);
MeCollisionSensor collision_66(66);

// ---- FreeRTOS objects ----
Packet latestPacket = {0, 0, 0};
SemaphoreHandle_t packetMutex;
SemaphoreHandle_t motorMutex;
EventGroupHandle_t systemEvents;

// Both values use the same 32-bit millisecond time base.
volatile uint32_t lastPacketMs = 0;
volatile uint32_t msCounter = 0;

// ---- Timer helpers ----
void markPacketReceived() {
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    lastPacketMs = msCounter;
  }
}

uint32_t packetAgeMs() {
  uint32_t now;
  uint32_t last;

  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    now = msCounter;
    last = lastPacketMs;
  }

  // Unsigned subtraction remains correct across uint32_t rollover.
  return now - last;
}

// ---- Helpers ----
int16_t mapValue(
    int16_t x,
    int16_t inMin,
    int16_t inMax,
    int16_t outMin,
    int16_t outMax) {
  return (int16_t)(
      ((int32_t)(x - inMin) * (outMax - outMin))
      / (inMax - inMin)
      + outMin);
}

int16_t constrainValue(int16_t x, int16_t minVal, int16_t maxVal) {
  if (x < minVal) {
    return minVal;
  }

  if (x > maxVal) {
    return maxVal;
  }

  return x;
}

int16_t applyDeadzone(int16_t value) {
  if (value == 0) {
    return 0;
  }

  if (value > 0 && value < MIN_MOTOR_SPEED) {
    return MIN_MOTOR_SPEED;
  }

  if (value < 0 && value > -MIN_MOTOR_SPEED) {
    return -MIN_MOTOR_SPEED;
  }

  return value;
}

// ---- Motor control ----
void motorForwardLeftRun(int16_t speed) {
  motor_10.run(-speed);
}

void motorForwardRightRun(int16_t speed) {
  motor_1.run(speed);
}

void motorBackLeftRun(int16_t speed) {
  motor_2.run(-speed);
}

void motorBackRightRun(int16_t speed) {
  motor_9.run(speed);
}

void stopMotors() {
  motorForwardLeftRun(0);
  motorForwardRightRun(0);
  motorBackLeftRun(0);
  motorBackRightRun(0);
}

void rotateInPlace(int16_t speed) {
  motorForwardLeftRun(speed);
  motorBackLeftRun(speed);
  motorForwardRightRun(-speed);
  motorBackRightRun(-speed);
}

void performUTurn() {
  stopMotors();
  vTaskDelay(pdMS_TO_TICKS(STOP_TIME));

  rotateInPlace(TURN_SPEED);
  vTaskDelay(pdMS_TO_TICKS(TURN_180_TIME));

  stopMotors();
  vTaskDelay(pdMS_TO_TICKS(STOP_TIME));
}

void lineFollowDrive(int16_t speed, int8_t steer) {
  int16_t correction = mapValue(
      steer,
      -40,
      40,
      -MAX_TURN_CORRECTION,
      MAX_TURN_CORRECTION);

  int16_t leftSpeed = constrainValue(speed + correction, -255, 255);
  int16_t rightSpeed = constrainValue(speed - correction, -255, 255);

  leftSpeed = applyDeadzone(leftSpeed);
  rightSpeed = applyDeadzone(rightSpeed);

  motorForwardLeftRun(leftSpeed);
  motorBackLeftRun(leftSpeed);
  motorForwardRightRun(rightSpeed);
  motorBackRightRun(rightSpeed);
}



// ----  UART ----
// Uart setup
void UART_init(void) {
  UCSR0A = (1 << U2X0);

  UBRR0H = (UBRR_VALUE >> 8);
  UBRR0L = UBRR_VALUE;

  UCSR0B = (1 << TXEN0) | (1 << RXEN0);
  UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
}

// Send char
void UART_sendChar(char c) {
    while (!(UCSR0A & (1 << UDRE0)));
    UDR0 = c;
}

// Send a full string
void UART_sendString(const char *str) {
    while (*str) {
        UART_sendChar(*str++);
    }
}

// UART availability
uint8_t UART_available(void) {
    return (UCSR0A & (1 << RXC0));
}

// Non block readbyte
uint8_t UART_readByte(void) {
    return UDR0;
}

// ---- Packet decoder ----
uint8_t computeCRC(const uint8_t *data, uint8_t length) {
  uint8_t crc = 0;

  for (uint8_t i = 0; i < length; i++) {
    crc ^= data[i];
  }

  return crc;
}

// Resync helper
uint8_t resyncPacketBuffer(uint8_t *buffer) {
  for (uint8_t position = 1; position < PACKET_SIZE; position++) {
    if (buffer[position] == SYNC_BYTE) {
      uint8_t remaining = PACKET_SIZE - position;
      memmove(buffer, &buffer[position], remaining);
      return remaining;
    }
  }

  return 0;
}

bool receivePacket(Packet *packet) {
  static uint8_t buffer[PACKET_SIZE];
  static uint8_t index = 0;

  while (UART_available()) {
    uint8_t value = UART_readByte();

    // Resync
    if (index == 0) {
      if (value == SYNC_BYTE) {
        buffer[0] = value;
        index = 1;
      }
      continue;
    }

    buffer[index++] = value;

    if (index < PACKET_SIZE) {
      continue;
    }

    uint8_t receivedCRC = buffer[PACKET_SIZE - 1];
    uint8_t calculatedCRC = computeCRC(buffer, PACKET_SIZE - 1);

    if (receivedCRC == calculatedCRC) {
      packet->speed = buffer[1];
      packet->steer = (int8_t)buffer[2];
      packet->flags = buffer[3];
      index = 0;
      return true;
    }

    index = resyncPacketBuffer(buffer);
  }

  return false;
}

// ----  1 ms timer ----
void setupTimer3_1ms() {
  cli();

  TCCR3A = 0;
  TCCR3B = 0;
  TCNT3 = 0;

  OCR3A = 249;
  TCCR3B |= (1 << WGM32);
  TCCR3B |= (1 << CS31) | (1 << CS30);
  TIMSK3 |= (1 << OCIE3A);

  sei();
}

ISR(TIMER3_COMPA_vect) {
  msCounter++;
}

// ---- FreeRTOS tasks ----
void vUARTTask(void *pvParameters) {
  UART_sendString("UART");
  (void)pvParameters;
  Packet packet;
  TickType_t lastWake = xTaskGetTickCount();

  while (1) {
    uint8_t packetsProcessed = 0;

    while (receivePacket(&packet)) {
      if (xSemaphoreTake(packetMutex, portMAX_DELAY) == pdTRUE) {
        latestPacket = packet;
        xSemaphoreGive(packetMutex);
      }

      markPacketReceived();
      xEventGroupSetBits(systemEvents, EVENT_COMMS_OK);

      packetsProcessed++;
      if (packetsProcessed >= 8) {
        break;
      }
    }

    vTaskDelayUntil(&lastWake, 1);
  }
}

void vLEDTask(void *pvParameters) {
  UART_sendString("LEDs");
  (void)pvParameters;
  TickType_t lastWake = xTaskGetTickCount();

  while (1) {
    EventBits_t bits = xEventGroupGetBits(systemEvents);

    if (bits & EVENT_COMMS_OK) {
      rgbled_67.setColor(2, 211, 211, 211);
      rgbled_68.setColor(2, 0, 0, 0);
    } else {
      rgbled_67.setColor(2, 0, 0, 0);
      rgbled_68.setColor(2, 211, 32, 32);
    }

    rgbled_67.show();
    rgbled_68.show();

    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(200));
  }
}

void vSafetyTask(void *pvParameters) {
  UART_sendString("Safety");
  (void)pvParameters;
  TickType_t lastWake = xTaskGetTickCount();

  while (1) {
    if (packetAgeMs() > COMM_TIMEOUT_MS) {
      xEventGroupClearBits(systemEvents, EVENT_COMMS_OK);

      if (xSemaphoreTake(motorMutex, 0) == pdTRUE) {
        stopMotors();
        xSemaphoreGive(motorMutex);
      }
    }

    vTaskDelayUntil(&lastWake, 1);
  }
}

void vMotionTask(void *pvParameters) {
  UART_sendString("Motion");
  (void)pvParameters;
  Packet packet = {0, 0, 0};
  TickType_t lastWake = xTaskGetTickCount();

  while (1) {
    EventBits_t bits = xEventGroupGetBits(systemEvents);

    if ((bits & EVENT_COMMS_OK) && !(bits & EVENT_COLLISION)) {
      if (xSemaphoreTake(packetMutex, portMAX_DELAY) == pdTRUE) {
        packet = latestPacket;
        xSemaphoreGive(packetMutex);
      }

      if (xSemaphoreTake(motorMutex, portMAX_DELAY) == pdTRUE) {
        // Recheck the safety state after motor mutex thing.
        bits = xEventGroupGetBits(systemEvents);
        if ((bits & EVENT_COMMS_OK) && !(bits & EVENT_COLLISION)) {
          lineFollowDrive(packet.speed, packet.steer);
        } else {
          stopMotors();
        }
        xSemaphoreGive(motorMutex);
      }
    }

    vTaskDelayUntil(&lastWake, 1);
  }
}

void vCollisionTask(void *pvParameters) {
  UART_sendString("Collision");
  (void)pvParameters;
  TickType_t lastWake = xTaskGetTickCount();

  while (1) {
    if (collision_65.isCollision() || collision_66.isCollision()) {
      xEventGroupSetBits(systemEvents, EVENT_COLLISION);

      if (xSemaphoreTake(motorMutex, portMAX_DELAY) == pdTRUE) {
        performUTurn();
        xSemaphoreGive(motorMutex);
      }

      xEventGroupClearBits(systemEvents, EVENT_COLLISION);
    }

    vTaskDelayUntil(&lastWake, 1);
  }
}

// ---- Setup and loop ----
void setup() {
  UART_init();
  UART_sendString("Setup");

  setupTimer3_1ms();
  stopMotors();

  packetMutex = xSemaphoreCreateMutex();
  motorMutex = xSemaphoreCreateMutex();
  systemEvents = xEventGroupCreate();

  if (packetMutex == NULL || motorMutex == NULL || systemEvents == NULL) {
    stopMotors();
    while (1) {
    }
  }

  rgbled_67.fillPixelsBak(0, 2, 1);
  rgbled_68.fillPixelsBak(0, 2, 1);

  TCCR1A = _BV(WGM10);
  TCCR1B = _BV(CS11) | _BV(WGM12);

  TCCR2A = _BV(WGM21) | _BV(WGM20);
  TCCR2B = _BV(CS21);

  rgbled_67.setColor(0, 0, 0, 0);
  rgbled_67.show();
  rgbled_68.setColor(0, 0, 0, 0);
  rgbled_68.show();

  xEventGroupClearBits(systemEvents, EVENT_COMMS_OK);
  markPacketReceived();

  bool tasksCreated =
      xTaskCreate(vCollisionTask, "Collision", 256, NULL, 2, NULL) == pdPASS
      && xTaskCreate(vUARTTask, "UART", 256, NULL, 1, NULL) == pdPASS
      && xTaskCreate(vSafetyTask, "Safety", 256, NULL, 1, NULL) == pdPASS
      && xTaskCreate(vMotionTask, "Motion", 256, NULL, 1, NULL) == pdPASS
      && xTaskCreate(vLEDTask, "LED", 192, NULL, 1, NULL) == pdPASS;

  if (!tasksCreated) {
    UART_sendString("failed");
    stopMotors();
    while (1) {
    }
  }

  vTaskStartScheduler();

  // The scheduler should never return.
  stopMotors();
  UART_sendString("shit");
  while (1) {
  }
}

void loop() {
  /*
  * Vacio
  */
}
