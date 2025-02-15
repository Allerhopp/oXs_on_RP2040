#include "pico/stdlib.h"
#include "stdio.h"  // used by printf
#include "config.h"
#include <param.h>
#include "hardware/uart.h"
#include "hardware/timer.h"
#include "hardware/irq.h"
#include "sbus2_tlm.h"
#include "math.h"

//#include "pico/util/queue.h"
//#include "sbus_in.h"
//#include "crsf.h"
#include "tools.h"


// manage the telemetry for futaba
// This uses the same UART as the one used to receive the PRI Sbus signal so it is uart1
// As this needs a uart Tx pin, this pin is the uart1 RX pin - 1 and should be defined as TLM
// It is safe to use a 1K resistor between RX and TX pin and to connect Rx pin to the receiver 

//for each group of sensors (vario, battery, gps) add a setup in config.h to specify the slot being used
// GPS uses slot 8, 16 or 24 (and following)
// Vario uses 2 slots consecutive slots
// Battery uses 3 slots (volt, current, capacity) 
// Temp1 and Temp2 use each 1 slot



// when a full Sbus frame is received, set an alarm 2msec after and fill the first slot to be sent
// when the alarm fires,  sent the frame and prepare a new alarm for next slot if any 

// to do : add a check about config that Prim Rx pin is not used for another purpose when futaba protocol is selected 
// and check that TLM pin = PRI pin -1

/****************************************************************
 * transmit_frame
 * 
 * Implemented as an ISR to fit the strict timing requirements
 * Slot 1 has to be transmitted 2ms after receiving the SBUS2 Frame
 * Slot 2-8 has to be transmitted every 660 mirco seconds
 * When a data is not available the slot is unused (but not reused for another data)
 * Each Slot needs about 320 mircoseconds to be transmitted via uart
 * The delay between the Slots must not exceed 400 mirco seconds
 * 
 * so we wait to receive a RC frame.
 * * when a valid frame is received, we stop listening and we prepare transmitting
 * if byte 24 is 04,14,23 or 34 then ir is a SBUS2 and we can reply
 * we save the 0,1,2,3 (tlm frame counter), set sequence = 0 and start then a timer for 2msec
 * when it fires, we look if sequence is < 8 
 * if < 8, and if the data is available for slot sequence + frame counter *8, we fill the buffer and transmit it
 * if < 8 and data is not available, set a new alarm 660 later
 * In both cases we increment sequence
 * if == 8, we stop transmitting (we start receiving)
 * 
 * Attention:  the RX can probably sent some telemetry frame him self in slot 0
 ****************************************************************/

extern CONFIG config;
extern field fields[];  // list of all telemetry fields and parameters used by Sport


// 32 Slots for telemetrie Data
uint8_t slotId[32] =           {0x03, 0x83, 0x43, 0xC3, 0x23, 0xA3, 0x63, 0xE3,
                                 0x13, 0x93, 0x53, 0xD3, 0x33, 0xB3, 0x73, 0xF3,
                                 0x0B, 0x8B, 0x4B, 0xCB, 0x2B, 0xAB, 0x6B, 0xEB,
                                 0x1B, 0x9B, 0x5B, 0xDB, 0x3B, 0xBB, 0x7B, 0xFB};

bool slotAvailable[8] = {false}; // 0 = Not available = do not transmit
uint8_t slotValueByte1[8] = {0};  // value to transmit
uint8_t slotValueByte2[8] = {0};  // value to transmit

volatile uint8_t firstSlot32Idx ; 
volatile uint8_t slotNr;     // 0/7 depending of the slot being sent between 2 Rc frames 


void setupSbus2Tlm(){
    // setup of uart Tx pin : nothing is required ; only Tx pin must be enabled
    // setup of uart for receive and send : will be done while running
    // init list of slot codes used by futaba as first byte (32 values)
    // init list of slot being used (based on the config parameters)
    
    // GPIO pin Tx as GPIO input
    //gpio_init(config.pinTlm);
    //gpio_set_dir(config.pinTlm, GPIO_IN);
    //gpio_pull_down(config.pinTlm); 
    
    // !!! to be move
    gpio_set_function( config.pinTlm, GPIO_FUNC_UART);
    gpio_set_outover( config.pinTlm , GPIO_OVERRIDE_INVERT);
    
    
    //gpio_set_function( config.pinTlm,  GPIO_FUNC_UART);
    //gpio_set_inover( config.pinTlm, GPIO_OVERRIDE_INVERT);
    #ifdef SIMULATE_SBUS2_ON_PIN
        setupBus2Simulation();    
    #endif


}

