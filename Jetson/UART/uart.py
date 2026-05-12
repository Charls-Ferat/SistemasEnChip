import serial

# Cambiar el puerto si es necesario
ser = serial.Serial('/dev/ttyACM0', 9600)

contador = 0

while contador < 7:

    mensaje = ser.readline().decode('utf-8').strip()

    print("Recibido:", mensaje)

    contador += 1

ser.close()
