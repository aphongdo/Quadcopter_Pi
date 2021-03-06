#include <AP_HAL.h>
#include <AP_Common.h>
#include <PID.h>
#include <AP_Progmem.h>
#include <AP_Math.h>
#include <AP_Param.h>
#include <AP_InertialSensor.h>
#include <AP_ADC.h>
#include <AP_ADC_AnalogSource.h>
#include <AP_Baro.h>            // ArduPilot Mega Barometer Library
#include <AP_GPS.h>
#include <AP_AHRS.h>
#include <AP_Compass.h>
#include <AP_Declination.h>
#include <AP_Airspeed.h>
#include <GCS_MAVLink.h>
#include <AP_Mission.h>
#include <StorageManager.h>
#include <AP_Terrain.h>
#include <Filter.h>
#include <SITL.h>
#include <AP_Buffer.h>
#include <AP_Notify.h>
#include <AP_Vehicle.h>
#include <DataFlash.h>
#include <AP_BattMonitor.h>

#include <AP_HAL_AVR.h>
#include <AP_HAL_AVR_SITL.h>
#include <AP_HAL_Empty.h>

// ArduPilot Hardware Abstraction Layer
const AP_HAL::HAL& hal = AP_HAL_AVR_APM2;

// MPU6050 accel/gyro chip
AP_InertialSensor ins;
AP_Baro_MS5611 baro(&AP_Baro_MS5611::spi);
AP_Compass_HMC5843 compass;
AP_GPS gps;
AP_AHRS_DCM  ahrs(ins, baro, gps);

// Battery info
uint32_t bat_Schl;
uint32_t time_TPRY;
AP_BattMonitor battery_mon;

#define RC_THR_MIN   1070
// Motor numbers definitions
#define MOTOR_FL   2    // Front left    
#define MOTOR_FR   0    // Front right
#define MOTOR_BL   1    // back left
#define MOTOR_BR   3    // back right