#include "hardware/address_mapped.h"
#include "hardware/platform_defs.h"
#include "hardware/uart.h"

#include "hardware/structs/uart.h"
#include "hardware/resets.h"
#include "hardware/clocks.h"
#include "hardware/timer.h"

#include "pico/assert.h"
#include "pico.h"

/// \tag::uart_reset[]
static inline void uart_reset_ms(uart_inst_t *uart) {
    invalid_params_if(UART, uart != uart0 && uart != uart1);
    reset_block(uart_get_index(uart) ? RESETS_RESET_UART1_BITS : RESETS_RESET_UART0_BITS);
}

static inline void uart_unreset_ms(uart_inst_t *uart) {
    invalid_params_if(UART, uart != uart0 && uart != uart1);
    unreset_block_wait(uart_get_index(uart) ? RESETS_RESET_UART1_BITS : RESETS_RESET_UART0_BITS);
}
/// \end::uart_reset[]

void enableSbus2Transmit(){
//    if (clock_get_hz(clk_peri) == 0)
//        return ;
//
//    uart_reset_ms(uart1);
//    uart_unreset_ms(uart1);
//
//#if PICO_UART_ENABLE_CRLF_SUPPORT
//    uart_set_translate_crlf(uart1, PICO_UART_DEFAULT_CRLF);
//#endif

    // Any LCR writes need to take place before enabling the UART
//    uint baud = uart_set_baudrate(uart1, 100000);
//    uart_set_format(uart1, 8, 2, UART_PARITY_EVEN);
    // Enable FIFOs
    uart_get_hw(uart1)->cr = 0 ; // disable UART    
    while ( uart_get_hw(uart1)->fr & UART_UARTFR_BUSY_BITS) {}; // wait that UART is not busy 
    hw_set_bits(&uart_get_hw(uart1)->lcr_h, UART_UARTLCR_H_FEN_BITS); // enable fifo for transmit
    uart_get_hw(uart1)->cr = UART_UARTCR_UARTEN_BITS | UART_UARTCR_TXE_BITS ; // this seems required when uart is not 8N1 (bug in sdk)    

    gpio_set_outover( config.pinTlm , GPIO_OVERRIDE_INVERT);
    gpio_set_function( config.pinTlm, GPIO_FUNC_UART);
    // !!!!!!!!! here we should also disable the interrupts the Rx UART (???)
}
/// \end::uart_init[]

void disableSbus2Transmit(){
    gpio_init(config.pinTlm);  // disconect Tx pin
    gpio_set_dir(config.pinTlm, GPIO_IN); // configure as input (high impedance)
//    gpio_pull_down(config.pinTlm);
    uart_get_hw(uart1)->cr = 0 ; // disable UART    
    while ( uart_get_hw(uart1)->fr & UART_UARTFR_BUSY_BITS) {}; // wait that UART is not busy 
    hw_clear_bits(&uart_get_hw(uart1)->lcr_h, UART_UARTLCR_H_FEN_LSB);
    uart_get_hw(uart1)->cr = UART_UARTCR_UARTEN_BITS | UART_UARTCR_RXE_BITS ; // enable uart    
//  
  
    //uart_set_fifo_enabled(uart1 ,  true); // is already enabled
}

