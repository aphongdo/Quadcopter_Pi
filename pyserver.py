import socket
import json 
import serial
from time import *

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(('0.0.0.0',7000))

start = time()

ser = serial.Serial('/dev/ttyACM0', '57600',timeout=0.0001)
outputOld = 0


#chksum calculation
def chksum(str):
  c = 0
  for a in str:
    c = ((c + ord(a)) << 1)% 256
  return c

def sendMsg(str):
  global outputOld
  #calc checksum
  chk = chksum(str)
  
  #concatenate msg and chksum
  output = "%s*%2x\r\n" % (str, chk)
  if (output != outputOld):
    print output  
    outputOld = output;
  ser.write(output)
  ser.flush()
  
#main loop
def main():
  readStrOld = 0
  while True:
    # wait for UDP packet
    data,addr= sock.recvfrom(1024)
    data = data.replace("'", "\"");

    # parse it
    p = json.loads(data)

    readStr=ser.readline()
    if (readStr != readStrOld):
        print readStr
        readStrOld = readStr

    # if control packet, send to ardupilot
    if (p['type'] == 'rcinput'):
        str = "%d,%d,%d,%d" % (p['roll'], -p['pitch'], p['yaw'], p['thr'])
        sendMsg(str)
    if (p['type'] == 'set'):
        str = "%s,%.2f,%.2f,%d" % (p['type'], p['rdev'], p['pdev'],p['thrscl'])
        sendMsg(str)
    if (p['type'] == 'pid'):
        str = "%s,%.1f,%.1f,%.1f" % (p['pid'], p['kp'],p['ki'],p['kd'])
        sendMsg(str)
      
main()
