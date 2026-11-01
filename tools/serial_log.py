"""Reset the Core2 and print its serial output (115200 baud) for a number of seconds.

Usage: python3 tools/serial_log.py /dev/cu.usbserial-XXXX [seconds]
Needs only the Python standard library (no pyserial).
"""
import os, sys, termios, time, fcntl, struct
port = sys.argv[1]
seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 10
fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
attrs = termios.tcgetattr(fd)
attrs[0] = 0; attrs[1] = 0
attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
attrs[3] = 0
attrs[4] = attrs[5] = termios.B115200
termios.tcsetattr(fd, termios.TCSANOW, attrs)
# Pulse RTS (wired to EN through the auto-reset circuit) to reset the ESP32, keep DTR high (no bootloader)
TIOCMBIS, TIOCMBIC = 0x8004746c, 0x8004746b
TIOCM_DTR, TIOCM_RTS = 0x002, 0x004
fcntl.ioctl(fd, TIOCMBIC, struct.pack('I', TIOCM_DTR))
fcntl.ioctl(fd, TIOCMBIS, struct.pack('I', TIOCM_RTS))
time.sleep(0.1)
fcntl.ioctl(fd, TIOCMBIC, struct.pack('I', TIOCM_RTS))
end = time.time() + seconds
data = b''
while time.time() < end:
    try:
        chunk = os.read(fd, 4096)
        if chunk: data += chunk
    except BlockingIOError:
        pass
    time.sleep(0.01)
os.close(fd)
sys.stdout.write(data.decode('latin-1'))
