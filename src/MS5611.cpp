#include "MS5611.h"
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "stdio.h"
#include <inttypes.h>
#include "tools.h"
#include "param.h"
#include "hardware/watchdog.h"

// datasheet page 10
#define MS5611_CMD_READ_ADC       0x00
#define MS5611_CMD_READ_PROM      0xA0
#define MS5611_CMD_RESET          0x1E
#define MS5611_CMD_CONVERT_D1     0x40
#define MS5611_CMD_CONVERT_D2     0x50

extern CONFIG config;

/////////////////////////////////////////////////////
//
// PUBLIC
//
MS5611::MS5611(uint8_t deviceAddress)
{
  _address           = deviceAddress;
  _result            = MS5611_NOT_READ;
  _lastRead          = 0;
  _state             = UNDEFINED;
  _D1                = 0;
  _D2                = 0;
  _D2Prev            = 0;
  
}

int8_t MS5611::ms56xx_crc(uint16_t *prom)
{
    int32_t i, j;
    uint32_t res = 0;
    uint8_t crc = prom[7] & 0xF;
    prom[7] &= 0xFF00;

    bool blankEeprom = true;

    for (i = 0; i < 16; i++) {
        if (prom[i >> 1]) {
            blankEeprom = false;
        }
        if (i & 1)
            res ^= ((prom[i >> 1]) & 0x00FF);
        else
            res ^= (prom[i >> 1] >> 8);
        for (j = 8; j > 0; j--) {
            if (res & 0x8000)
                res ^= 0x1800;
            res <<= 1;
        }
    }
    prom[7] |= crc;
    if (!blankEeprom && crc == ((res >> 12) & 0xF))
        return 0;

    return -1;
}


void MS5611::begin()  // return true when baro exist
{
  baroInstalled = false;
  if ( config.pinScl == 255 or config.pinSda == 255) return; // skip if pins are not defined
  uint8_t rxdata;
  rxdata = MS5611_CMD_RESET ;
  //printf("before baro reset\n");
  if (i2c_write_timeout_us (i2c1 , _address, &rxdata , 1 , false, 1000 ) <0 ) {// ask for a reset
    printf("error write reset MS5611\n");
    return;
  }  
  sleep_ms(10) ; // wait that data are loaded from eprom to memory (2.8msec in data sheet)
  //printf("before reading baro config\n");
  
  // read factory calibrations from EEPROM.
  for (uint8_t reg = 0; reg < 8; reg++)
  {
      //sleep_ms(1000);watchdog_enable(1500, 0);sleep_ms(1000);watchdog_enable(1500, 0);
      uint8_t readBuffer[2];
      _calibrationData[reg] = 0;
      rxdata = MS5611_CMD_READ_PROM + (reg <<1) ; // this is the address to be read
      if ( i2c_write_timeout_us (i2c1 , _address, &rxdata , 1 , false , 1000) <0 ) {
        printf("error write calibration MS5611\n");
        return ; // command to get access to one register '0xA0 + 2* offset
    //  sleep_ms(1);
      }
      if ( i2c_read_timeout_us (i2c1 , _address , &readBuffer[0] , 2 , false, 1500) < 0)  {
        printf("error read calibration MS5611\n");
        return ;
      }  
      _calibrationData[reg] = (readBuffer[0]<<8 ) | (readBuffer[1] );
      //printf("cal=%x\n",_calibrationData[reg]) ;    
  }
  if (ms56xx_crc(_calibrationData) != 0) return;  // Check the crc
  //watchdog_enable(1500, 0);
  //rxdata = MS5611_CMD_READ_PROM + 0 ; // this is the address to be read
  //printf("write i2c %d\n" ,  i2c_write_blocking (i2c1 , _address, &rxdata , 1 , false) );
  //printf("readI2C baro1 %d\n",i2c_read_blocking (i2c1 , _address, &rxdata , 1 , false) );
  //printf("generic %d\n",PICO_ERROR_GENERIC );  
  baroInstalled = true; // if we reach this point, baro is installed (and calibration is loaded)
}


void MS5611::command(const uint8_t command) // send a command. return 0 if succeeded, else -1)
{
  uint8_t cmd = command;
  _result = 0 ;
  if ( i2c_write_timeout_us (i2c1 , _address, &cmd , 1 , false, 1000) <0 ) { // i2c_write return the number of byte written or an error code
     printf("error write MS5611 cmd\n");
    _result = -1 ;  // Error
  } 
}