void fill8Sbus2Slots (uint8_t slotGroup){
    add_alarm_in_us(2000, sendNextSlot_callback, NULL, false); 
    slotNr= 0;
    firstSlot32Idx = slotGroup * 8; 
    for (uint8_t i=0 ; i<8 ; i++){  //reset all flags
        slotAvailable[i] = false;         
    }
    if ((fields[VSPEED].available) && ( slotGroup == (SBUS2_SLOT_VARIO_2 >>3))){
        fillVario( SBUS2_SLOT_VARIO_2 & 0x07); // keep 3 last bits as slot index
    }
    if (( (fields[MVOLT].available) || (fields[CURRENT].available) ) && ( slotGroup == (SBUS2_SLOT_BATTERY_3 >>3))){
        fillBattery( SBUS2_SLOT_BATTERY_3 & 0x07);
    }
    if ((fields[TEMP1].available) && ( slotGroup == (SBUS2_SLOT_TEMP1_1 >>3))){
        fillTemp1(SBUS2_SLOT_TEMP1_1 & 0x07); // keep 3 last bits as slot index
    }
    if ((fields[TEMP2].available) && ( slotGroup == (SBUS2_SLOT_TEMP2_1 >>3))){
        fillTemp2(SBUS2_SLOT_TEMP2_1  & 0x07); // keep 3 last bits as slot index
    }
    if ((fields[RPM].available) && ( slotGroup == (SBUS2_SLOT_RPM_1 >>3))){
        fillRpm(SBUS2_SLOT_RPM_1  & 0x07); // keep 3 last bits as slot index
    }
    if ((fields[LONGITUDE].available) && ( slotGroup == (SBUS2_SLOT_GPS_8 >>3))){
        fillGps(SBUS2_SLOT_GPS_8  & 0x07); // keep 3 last bits as slot index
    }
    //enableSbus2Transmit();   
}

int64_t sendNextSlot_callback(alarm_id_t id, void *user_data){ // sent the next callback (or stop sending after 8 slots)
    if (slotNr < 8){
        //add_alarm_in_us(660, sendNextSlot_callback, NULL, false); 
        if (slotAvailable[slotNr] ) {
            uart_putc_raw(uart1 , (char) slotId[slotNr + firstSlot32Idx] ) ;
            uart_putc_raw(uart1 , (char) slotValueByte1[slotNr] );      /// !!!!!!!!!! perhaps to reverse
            uart_putc_raw(uart1 , (char) slotValueByte2[slotNr] );
            }
        slotNr++;
        return -660;  // generates a new alarm 660 usec after this one
    } else {
        //disableSbus2Transmit();
        return 0;      
    }
}

void fillTemp1(uint8_t slot8){ // emulate SBS/01T ; Temp in °
    int16_t value=  fields[TEMP1].value;
    value |= 0x8000;
    value = value + 100;
    slotAvailable[slot8] = true;
    slotValueByte1[slot8] = value;// >> 8;
    slotValueByte2[slot8] = value >> 8;
}

void fillTemp2(uint8_t slot8){
    int16_t value=  fields[TEMP2].value;
    value |= 0x8000;
    value = value + 100;
    slotAvailable[slot8] = true;
    slotValueByte1[slot8] = value;// >> 8;
    slotValueByte2[slot8] = value >> 8;

}

void fillVario(uint8_t slot8){ // emulate F1672 ; Alt from cm to m ; Vspeed from cm/s to 0.1m/s
    // Vspeed
    int16_t value = int_round(fields[VSPEED].value, 10);
    slotAvailable[slot8] = true;
    slotValueByte1[slot8] = value >> 8;
    slotValueByte2[slot8] = value;// >> 8;
    //Alt
    value = int_round(fields[RELATIVEALT].value, 100);
    slotAvailable[slot8+1] = true;
    slotValueByte1[slot8+1] = value >> 8;
    slotValueByte2[slot8+1] = value;// >> 8;
}


