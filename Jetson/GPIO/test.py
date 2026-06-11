import time
import Jetson.GPIO as GPIO

GPIO.setmode(GPIO.BOARD)
GPIO.setwarnings(False)

# Pines físicos conectados a segmentos: A, B, C, D, E, F, G
seg_pins = [33, 7, 35, 36, 38, 32, 37]

# Display cátodo común:
ON = GPIO.HIGH
OFF = GPIO.LOW

# Mapa hexadecimal: A, B, C, D, E, F, G
hex_map = [
    [1,0,0,0,0,0,0],   # 0
    [0,1,0,0,0,0,0],   # 1
    [0,0,1,0,0,0,0],   # 2
    [0,0,0,1,0,0,0],   # 3
    [0,0,0,0,1,0,0],   # 4
    [0,0,0,0,0,1,0],   # 5
    [0,0,0,0,0,0,1],   # 7
]

def display_hex(val):
    for i in range(7):
        GPIO.output(seg_pins[i], ON if hex_map[val][i] == 1 else OFF)

try:
    # Configurar segmentos como salida
    for pin in seg_pins:
        GPIO.setup(pin, GPIO.OUT, initial=OFF)

    counter = 0
    display_hex(counter)

    while True:
        counter = (counter + 1) % 7

        display_hex(counter)
        time.sleep(1)

except KeyboardInterrupt:
    print("Programa detenido")

finally:
    for pin in seg_pins:
        GPIO.output(pin, OFF)
    GPIO.cleanup()
