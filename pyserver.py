import socket
import json 
import serial
from time import *

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(('0.0.0.0',7000))

ser = serial.Serial('/dev/ttyACM0', '57600',timeout=0.001)
rv = ""
clean = 0
#chksum calculation
def chksum(str):
  c = 0
  for a in str:
    c = ((c + ord(a)) << 1)% 256
  return c

def readlineCR(ser):
    global rv
    global clean
    ch = ser.read()
    rv += ch
    if ch=='\r' or ch=='':
        clean = 1;
        return rv
  
  
#main loop
def main():
  global rv
  global clean
  readStrOld = 0
  outputOld = 0
  while True:
    # wait for UDP packet
    data,addr= sock.recvfrom(1024)
    data = data.replace("'", "\"");

    # parse it
    p = json.loads(data)
    start = time()
    readStr=ser.readline()

    ##readStr = readlineCR(ser)
    ##if clean == 1:
    ##    clean = 0
    ##    rv = ""
    ##    print time() - start
    ##    print readStr
    
    if (readStr != readStrOld):
        print readStr
        readStrOld = readStr
      #sleep(0.05)
      #print ser.readline()

    # if control packet, send to ardupilot
    if (p['type'] == 'rcinput'):
      str = "%d,%d,%d,%d" % (p['roll'], -p['pitch'], 1070 +  p['thr']*10, p['yaw'])
      #calc checksum
      chk = chksum(str)
      
      #concatenate msg and chksum
      output = "%s*%2x\r\n" % (str, chk)
      if (output != outputOld):
        print output  
        outputOld = output;
      ser.write(output)
      ser.flush()
      

      
      
main()
