#ifndef DAQ_CAN
#define DAQ_CAN

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cmsis_os2.h"
#include "enums.h"
#include "main.h"

extern FDCAN_HandleTypeDef hfdcan1;

// the id the sample frames go out on, and the only one the filter accepts
#define DAQ_CAN_SAMPLE_ID 0x100U

typedef struct
{
    uint32_t id;
    uint8_t data[64];
    uint8_t length;
} DaqCanFrame;

// the task to signal when a frame arrives
void daq_can_set_rx_consumer(osThreadId_t task, uint32_t event);

// the task to signal when the transmit queue drains
void daq_can_set_tx_consumer(osThreadId_t task, uint32_t event);

// call once after the consumer is set
DaqStatus daq_can_start(void);

// copies into the controller's queue -> data now free
DaqStatus daq_can_send(uint32_t id, const uint8_t *data, uint8_t length);

// oldest queued frame, DAQ_ERR_EMPTY when nothing is waiting
DaqStatus daq_can_receive(DaqCanFrame *frame);

uint32_t daq_can_dropped_count(void);

// true once the controller has taken itself off the bus
bool daq_can_bus_off(void);

#endif
