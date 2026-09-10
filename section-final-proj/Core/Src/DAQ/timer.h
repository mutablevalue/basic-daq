#ifndef DAQ_TIMER
#define DAQ_TIMER

#include <stdint.h>

#include "main.h"

#define DAQ_TIME_TICK_HZ 1000000UL

#define DAQ_SAMPLE_PERIOD_US 1000UL

static inline uint32_t daq_timer_now(void)
{
    return TIM2->CNT;
}

#endif