// Arduino map function
long map(long x, long in_min, long in_max, long out_min, long out_max)
{
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

#define wrap_180(x) (x < -180 ? x+360 : (x > 180 ? x - 360: x))

// PID array (6 pids, two for each axis)
PID pids[6];
#define PID_PITCH_RATE 0
#define PID_ROLL_RATE 1
#define PID_PITCH_STAB 2
#define PID_ROLL_STAB 3
#define PID_YAW_RATE 4
#define PID_YAW_STAB 5

void setup() 
{
  // Enable the motors and set at 400Hz update
  hal.rcout->set_freq(0xF, 400);

  // PID Configuration
  pids[PID_PITCH_RATE].kP(0.7);
  pids[PID_PITCH_RATE].kI(1);
  pids[PID_PITCH_RATE].imax(50);

  pids[PID_ROLL_RATE].kP(0.7);
  pids[PID_ROLL_RATE].kI(1);
  pids[PID_ROLL_RATE].imax(50);

  pids[PID_YAW_RATE].kP(2.7);
  pids[PID_YAW_RATE].kI(1);
  pids[PID_YAW_RATE].imax(50);

  pids[PID_PITCH_STAB].kP(4.5);
  pids[PID_ROLL_STAB].kP(4.5);
  pids[PID_YAW_STAB].kP(10);

  // Turn off Barometer to avoid bus collisions
  hal.gpio->pinMode(40, HAL_GPIO_OUTPUT);
  hal.gpio->write(40, 1);

  // Turn on MPU6050 - quad must be kept still as gyros will calibrate
  ins.init(AP_InertialSensor::COLD_START, 
			 AP_InertialSensor::RATE_100HZ);

  // initialise sensor fusion on MPU6050 chip (aka DigitalMotionProcessing/DMP)
  ahrs.init();

  hal.uartA->begin(57600);   // for radios
  
  // initialise the battery monitor
  battery_mon.init();
  battery_mon.set_monitoring(AP_BATT_MONITOR_VOLTAGE_AND_CURRENT);
  bat_Schl = hal.scheduler->millis();
  time_TPRY = hal.scheduler->millis();
  // We're ready to go! Now over to loop()
  hal.rcout->enable_ch(MOTOR_FL);
  hal.rcout->enable_ch(MOTOR_FR);
  hal.rcout->enable_ch(MOTOR_BL);
  hal.rcout->enable_ch(MOTOR_BR);

  hal.console->printf("Finish Setup\n");
}

// serial command buffer
char buf[255];
int buf_offset = 0;

//checksum verifier
uint8_t verify_chksum(char *str, char *chk) {
	uint8_t nc = 0;
	for (int i = 0; i < strlen(str); i++)
		nc = (nc + str[i]) << 1;

	long chkl = strtol(chk, NULL, 16); // supplied chksum to long
	if (chkl == (long) nc)   // compare
		return true;

	return false;
}

void loop() 
{
  static float yaw_target = 0;  
  static uint32_t lastPkt = 0;
 
  static int16_t channels[8] = {0,0,0,0,0,0,0,0};
  static long double rdev = 0;
  static long double pdev = 0;
  static int thrscl = 10;
  char *ch;

  // serial bytes available?
  int bytesAvail = hal.console->available();
    if (bytesAvail > 0) {
        while (bytesAvail > 0) {  //yes
            char c = (char) hal.console->read();  //read next byte
            if (c == '\n') {             // new line reached - process cmd
                buf[buf_offset] = '\0';  // null terminator
                // process cmd
                char *str = strtok(buf, "*");  // str = roll,pit,thr,yaw
                char *chk = strtok(NULL, "*");  // chk = chksum

                if (verify_chksum(str, chk)) { // if chksum OK
                    if (strstr(str, "set")) {
                        ch = strtok(str, "set,"); // parse roll deviation
                        rdev = atof(ch);  // convert to float
                        ch = strtok(NULL, ",");  // parse pitch deviation
                        pdev = atof(ch);  // convert to float
                        ch = strtok(NULL, ",");  // parse pitch deviation
                        thrscl = atoi(ch);  // convert to int
                        hal.console->printf_P(PSTR("Setting %.2f pdev %.2f thrscl %d\n"), rdev, pdev, thrscl);
                    } else if (strstr(str, "pr_rate")) {
                        ch = strtok(str, "pr_rate,"); // parse Kp
                        pids[PID_PITCH_RATE].kP(atof(ch));  // convert to float
                        pids[PID_ROLL_RATE].kP(atof(ch));
                        ch = strtok(NULL, ",");  // parse Ki
                        pids[PID_PITCH_RATE].kI(atof(ch));
                        pids[PID_ROLL_RATE].kI(atof(ch));
                        ch = strtok(NULL, ",");  // parse Kd
                        pids[PID_PITCH_RATE].kD(atof(ch));
                        pids[PID_ROLL_RATE].kD(atof(ch));
                        hal.console->printf_P(PSTR("PR Rate Kp %.1f Ki %.1f Kd %.1f\n"), pids[PID_PITCH_RATE].kP(), pids[PID_PITCH_RATE].kI(),
                                pids[PID_PITCH_RATE].kD());
                    } else if (strstr(str, "pr_stab")) {
                        ch = strtok(str, "pr_stab,"); // parse Kp
                        pids[PID_PITCH_STAB].kP(atof(ch));  // convert to float
                        pids[PID_ROLL_STAB].kP(atof(ch));
                        ch = strtok(NULL, ",");  // parse Ki
                        pids[PID_PITCH_STAB].kI(atof(ch));
                        pids[PID_ROLL_STAB].kI(atof(ch));
                        ch = strtok(NULL, ",");  // parse Kd
                        pids[PID_PITCH_STAB].kD(atof(ch));
                        pids[PID_ROLL_STAB].kD(atof(ch));
                        hal.console->printf_P(PSTR("PR Stab Kp %.1f Ki %.1f Kd %.1f\n"), pids[PID_PITCH_STAB].kP(), pids[PID_PITCH_STAB].kI(),
                                pids[PID_PITCH_STAB].kD());
                    } else if (strstr(str, "yaw_rate")) {
                        ch = strtok(str, "yaw_rate,"); // parse Kp
                        pids[PID_YAW_RATE].kP(atof(ch));  // convert to float
                        ch = strtok(NULL, ",");  // parse Ki
                        pids[PID_YAW_RATE].kI(atof(ch));
                        ch = strtok(NULL, ",");  // parse Kd
                        pids[PID_YAW_RATE].kD(atof(ch));
                        hal.console->printf_P(PSTR("Yaw Rate Kp %.1f Ki %.1f Kd %.1f\n"), pids[PID_YAW_RATE].kP(), pids[PID_YAW_RATE].kI(), pids[PID_YAW_RATE].kD());
                    } else if (strstr(str, "yaw_stab")) {
                        ch = strtok(str, "yaw_stab,"); // parse Kp
                        pids[PID_YAW_STAB].kP(atof(ch));  // convert to float
                        ch = strtok(NULL, ",");  // parse Ki
                        pids[PID_YAW_STAB].kI(atof(ch));
                        ch = strtok(NULL, ",");  // parse Kd
                        pids[PID_YAW_STAB].kD(atof(ch));
                        hal.console->printf_P(PSTR("Yaw Stab Kp %.1f Ki %.1f Kd %.1f\n"), pids[PID_YAW_STAB].kP(), pids[PID_YAW_STAB].kI(), pids[PID_YAW_STAB].kD());
                    } else {
                        char *ch = strtok(str, ",");  // first channel
                        channels[0] = (uint16_t) strtol(ch, NULL, 10); // parse

                        for (int i = 1; i < 4; i++) { // loop through final 3 channels
                            char *ch = strtok(NULL, ",");
                            channels[i] = (uint16_t) strtol(ch, NULL, 10);
                        }
                    }
                    lastPkt = hal.scheduler->millis(); // update last valid packet
                } else {
                    hal.console->printf("Invalid chksum\n");
                }

                buf_offset = 0;
            } else if (c != '\r') {
                buf[buf_offset++] = c; // store in buffer and continue until newline
            }
            bytesAvail--;
        }
    }
 
  // turn throttle off if no update for 0.5seconds
  if(hal.scheduler->millis() - lastPkt > 500) 
    channels[3] = channels[3]*0.9;
  if(hal.scheduler->millis() - bat_Schl > 10000) 
  {
    battery_mon.read();
    hal.console->printf_P(PSTR("V: %.2f R:%d%%\n"),
                battery_mon.voltage()+0.45,
                battery_mon.capacity_remaining_pct());
    bat_Schl = hal.scheduler->millis();
  }
    

  long rcthr, rcyaw, rcpit, rcroll;  // Variables to store radio in 

  // Read RC transmitter 
  rcthr = RC_THR_MIN + (channels[3]*(long)thrscl);
  rcyaw = channels[2];
  rcpit = channels[1];
  rcroll = channels[0];
  
  // Ask MPU6050 for orientation. Current angle.
  ahrs.update();
  float roll,pitch,yaw;  
  roll = ToDeg(ahrs.roll) + rdev;
  pitch = ToDeg(ahrs.pitch) + pdev;
  yaw = ToDeg(ahrs.yaw) ;
  
  if((hal.scheduler->millis() - time_TPRY) > 1000)
  {
    time_TPRY = hal.scheduler->millis();
    hal.console->printf_P(PSTR("T:%ld R:%4.2f P:%4.2f Y:%4.0f\n"), rcthr,roll,pitch,yaw);
  }
  
  // Ask MPU6050 for gyro data. Rotational velocity
  Vector3f gyro = ins.get_gyro();
  float gyroPitch = ToDeg(gyro.y), gyroRoll = ToDeg(gyro.x), gyroYaw = ToDeg(gyro.z);
  
  // Do the magic
  if(rcthr > RC_THR_MIN + 50) {  // Throttle raised, turn on stabilization.
    // Stabilize PIDS
    float pitch_stab_output = constrain_float(pids[PID_PITCH_STAB].get_pid((float)rcpit - pitch, 1), -250, 250); // error = desired_input - real_reading
    float roll_stab_output = constrain_float(pids[PID_ROLL_STAB].get_pid((float)rcroll - roll, 1), -250, 250);
    float yaw_stab_output = constrain_float(pids[PID_YAW_STAB].get_pid(wrap_180(yaw_target - yaw), 1), -360, 360);
  
    // is pilot asking for yaw change - if so feed directly to rate pid (overwriting yaw stab output)
    if(abs(rcyaw ) > 5) {
      yaw_stab_output = rcyaw;
      yaw_target = yaw;   // remember this yaw for when pilot stops
    }
    
    // rate PIDS
    long pitch_output =  (long) constrain_int32(pids[PID_PITCH_RATE].get_pid(pitch_stab_output - gyroPitch, 1), - 500, 500);
    long roll_output =  (long) constrain_int32(pids[PID_ROLL_RATE].get_pid(roll_stab_output - gyroRoll, 1), -500, 500);
    long yaw_output =  (long) constrain_int32(pids[PID_ROLL_RATE].get_pid(yaw_stab_output - gyroYaw, 1), -500, 500);

    // mix pid outputs and send to the motors.
    hal.rcout->write(MOTOR_FL, rcthr + roll_output + pitch_output - yaw_output);
    hal.rcout->write(MOTOR_BL, rcthr + roll_output - pitch_output + yaw_output);
    hal.rcout->write(MOTOR_FR, rcthr - roll_output + pitch_output + yaw_output);
    hal.rcout->write(MOTOR_BR, rcthr - roll_output - pitch_output - yaw_output);
  } else {
    // motors off
    hal.rcout->write(MOTOR_FL, 1000);
    hal.rcout->write(MOTOR_BL, 1000);
    hal.rcout->write(MOTOR_FR, 1000);
    hal.rcout->write(MOTOR_BR, 1000);
       
    // reset yaw target so we maintain this on takeoff
    yaw_target = yaw;
    
    // reset PID integrals whilst on the ground
    for(int i=0; i<6; i++)
      pids[i].reset_I();

  }
}

AP_HAL_MAIN();



