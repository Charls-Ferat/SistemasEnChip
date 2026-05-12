import time
import Jetson.GPIO as GPIO

GPIO.setmode(GPIO.BOARD)
GPIO.setwarnings(False)

# Pines físicos conectados a segmentos: A, B, C, D, E, F, G
seg_pins = [33, 7, 35, 36, 38, 32, 37]

# Pin físico de entrada
# Pin 40 baja a 0 cuando lo conectas a GND
input_pin = 40

# Display cátodo común:
ON = GPIO.HIGH
OFF = GPIO.LOW

# Mapa hexadecimal: A, B, C, D, E, F, G
hex_map = [
    [1,1,1,1,1,1,0],   # 0
    [0,1,1,0,0,0,0],   # 1
    [1,1,0,1,1,0,1],   # 2
    [1,1,1,1,0,0,1],   # 3
    [0,1,1,0,0,1,1],   # 4
    [1,0,1,1,0,1,1],   # 5
    [1,0,1,1,1,1,1],   # 6
    [1,1,1,0,0,0,0],   # 7
    [1,1,1,1,1,1,1],   # 8
    [1,1,1,1,0,1,1],   # 9
    [1,1,1,0,1,1,1],   # A
    [0,0,1,1,1,1,1],   # b
    [1,0,0,1,1,1,0],   # C
    [0,1,1,1,1,0,1],   # d
    [1,0,0,1,1,1,1],   # E
    [1,0,0,0,1,1,1],   # F
]

def display_hex(val):
    for i in range(7):
        GPIO.output(seg_pins[i], ON if hex_map[val][i] == 1 else OFF)

try:
    # Configurar segmentos como salida
    for pin in seg_pins:
        GPIO.setup(pin, GPIO.OUT, initial=OFF)

    # Configurar pin 40 como entrada
    GPIO.setup(input_pin, GPIO.IN)

    counter = 0
    display_hex(counter)

    while True:
        estado = GPIO.input(input_pin)

        if estado == GPIO.HIGH:
            counter = (counter + 1) % 16
            #print("Entrada = 1 | cuenta adelante:", counter)
        else:
            counter = (counter - 1) % 16
            #print("Entrada = 0 | cuenta atrás:", counter)

        display_hex(counter)
        time.sleep(1)

except KeyboardInterrupt:
    print("Programa detenido")

finally:
    for pin in seg_pins:
        GPIO.output(pin, OFF)
    GPIO.cleanup()