void fillBattery(uint8_t slot8){   // emulate SBS/01C ; Current from mA to 0.1A; volt from mV to 0.1V ; capacity in mAh
    uint16_t value = 0;
    uint32_t local = 0;
   // CURRENT
   if ( fields[CURRENT].value > 0 ) {
        value = (uint16_t) int_round(fields[CURRENT].value, 100);
   } else {
        value = 0;
   }       
   if ( value > 0x3FFF ){ // max current is 163.83
      value = 0x3FFF;
   }  
   slotAvailable[slot8] = true;
   slotValueByte1[slot8] = ((value >> 8) | 0x40) & 0x7F;
   slotValueByte2[slot8] = value;// >> 8;

   //VOLTAGE
   if ( fields[MVOLT].value > 0 ){
         value = (uint16_t) int_round(fields[MVOLT].value, 100);
   } else {
        value = 0;
   }  
   slotAvailable[slot8 + 1] = true;
   slotValueByte1[slot8 + 1] = value >> 8;
   slotValueByte2[slot8 + 1] = value;
   
   // CAPACITY
   local = (uint32_t) fields[CAPACITY].value ;
   if ( fields[CAPACITY].value > 0 ){
         value = (uint16_t)local;
   } else {
        value = 0;
   }
   slotAvailable[slot8+2] = true;
   slotValueByte1[slot8 + 2] = value >> 8;
   slotValueByte2[slot8 + 2] = value;
}


void fillRpm(uint8_t slot8){ // emulate SBS01RO ; in from Hz to 0.1 RPM 
    uint32_t value =  fields[CAPACITY].value * 10;
    if(value > 0xffff){
    value = 0xffff;
    }
   slotAvailable[slot8] = true;
   slotValueByte1[slot8] = value >> 8;
   slotValueByte2[slot8] = value;
}

void fillGps(uint8_t slot8){ // emulate SBS01G  ; speed from  to Km/h ; Alt from ??? to m ; vario to m/s
//    uint16_t hours,          // 0 to 24
//    uint16_t minutes,        // 0 to 60
//    uint16_t seconds,        // 0 to 60
//    float latitude,          // decimal degrees (i.e. 52.520833; negative value for southern hemisphere)
//    float longitude,         // decimal degrees (i.e. 13.409430; negative value for western hemisphere)
//    float altitudeMeters,    // meters (valid range: -1050 to 4600)
//    uint16_t speed,          // km/h (valid range 0 to 511)
//    float gpsVario)          // m/s (valid range: -150 to 260)
   
    //setup the data in format used by the original code
    uint16_t hours = 0;
    uint16_t minutes = 0;
    uint16_t seconds = 0;
    if (fields[GPS_TIME].available) {
        hours = fields[GPS_TIME].value >> 24 ;
        minutes = fields[GPS_TIME].value >> 16 ;
        seconds = fields[GPS_TIME].value >> 8 ;
    }
    uint32_t utc = (hours*3600) + (minutes*60) + seconds;
    float latitude = 0;
    float longitude = 0 ; 
    if (fields[LONGITUDE].available) longitude = (float) fields[LONGITUDE].value;
    if (fields[LATITUDE].available) latitude = (float) fields[LATITUDE].value;
    float altitudeMeters = 0 ;    // meters (valid range: -1050 to 4600)
    if (fields[ALTITUDE].available) altitudeMeters = ((float) fields[ALTITUDE].value)*0.01 ;
    uint16_t speed = 0; //km/h (valid range 0 to 511)
    if (fields[GROUNDSPEED].available) speed = fields[GROUNDSPEED].value * 36 / 1000; 
    float gpsVario = 0 ;

    uint32_t lat, lon;
    // scale latitude/longitude (add 0.5 for correct rounding)
    if (latitude > 0) {
        lat = (600000.0*latitude) + 0.5;
    }
    else {
        lat = (-600000.0*latitude) + 0.5;
        // toggle south bit
        lat |= 0x4000000;
    }
    if (longitude > 0) {
        lon = (600000.0*longitude) + 0.5;
    }
    else {
        lon = (-600000.0*longitude) + 0.5;
        // toggle west bit
        lon |= 0x8000000;
    }
    // convert altitude (add 0.5 for correct rounding)
    uint16_t alt = (altitudeMeters>=-820 && altitudeMeters<=4830) ?(1.25*(altitudeMeters+820)) + 0.5  : 0;
    // error check speed
    if (speed < 512) {
        // set speed enable bit
        speed |= 0x200;
    }
    else {
        speed = 0;
    }
    // initialize buffer
    uint8_t bytes[3] = {0x03, 0x00, 0x00 };
    // slot 0 (utc)
    slotAvailable[0] = true;
    slotValueByte1[0] = (utc&0x00ff);
    slotValueByte2[0] = (utc&0xff00)>>8;
    // slot 1 (latitude & utc)
    slotAvailable[1] = true;
    slotValueByte1[1] = ((lat&0x007f)<<1) | ((utc&0x10000)>>16);
    slotValueByte2[1] =  (lat&0x7f80)>>7;
    // slot 2 (latitude & longitude)
    slotAvailable[2] = true;
    slotValueByte1[2] =  (lat&0x07f8000)>>15;
    slotValueByte2[2] = ((lat&0x7800000)>>23) | (lon&0x0f)<<4;
    // slot 3 (longitude)
    slotAvailable[3] = true;
    slotValueByte1[3] = (lon&0x00ff0)>>4;
    slotValueByte2[3] = (lon&0xff000)>>12;
    // slot 4 (longitude & speed)
    slotAvailable[4] = true;
    slotValueByte1[4] = ((lon&0xff00000)>>20);
    slotValueByte2[4] = (speed&0xff);
    // slot 5 (pressure & speed)
    slotAvailable[5] = true;
    slotValueByte1[5] = ((speed&0x300)>>8);
    slotValueByte2[5] = 0x00;
    // slot 6 (altitude & pressure)
    slotAvailable[6] = true;
    slotValueByte1[6] = ((alt&0x003)<<6);
    slotValueByte2[6] =  (alt&0x3fc)>>2;
    // slot (7 (vario & altitude)
    uint16_t vario;
    // error check vario
    if (gpsVario >= -150 && gpsVario <= 260) {
        // scale vario (add 0.5 for correct rounding)
        vario = (10.0*(gpsVario + 150)) + 0.5;
        // set vario enable
        vario |= 0x1000;
    }
    else {
        vario = 0;
    }
    slotAvailable[7] = true;
    slotValueByte1[7] = ((vario&0x001f)<<3) | ((alt&0x1c00)>>10);
    slotValueByte2[7] =  (vario&0x1fe0)>>5;
}