uint32_t MS5611::readADC() // returned value = 0 in case of error (and _result is not 0)
{
  uint8_t buffer[3] ;
  uint32_t adcValue = 0 ;
  command(MS5611_CMD_READ_ADC);
  if (_result == 0)
  {
    _result = 1; // error
    if ( i2c_read_timeout_us (i2c1 , _address , &buffer[0] , 3 , false, 1500) != PICO_ERROR_TIMEOUT) {
      adcValue = (buffer[0] << 16) | (buffer[1] << 8) | buffer[2];
      _result = 0 ; // no error
    } else {
        printf("read error MS5611\n");
    }
  }
  if (_result ) adcValue = 0; //  
//  printf("adc %" PRIu32 "\n", adcValue);
  return adcValue ;
}


// -- END OF FILE --
// Try to get a new pressure 
// MS5611 requires some delay between asking for a conversion and getting the result
// in order not to block the process, we use a state
// If state = 1, it means we asked for a pressure conversion; we can read it if delay is long enough; we ask for a temp conversion
// If state = 2, it means we asked for a temperature conversion; we can read it if delay is long enough
// if state = 0, we have to
// return 0 if a new value is calculated; -1 if no calculation was performed; other in case of I2C error
// when a value is calculated, then altitude, rawPressure and temperature are calculated.
int MS5611::getAltitude() // Try to get a new pressure ; 
{
  if ( ! baroInstalled) return -1;     // do not process if there is no baro; -1 = no new data
  if ( (microsRp() - _lastConversionRequest) < 9500 ) // it take about 9000 usec for a conversion
    return -1;
  //printf("state=%X\n",_state);  
  switch (_state) 
  {
  case UNDEFINED :   
    command(0x48); // ask for pressure conversion in high resolution
    if (_result) return _result;
    _state = WAIT_FOR_PRESSURE ;
    _lastConversionRequest = microsRp() ;      
    break;
  case WAIT_FOR_PRESSURE :   
    _D1 = readADC(); // read pressure, return 0 in case of error; _result =0 if OK.
    if (_result) return _result;
    command(0x58); // ask immediately for temperature conversion in high resolution
    if (_result) return _result;
    _state = WAIT_FOR_TEMPERATURE ;
    _lastConversionRequest = microsRp() ;
    _lastTempRequest = _lastConversionRequest; 
    break ;  
  case WAIT_FOR_TEMPERATURE : 
    _D2 = readADC(); // read temperature, return 0 in case of error; _result =0 if OK.
    //printf("result=%X\n",_result);
    //printf("cal=%x\n",_calibrationData[2]);
    if (_result) return _result;
    command(0x48); // ask for pressure conversion in high resolution
    if (_result) return _result;
    _state = WAIT_FOR_PRESSURE ;
    _lastConversionRequest = microsRp() ;      
    calculateAltitude();
    return 0 ; // new data available
    break;
  }
  return -1 ; // no new data 
}

