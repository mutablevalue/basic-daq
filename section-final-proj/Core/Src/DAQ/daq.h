#ifndef DAQ_H
#define DAQ_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cmsis_os2.h"
#include "enums.h"
#include "main.h"
#include "ringbuffer.h"
#include "sample.h"

extern osThreadId_t txTaskHandle;
extern ADC_HandleTypeDef hadc1;
extern TIM_HandleTypeDef htim3;

// one half is settled while the DMA fills the other
#define ADC_BLOCK_LEN 64U
#define ADC_BUF_LEN (ADC_BLOCK_LEN * 2U)

// signalled to the consumer when there is work to do
#define DAQ_TX_EVENT 0x01U
#define DAQ_RX_EVENT 0x02U

// samples coalesced into a single transmit
#define DAQ_TX_FRAME_SAMPLES 32U

typedef struct
{
    DaqRingBuffer ring;

    // samples dropped by a full ring, producer only
    volatile uint32_t overflowCount;

    volatile bool initialized;
} Daq;

// buffer is caller owned, capacity is its length and must be >= 2
DaqStatus daq_init(Daq *daq, DAQSample *buffer, uint32_t capacity);

// DAQ_ERR_OVERFLOW means the sample was dropped and the count bumped
DaqStatus daq_store_sample(Daq *daq, const DAQSample *sample);

// oldest sample, removed from the queue
DaqStatus daq_take_sample(Daq *daq, DAQSample *sample);

// sends sample with uart
uint32_t daq_count(const Daq *daq);

uint32_t daq_capacity(const Daq *daq);

uint32_t daq_overflow_count(const Daq *daq);

// consumer side only, never call this from the ISR
DaqStatus daq_flush_samples(Daq *daq);

// starts the ADC and its trigger, daq must already be initialised
DaqStatus daq_acquire_start(Daq *daq);

// sends one frame, consumer side only
// DAQ_ERR_EMPTY means the ring had nothing queued
DaqStatus daq_service_tx(Daq *daq);

#endif