#ifdef SIMULATE_SBUS2_ON_PIN

#define RC_CHANNEL_MIN 990
#define RC_CHANNEL_MAX 2010

#define SBUS_MIN_OFFSET 173
#define SBUS_MID_OFFSET 992
#define SBUS_MAX_OFFSET 1811
#define SBUS_CHANNEL_NUMBER 16
#define SBUS_PACKET_LENGTH 25
#define SBUS_FRAME_HEADER 0x0f
#define SBUS_FRAME_FOOTER 0x00
#define SBUS_FRAME_FOOTER_V2 0x04
#define SBUS_STATE_FAILSAFE 0x08
#define SBUS_STATE_SIGNALLOSS 0x04
#define SBUS_UPDATE_RATE 15000 //us
#define SBUS_RESET_RATE 3500 //us

void setupBus2Simulation(){
    if ( config.pinSecIn == 255) {
        //gpio_init(SIMULATE_SBUS2_ON_PIN);
        uart_init(SIMU_SBUS2_UART_ID, SIMU_SBUS2_BAUDRATE);   // setup UART at 100000 baud
        uart_get_hw(uart0)->cr = 0;
        uart_set_format(uart0, 8, 2, UART_PARITY_EVEN);
        uart_get_hw(uart0)->cr = UART_UARTCR_UARTEN_BITS | UART_UARTCR_TXE_BITS ;
        uart_set_hw_flow(SIMU_SBUS2_UART_ID, false, false);// Set UART flow control CTS/RTS, we don't want these, so turn them off
        uart_set_fifo_enabled(SIMU_SBUS2_UART_ID, false);    // Turn on FIFO's for sending frames 
        gpio_set_function( SIMULATE_SBUS2_ON_PIN , GPIO_FUNC_UART);
        gpio_set_outover( SIMULATE_SBUS2_ON_PIN , GPIO_OVERRIDE_INVERT);        
    }    
}