void MS5611::calculateAltitude(){
  //printf("+\n");
  //printf("testInit=%d\n", testInit);
  if (_D2Prev == 0)
  {
    _D2Prev = _D2;
    _prevAltMicros = _lastTempRequest ;
     
  }
  //printf("%" PRIu32 " %" PRIu32 "\n",_D1,_D2) ;
  //printf("%x  %x %x %x %x %x\n",_calibrationData[1], _calibrationData[2], _calibrationData[3], _calibrationData[4] ,_calibrationData[5] ,_calibrationData[6]  ); 
  //      _D2 = 0X825AF8;
  //      _D2Prev = _D2;
  //      _D1 = 0X80777E;  
  int64_t dT = ((int64_t)((_D2+ _D2Prev) >> 1 )) - (( (uint64_t)_calibrationData[5]) << 8) ;
  int32_t TEMP = 2000 + ((dT * ((int64_t)_calibrationData[6])) >> 23)  ;
  temperature = TEMP;
  _D2Prev = _D2 ;
  int64_t OFF  = (((int64_t)_calibrationData[2]) << 16) + ((((int64_t)_calibrationData[4]) * dT) >> 7);
  int64_t SENS = (((int64_t)_calibrationData[1]) << 15) + ((((int64_t)_calibrationData[3]) * dT) >> 8);
  int64_t rawPressure= (((((((int64_t) _D1) * SENS) >> 21) - OFF) * 10000 ) >> 15) ; // 1013.25 mb gives 1013250000 is a factor to keep higher precision (=1/100 cm).
  
  //printf("%d\n", temperature);
  //#define DEBUG_MS5611_ALT 
  #ifdef DEBUG_MS5611_ALT  
  static bool first = true ;
  if (first ) { 
        printf("dT=%X%X\n" , (uint32_t)((dT >> 32) & 0xFFFFFFFF) , (uint32_t)(dT & 0xFFFFFFFF) );
        printf("temp=%X%X\n" , (uint32_t)((temperature >> 32) & 0xFFFFFFFF) , (uint32_t)(temperature & 0xFFFFFFFF) ); 
        printf("OFF=%X%X\n" , (uint32_t)((OFF >> 32) & 0xFFFFFFFF) , (uint32_t)(OFF & 0xFFFFFFFF) );
        printf("SENS=%x%x\n" , (uint32_t)((SENS >> 32) & 0xFFFFFFFF),(uint32_t)(SENS & 0xFFFFFFFF) );
        printf("pressure=%X%X\n" , (uint32_t)((rawPressure >> 32) & 0xFFFFFFFF) , (uint32_t)(rawPressure & 0xFFFFFFFF) );
        first = false;
  }
  #endif  

        // altitude = 44330 * (1.0 - pow(pressure /sealevelPressure,0.1903));
      // other alternative (faster) = 1013.25 = 0 m , 954.61 = 500m , etc...
      //      Pressure	Alt (m)	Ratio
      //      101325	0	0.08526603
      //      95461	500	0.089525515
      //      89876	1000	0.094732853
      //      84598	1500	0.098039216
      //      79498	2000	0.103906899
      //      74686	2500	0.109313511
      //      70112	3000	0.115101289
      //      65768	3500	0.121270919
      //      61645	4000	0.127811861
      //      57733	4500	0.134843581
      //      54025	5000	
  //printf("D1: %" PRIi32 "   ", _D1);
  //printf("D2: %" PRIi32 "   ", _D2);
  //printf("temp: %" PRIi32 "   ", TEMP);
  float rp = rawPressure / 1000000.0;
  if ( rawPressure > 954610000) {
    altitude = ( 1013250000 - rawPressure ) * 0.08526603 ; // = 500 / (101325 - 95461)  // returned value 1234567 means 123,4567 m (temp is fixed to 15 degree celcius)
    //printf("-");
  } else if ( rawPressure > 898760000) {
    altitude = 5000000 + ( 954610000 - rawPressure ) * 0.089525515  ; 
  } else if ( rawPressure > 845980000) {
    altitude = 10000000 + ( 898760000 - rawPressure ) * 0.094732853  ; 
  } else if ( rawPressure > 794980000) {
    altitude = 15000000 + ( 845980000 - rawPressure ) *  0.098039216 ; 
  } else if ( rawPressure > 746860000) {
    altitude = 20000000 + ( 794980000 - rawPressure ) *  0.103906899 ; 
  } else if ( rawPressure > 701120000) {
    altitude = 25000000 + ( 746860000 - rawPressure ) *  0.109313511 ; 
  } else if ( rawPressure > 657680000) {
    altitude = 30000000 + ( 701120000 - rawPressure ) *  0.115101289 ; 
  } else if ( rawPressure > 616450000) {
    altitude = 35000000 + ( 657680000 - rawPressure ) *  0.121270919 ; 
  } else if ( rawPressure > 577330000) {
    altitude = 40000000 + ( 616450000 - rawPressure ) *  0.127811861 ;
  } else {    altitude = 45000000 + ( 577330000 - rawPressure ) *  0.134843581 ;
  }
  //printf("raw %f\n",rp);
  //printf("  alt:  %" PRId32 "\n"   ,  altitude/10000);
  //printf("  alt:  %f\n"   ,  ((float) altitude)/10000);
  //printf("calib: %" PRIu16 " %" PRIu16 " %" PRIu16 " %" PRIu16 " %" PRIu16  " %" PRIu16 "\n" , 
  //  _calibrationData[1] , _calibrationData[2] ,_calibrationData[3] ,_calibrationData[4] , _calibrationData[5], _calibrationData[6]);
  altIntervalMicros = _lastTempRequest - _prevAltMicros;
  _prevAltMicros = _lastTempRequest ; 
}

