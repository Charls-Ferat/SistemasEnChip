import time
import Jetson.GPIO as GPIO

GPIO.setmode(GPIO.BOARD)
GPIO.setwarnings(False)

# Pines físicos conectados a segmentos: A, B, C, D, E, F, G
seg_pins = {
    'A': 16,
    'B': 12,
    'C': 40,
    'D': 32,
    'E': 36,
    'F': 18,
    'G': 38
}
# Display cátodo común:
ON = GPIO.HIGH
OFF = GPIO.LOW

try:
    # Configurar segmentos como salida
    for name, pin in seg_pins.items():
        GPIO.setup(pin, GPIO.OUT, initial=OFF)

    while True:
        for pin in seg_pins.items():
            GPIO.output(pin, ON)
            print(f"Encendido: {pin}")
            time.sleep(0.5)
            GPIO.output(pin, OFF)


except KeyboardInterrupt:
    print("Programa detenido")

finally:
    for pin in seg_pins:
        GPIO.output(pin, OFF)
    GPIO.cleanup()
