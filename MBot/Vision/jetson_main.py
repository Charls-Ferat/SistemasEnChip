# Jetson - Python CV2 scrips
# Rev 3
# PD controller - Only proportional and derivative changes are meassured
import cv2
import numpy as np
import serial

# Packet sync byte & flags
SYNC_BYTE = 0xAA
FLAGS_NONE = 0x00

# Max speed straight
SPEED_STRAIGHT = 170   

# CRC package computation
def compute_crc(data):
    crc = 0

    for b in data:
        crc ^= b

    return crc & 0xFF

# Packet UART send
# [SYNC][SPEED][STEER][FLAGS][CRC]
def send_packet(uart, speed, steer, flags):

    packet = bytearray(5)

    packet[0] = SYNC_BYTE
    packet[1] = speed & 0xFF
    packet[2] = steer & 0xFF  # int8 -> uint8
    packet[3] = flags & 0xFF
    packet[4] = compute_crc(packet[:4])

    uart.write(packet)

# Clamp value to max values
def clamp(value, minimum, maximum):
    return max(minimum, min(value, maximum))

# PD class
class PDController:
    def __init__(self, kp, kd, output_limit=None):
        #kp : proportional gain
        #kd : derivative gain

        self.kp = kp
        self.kd = kd
        self.last_error = 0
        self.output_limit = output_limit

    def update(self, error):
        # Derivative error
        derivative = error - self.last_error
        output = self.kp * error + self.kd * derivative
        self.last_error = error

        # Clamp output if requested
        if self.output_limit is not None:
            output = max(self.output_limit[0], min(self.output_limit[1], output))

        return output

# PD gains
steer_pd = PDController(
    kp = 35.0,                  # A kind of magic
    kd = 21.0,                  # Nananana
    output_limit=(-50, 50)      # No monsters in me
)

# Main cycle
def main():
    # Serial Port 
    uart = serial.Serial(port="/dev/ttyUSB0", baudrate=115200, timeout=0.01)

    # Video capture frame
    cap = cv2.VideoCapture(0, cv2.CAP_V4L2)

    if not cap.isOpened():
        print("Camera error")
        return
    
    # Limit buffer size
    cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)

    # Kernel for image cleaning
    kernel = np.ones((5, 5), np.uint8)

    # Last valid command
    last_valid_steer = 0
    last_valid_speed = SPEED_STRAIGHT

    # Processing loop
    while True:
        ret, frame = cap.read()

        if not ret:
            break

        height, width = frame.shape[:2]

        # ROI inferior (último 20% de la imagen)
        roi_y = int(height * 0.25)
        roi = frame[roi_y:height, :]
        roi_height, roi_width = roi.shape[:2]

        # Escala de grises
        gray = cv2.cvtColor(roi, cv2.COLOR_BGR2GRAY)

        # Threshold inverso
        # Línea negra -> blanco
        # Fondo blanco -> negro
        _, binary = cv2.threshold(gray, 80, 255, cv2.THRESH_BINARY_INV)

        # Apertura
        binary = cv2.morphologyEx(binary, cv2.MORPH_OPEN, kernel)
        # Cierre
        binary = cv2.morphologyEx(binary, cv2.MORPH_CLOSE, kernel)

        contours, _ = cv2.findContours(
            binary, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE
        )

        steer = last_valid_steer    # Steer value to send
        speed = last_valid_speed    # Speed value to send
        flags = FLAGS_NONE          # No. of flags to send(just one)

        # If line detected
        if len(contours) > 0:
            largest = max(contours, key=cv2.contourArea)

            M = cv2.moments(largest)

            if M["m00"] > 0:
                cx = int(M["m10"] / M["m00"])

                # Centroid
                center_x = roi_width // 2

                # Desfase
                error_pixels = cx - center_x
                error_norm = error_pixels / (roi_width / 2)

                # PD control
                steer = int(steer_pd.update(error_norm))

                # Steering effort
                turn_intensity = abs(steer) / 45.0  # normalized 0 → 1

                speed = int(SPEED_STRAIGHT - 140 * (turn_intensity ** 2))
                speed = max(80, speed)

                # Save last valid command
                last_valid_steer = steer
                last_valid_speed = speed

                # Debug visual
                cv2.drawContours(roi, [largest], -1, (0, 255, 0), 2)

                cv2.line(
                    roi,
                    (center_x, roi_height - 1),
                    (cx, roi_height // 2),
                    (255, 0, 0),
                    2,
                )

                if steer > 5:
                    accion = "GIRAR DERECHA"
                elif steer < -5:
                    accion = "GIRAR IZQUIERDA"
                else:
                    accion = "SEGUIR RECTO"

                print(
                    f"Centro={cx} px | "
                    f"Error={error_pixels:+d} px | "
                    f"Angulo={steer:+d} deg | "
                    f"Speed={speed} | "
                    f"{accion}"
                )

            else:

                steer = last_valid_steer
                speed = last_valid_speed
                flags |= 0x01

                print(
                    f"CONTORNO INVALIDO -> "
                    f"USANDO ULTIMO COMANDO "
                    f"(angulo={steer}, speed={speed})"
                )

        else:
            steer = last_valid_steer
            speed = last_valid_speed
            flags |= 0x01

            print(
                f"LINEA PERDIDA -> "
                f"USANDO ULTIMO COMANDO "
                f"(angulo={steer}, speed={speed})"
            )

        send_packet(uart, speed, steer, flags)

        cv2.imshow("Binary", binary)
        cv2.imshow("ROI", roi)

        key = cv2.waitKey(1) & 0xFF

        if key == ord("q"):
            break

    cap.release()
    uart.close()

    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
