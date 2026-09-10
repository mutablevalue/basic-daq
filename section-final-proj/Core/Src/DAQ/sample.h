#ifndef DAQ_SAMPLE

#define DAQ_SAMPLE
#include <stdint.h>

// packed so it goes on the wiire as 7 bytes with no padding
typedef struct __attribute__((packed))
{
    uint32_t timestamp;
    uint16_t data;
    uint8_t channel;
} DAQSample;

#endif
