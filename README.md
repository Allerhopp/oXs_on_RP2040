# expressLRS / FRSKY / HOTT / JETI / MPX / FLYSKY openXsensor (oXs) on RP2040 board

This project can be interfaced with 1 or 2 ELRS, FRSKY , HOTT , MPX, FLYSKY or Jeti receiver(s) (protocol has to be selected accordingly).
 
This project is foreseen to generate telemetry data (e.g. when a flight controller is not used) , PWM and Sbus signals.
 
For telemetry, it can provide
 
 * up to 4 analog voltages measurement (with scaling and offset) without external ADC 
 * the altitude and the vertical speed when connected to a pressure sensor (optional) (and optionally a MP6050 acc/gyro)
 * GPS data (longitude, latitude, speed, altitude,...) (optional)
 * RPM (requires some composant to generate pulses) (optional)
 * up to 8 additional analog voltages (requires up to 2 ADS1115)

It can also provide up to 16 PWM RC channels from a CRSF (ELRS) or a Sbus (Frsky/Jeti) signal.

It can provide diversity when connected to 2 receivers: the generated PWM and Sbus signals will be issued from the last received Rc channels.

## -------  Hardware -----------------

This project requires a board with a RP2040 processor (like the rapsberry pi pico).

A better alternative is the RP2040-Zero (same processor but smaller board)

This board can be connected to:
* a pressure sensor to get altitude and vertical speed. It can be
   * a GY63 or a GY86 board based on MS5611
   * a SPL06-001 sensor
   * a BMP280 sensor
* a MP6050 (acc+gyro) to improve reaction time of the vario or to get pitch/roll
* a GPS from UBlox (like the BEITIAN bn220) or one that support CASIC messages

       note : a Ublox GPS has to use the default standard config. It will be automatically reconfigure by this firmware  
       
       a CASIC gps has to be configured before use in order to generate only NAV-PV messages at 38400 bauds  
       
       This can be done using a FTDI and the program GnssToolkit3.exe (to download from internet)
* some voltage dividers (=2 resistors) when the voltages to measure exceeds 3V

      note : a voltage can be used to measure e.g. a current when some external devices are use to generate an analog voltage 
* 1 or 2 ADS1115 if you want to measure more than 4 analog voltage

## --------- Wiring --------------------

