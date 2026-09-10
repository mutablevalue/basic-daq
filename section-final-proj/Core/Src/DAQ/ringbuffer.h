#ifndef DAQ_RING_BUFFER
#define DAQ_RING_BUFFER

#include <stdbool.h>
#include <stdint.h>

#include "enums.h"
#include "sample.h"

// one pusher (the ISR) and one popper (main loop) only
typedef struct
{
    DAQSample *buffer;

    volatile uint32_t head; // producer only
    volatile uint32_t tail; // consumer only

    uint32_t capacity; // length of buffer, must be >= 2
} DaqRingBuffer;

// reject sample on overflow
DaqRingStatus daq_ring_push(DaqRingBuffer *ring, const DAQSample *sample);

DaqRingStatus daq_ring_pop(DaqRingBuffer *ring, DAQSample *sample);

bool daq_ring_is_empty(const DaqRingBuffer *ring);

bool daq_ring_is_full(const DaqRingBuffer *ring);

uint32_t daq_ring_size(const DaqRingBuffer *ring);

// usable slots
uint32_t daq_ring_capacity(const DaqRingBuffer *ring);

// consumer side only, drops everything queued up to head
void daq_ring_reset(DaqRingBuffer *ring);

#endif
