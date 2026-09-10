#include "ringbuffer.h"

#include "stm32g4xx_hal.h"

DaqRingStatus daq_ring_push(DaqRingBuffer *ring, const DAQSample *sample)
{
    uint32_t nextHead = ring->head + 1U;
    if (nextHead >= ring->capacity)
    {
        nextHead = 0U;
    }
    // we discard the sample
    if (nextHead == ring->tail)
    {
        return DAQ_RING_FULL;
    }

    ring->buffer[ring->head] = *sample;

    // write the sample before the head that exposes it
    __DMB();

    ring->head = nextHead;

    return DAQ_RING_OK;
}

DaqRingStatus daq_ring_pop(DaqRingBuffer *ring, DAQSample *sample)
{
    if (ring->head == ring->tail)
    {
        return DAQ_RING_EMPTY;
    }

    // don't read the slot before the head check
    __DMB();

    *sample = ring->buffer[ring->tail];

    uint32_t nextTail = ring->tail + 1U;
    if (nextTail >= ring->capacity)
    {
        nextTail = 0U;
    }

    // finish the copy before tail frees the slot
    __DMB();

    ring->tail = nextTail;

    return DAQ_RING_OK;
}

bool daq_ring_is_empty(const DaqRingBuffer *ring)
{
    return ring->head == ring->tail;
}

bool daq_ring_is_full(const DaqRingBuffer *ring)
{
    uint32_t nextHead = ring->head + 1U;
    if (nextHead >= ring->capacity)
    {
        nextHead = 0U;
    }

    return nextHead == ring->tail;
}

uint32_t daq_ring_size(const DaqRingBuffer *ring)
{
    // snapshot, these move underneath us
    uint32_t head = ring->head;
    uint32_t tail = ring->tail;

    if (head >= tail)
    {
        return head - tail;
    }

    return ring->capacity - (tail - head);
}

uint32_t daq_ring_capacity(const DaqRingBuffer *ring)
{
    // one slot stays empty to tell full from empty
    return (ring->capacity > 0U) ? (ring->capacity - 1U) : 0U;
}

void daq_ring_reset(DaqRingBuffer *ring)
{
    // tail belongs to the consumer,
    ring->tail = ring->head;
}
