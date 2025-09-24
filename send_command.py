# send_command.py
# Exemple simple d'utilisation sous Windows pour envoyer une commande série et lire la réponse
# Nécessite pyserial : pip install pyserial

import serial
import time
import sys

if len(sys.argv) < 3:
    print("Usage: python send_command.py COMx 'COMMAND'")
    sys.exit(1)

port = sys.argv[1]
cmd = sys.argv[2]

ser = serial.Serial(port, 9600, timeout=1)
# laisser le temps au device
time.sleep(0.5)
ser.write((cmd + "\r\n").encode('ascii'))
# lire quelques lignes de réponse
start = time.time()
while time.time() - start < 2.0:
    line = ser.readline().decode('ascii', errors='ignore').strip()
    if line:
        print(line)
ser.close()
