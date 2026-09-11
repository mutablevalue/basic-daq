#ifndef DAQ_UART
#define DAQ_UART

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cmsis_os2.h"
#include "enums.h"
#include "main.h"

extern UART_HandleTypeDef hlpuart1;

// the task to signal when a transfer completes
void daq_uart_set_consumer(osThreadId_t task, uint32_t event);

bool daq_uart_busy(void);

// returns as soon as the DMA is armed, data must stay untouched until
// daq_uart_busy goes false
DaqStatus daq_uart_send(const uint8_t *data, uint16_t length);

// the task to signal when bytes arrive
void daq_uart_set_rx_consumer(osThreadId_t task, uint32_t event);

// arms the circular receive, call once and only after the consumer is set
DaqStatus daq_uart_receive_start(void);

// consumer side only, never call this from an ISR
DaqStatus daq_uart_receive(uint8_t *destination, uint16_t length,
                           uint16_t *received);

uint32_t daq_uart_receive_error_count(void);

#endif