* FRSKY/ELRS receiver, baro sensor GPS, ... must share the same Gnd
* Connect a 5V source to the Vcc pin of RP2040 board (attention max input voltage of RP2040-Zero is 5.5Volt)  
* Select the functions and pins being used (most are optional)
* The config parameters allow to select:

   * the pins used to generate PWM channels (Gpio0 up to Gpio15) 

   * a pin (within Gpio5 ,9, 21 or 25) that get the Rc channels and is connected to one ELRS receiver or to the SBus pin (FRSKY/JETI...).

   * a pin (within Gpio1, 13 , 17 or29) that get the Rc channels and is connected to a second ELRS receiver or a second SBus receiver.

   * a pin used to generate a Sbus signal (gpio 0...29) (from the ELRS signal(s) or by "merging" the 2 Sbus

   * a pin used to transmit telemetry data (gpio 0...29) (connected to ELRS Rx/Frsky Sport/Jeti Ex)

   * the (max 4) pins used to measure voltages (gpio 26...29)
    
   * a pin used to measure RPM (gpio 0...29)
   
   * the 2 pins used for GPS (gpio 0...29) (RP2040 pin defined as GPS-TX is connected GPS Tx pin and GPS-RX to GPS RX pin)
   
   * the 2 pins connected to I2C sensor (baro, MP6050, ADS1115) (SDA=2, 6, 10, 14, 18, 22, 26) (SCL=3, 7, 11, 15, 19, 23, 27)


   Take care to use a voltage divider (2 resistances) in order to limit the voltage on the pins to 3V max (e.g. when you want to measure higher voltages)

* When a baro sensor and/or MP6050 and/or ADS115 are used:

   * Connect the 3V pin from RP2040 board to the 5V pin of GY63/GY86 or the Vcc of SPL06/BMP280/MP6050/ADS1115  

   Note: do not connect 5V pin of GY63/GY86/ADS1115 to a 5V source because the SDA and SCL would then be at 5V level and would damage the RP2040          

* When a GPS is used:

   * Connect the 3V pin from RP2040 board to the Vin/5V pin from GPS

* For more details, look at file named "general & setup.txt" in the "doc" folder

## --------- Software -------------------
This software has been developped using the RP2040 SDK provided by Rapsberry.

It uses as IDE platformio and the WIZIO extension (to be found on internet here : https://github.com/Wiz-IO/wizio-pico )
It can also be compiled with Platformio CLI (which requires lees installation than platformio IDE)

Developers can compile and flash this software with those tools.

Still if you just want to use it, there is no need to install/use those tools.

On github, in uf2 folder, there is already a compiled version of this software that can be directly uploaded and configured afterwards

To upload this compiled version, the process is the folowing:
* download the file in folder uf2 on your pc
* insert the USB cable in the RP2040 board
* press on the "boot" button on the RP2040 board while you insert the USB cable in your PC.
* this will enter a special bootloader mode and your pc should show a new drive named RPI-RP2
* copy and paste the uf2 file to this new drive
* the file should be automatically picked up by the RP2040 bootloader and flashed
* the RPI_RP2 drive should disapear from the PC and the PC shoud now have a new serial port (COMx on windows)

Once the firmware is uploaded and running the led (when a RP2040-Zero is used) will blink or be on (see below).
The firmware must still be configured (to specify the pins, protocol, sensors... being used):
* You can now use a serial terminal (like serial monitor in visual code , the one from arduino IDE, ...) and set it up for 115200 baud 8N1
* While the RP2040 is connected to the pc with the USB cable, connect this serial terminal to the serial port from the RP2040
* When the RP2040 starts (or pressing the reset button), press the enter key and it will display the current configuration
* Press "?"+ ENTER to get the list of commands.
* To change some parameters, fill in a command using a format xxxx=yyyyy and press the enter.
* The RP2040 should then display the new (saved) config.
* When ADS1115 are used, you can configure quite many additional parameters but this requires to edit the config.h file and compile your self.  

Notes for ELRS:

The RP2040 sent the telemetry data to the ELRS receiver at some speed.
This speed (=baud rate) must be the same as the baudrate defined on the receiver
Usually ELRS receiver uses a baudrate of 420000 to transmit the CRSF channels signal to the flight controller and to get the telemetry data.
Still, ELRS receivers can be configured to use another baud rate. In this case, change the baudrate in parameters accordingly


## --------- Failsafe---------------
* For ELRS protocol, oXs does not received any RC channels data from the receiver(s) when RF connection is lost. If oXs is connected to 2 receivers (via PRI and SEC), oXs will generate PWM and Sbus signals on the last received data. If oXs does not get any data anymore from receiver(s), it will still continue to generate PWM and/or SBUS signals based on the failsafe setup stored inside oXs.


* For Frsky/Jeti... protocols where Sbus is used, the failsafe values are normally defined inside the receiver and the receiver continue to generate a Sbus signal even if the RF connection is lost. Still, when connection is lost Sbus signal contains some flags that say that some data are missing or that failsafe has been applied. When oXs is connected to 2 different receivers, it gives priority to PRI sbus signal except when SEC signal is OK and PRI is not OK (no signal, missing frame, failsafe). So for Frsky/Jeti, oXs does not have to take care of his own failsafe setup (except if oXs would not get any Sbus signal anymore - e.g due a wiring issue).   

* For failsafe oXs has 3 options:
    * "Hold" = failsafe will be the last Rc channels values known just before connection is lost; to select this option, use the serial interface with the command "FAILSAFE=H"
    * store as failsafe the current RC channels values using the serial interface with command "SETFAILSAFE".
    *  store as failsafe the current RC channels values using the "boot" button on the RP2040. To activate this option, doubble click the button. Led should become fixed blue. In the next 5 seconds, press and hold the "boot" button. Led will become white for 2 seconds confirming that values are saved in the config.
    
For the 2 last options, the handset must be on and generating the channels values that you want to save in oXs.

## --------- Led -------------------
When a RP2040-Zero is used, the firmware will handle a RGB led (internally connected to gpio16).
* when config is wrong, led is red and ON.
* when config is valid, led is blinking and the color depends on RC channels being received ot not
    * Red = Rc frames have nerver been received, Sbus and/or PWM signals are not generated.
    * Blue = Sbus and/or PWM signals are based on failsafe values. Failsafe values are given by the receiver for Sbus or are configured in oXs for CRSF protocol)
    * Yellow/oranje = Sbus and/or PWM signals are based on a valid RC channels frame received from PRI or SEC source but the orther source does not provided a valid RC channels frame.
    * blinking green = Sbus and/or PWM signals are based on valid RC channels frames (from one source; from both sources if both are foressen in the setup)
* when "Boot" button is used for setting the failsafe values, led becomes blue and white (see above)


Please note that other boards does not have a RGB led on gpio16 and so this does not applies.
