gpio mode 1 pwm
gpio pwm-ms
gpio pwmr 1000
gpio pwmc 48
gpio pwm 1 800 # maximum throttle = 2ms
read -p "connect Power...max throttle, wait 2s"
sleep 2
read -p "beep-beep?"
sleep 5
read -p "56712 sound, select beep---beep-beep"
gpio pwm 1 400 # minimum throttle = 1ms
sleep 3
#read -p "Slightly open throttle"
#gpio pwm 1 450
#read -p "stop throttle"
#gpio pwm 1 400