void generateSbus2RcPacket(){
    static uint32_t nextSbusTime;
    static uint32_t nextResetTx; 
    static uint8_t packet[SBUS_PACKET_LENGTH]; 
    static uint16_t output[SBUS_CHANNEL_NUMBER] = {0XFF};
    static uint8_t frameCounter = 0X04; 
    if (microsRp() > nextSbusTime) {
        // before sending the frame, we activate the Tx uart
        //gpio_set_function( SIMULATE_SBUS2_ON_PIN , GPIO_FUNC_UART);
        //gpio_set_outover( SIMULATE_SBUS2_ON_PIN , GPIO_OVERRIDE_INVERT);

        packet[0] = SBUS_FRAME_HEADER; //Header
        packet[1] = (uint8_t) (output[0] & 0x07FF);
        packet[2] = (uint8_t) ((output[0] & 0x07FF)>>8 | (output[1] & 0x07FF)<<3);
        packet[3] = (uint8_t) ((output[1] & 0x07FF)>>5 | (output[2] & 0x07FF)<<6);
        packet[4] = (uint8_t) ((output[2] & 0x07FF)>>2);
        packet[5] = (uint8_t) ((output[2] & 0x07FF)>>10 | (output[3] & 0x07FF)<<1);
        packet[6] = (uint8_t) ((output[3] & 0x07FF)>>7 | (output[4] & 0x07FF)<<4);
        packet[7] = (uint8_t) ((output[4] & 0x07FF)>>4 | (output[5] & 0x07FF)<<7);
        packet[8] = (uint8_t) ((output[5] & 0x07FF)>>1);
        packet[9] = (uint8_t) ((output[5] & 0x07FF)>>9 | (output[6] & 0x07FF)<<2);
        packet[10] = (uint8_t) ((output[6] & 0x07FF)>>6 | (output[7] & 0x07FF)<<5);
        packet[11] = (uint8_t) ((output[7] & 0x07FF)>>3);
        packet[12] = (uint8_t) ((output[8] & 0x07FF));
        packet[13] = (uint8_t) ((output[8] & 0x07FF)>>8 | (output[9] & 0x07FF)<<3);
        packet[14] = (uint8_t) ((output[9] & 0x07FF)>>5 | (output[10] & 0x07FF)<<6);  
        packet[15] = (uint8_t) ((output[10] & 0x07FF)>>2);
        packet[16] = (uint8_t) ((output[10] & 0x07FF)>>10 | (output[11] & 0x07FF)<<1);
        packet[17] = (uint8_t) ((output[11] & 0x07FF)>>7 | (output[12] & 0x07FF)<<4);
        packet[18] = (uint8_t) ((output[12] & 0x07FF)>>4 | (output[13] & 0x07FF)<<7);
        packet[19] = (uint8_t) ((output[13] & 0x07FF)>>1);
        packet[20] = (uint8_t) ((output[13] & 0x07FF)>>9 | (output[14] & 0x07FF)<<2);
        packet[21] = (uint8_t) ((output[14] & 0x07FF)>>6 | (output[15] & 0x07FF)<<5);
        packet[22] = (uint8_t) ((output[15] & 0x07FF)>>3);

        packet[23] = 0; //Flags byte
        packet[24] = frameCounter; //Footer = 04,14,24,34
        frameCounter += 0x10;
        if (frameCounter > 0X34) frameCounter = 0X04;
        nextSbusTime = microsRp() + SBUS_UPDATE_RATE;
        nextResetTx = microsRp() + SBUS_RESET_RATE;
        uart_write_blocking	( SIMU_SBUS2_UART_ID , &packet[0] , SBUS_PACKET_LENGTH ) ; 
        //printf("send simu frame\n") ;   
    }  
    /*
    else if ( microsRp() > nextResetTx) {  // when the RC frame is sent, the Tx pin is disabled
        gpio_init(SIMULATE_SBUS2_ON_PIN);
        gpio_set_dir(SIMULATE_SBUS2_ON_PIN, GPIO_IN);
        gpio_pull_down(SIMULATE_SBUS2_ON_PIN);
        //gpio_set_inover( SIMULATE_SBUS2_ON_PIN , GPIO_OVERRIDE_INVERT);
        nextResetTx += SBUS_UPDATE_RATE ; 
    }
    */   
}



#endif