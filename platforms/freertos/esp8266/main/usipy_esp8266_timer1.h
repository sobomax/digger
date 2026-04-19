#ifndef _USIPY_ESP8266_TIMER1_H
#define _USIPY_ESP8266_TIMER1_H

#define ESP8266_REG(addr) *((volatile uint32_t *)(0x60000000+(addr)))
#define T1I  ESP8266_REG(0x60C) //Interrupt Status Register (1bit) write to clear
#define T1V  ESP8266_REG(0x604) //(RO) Current Value
#define T1C  ESP8266_REG(0x608) //Control Register
#define T1L  ESP8266_REG(0x600) //Load Value (Starting Value of Counter) 23bit (0-8388607)
#define TCIT  0 //Interrupt Type 0:edge, 1:level
#define TCPD  2 //Prescale Devider (2bit) 0:1(12.5ns/tick), 1:16(0.2us/tick), 2/3:256(3.2us/tick)
#define TCAR  6 //AutoReload (restart timer when condition is reached)
#define TCTE  7 //Timer Enable
#define T1VMAX 0x7FFFFF
#define timer1_read()           (T1V)
#define timer1_enabled()        ((T1C & (1 << TCTE)) != 0)

//timer dividers
enum TIM_DIV_ENUM {
  TIM_DIV1 = 0,   //80MHz (80 ticks/us - 104857.588 us max)
  TIM_DIV16 = 1,  //5MHz (5 ticks/us - 1677721.4 us max)
  TIM_DIV256 = 3 //312.5Khz (1 tick = 3.2us - 26843542.4 us max)
};
//timer reload values
#define TIM_SINGLE      0 //on interrupt routine you need to write a new value to start the timer again
#define TIM_LOOP        1 //on interrupt the counter will start with the same value again
#define TIM_EDGE        0

static inline void
timer1_enable(uint8_t divider, uint8_t int_type, uint8_t reload){
    T1C = (1 << TCTE) | ((divider & 3) << TCPD) | ((int_type & 1) << TCIT) | ((reload & 1) << TCAR);
    T1I = 0;
}

static inline void
timer1_write(uint32_t ticks){
    T1L = ((ticks)& T1VMAX);
#if 0
    if ((T1C & (1 << TCIT)) == 0) TEIE |= TEIE1;//edge int enable
#endif
}

static inline uint32_t
GetCycleCount() {
    uint32_t ccount;
    __asm__ __volatile__("esync; rsr %0,ccount":"=a"(ccount));
    return ccount;
}

#endif /* _USIPY_ESP8266_TIMER1_H */
