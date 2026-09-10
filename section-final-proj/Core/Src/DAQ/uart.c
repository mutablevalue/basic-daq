#include "uart.h"

// the DMA owns the caller's buffer while this is set
static volatile bool txBusy = false;

// signalled from the transmit complete ISR
static osThreadId_t consumerTask = NULL;
static uint32_t consumerEvent = 0U;

static void daq_uart_release(void)
{
    txBusy = false;

    if (consumerTask != NULL)
    {
        (void)osThreadFlagsSet(consumerTask, consumerEvent);
    }
}

void daq_uart_set_consumer(osThreadId_t task, uint32_t event)
{
    consumerTask = task;
    consumerEvent = event;
}

bool daq_uart_busy(void)
{
    return txBusy;
}

DaqStatus daq_uart_send(const uint8_t *data, uint16_t length)
{
    if (data == NULL)
    {
        return DAQ_ERR_NULL;
    }

    if (txBusy)
    {
        return DAQ_ERR_TX_BUSY;
    }

    // claimed before arming, a short frame can complete before the next line
    txBusy = true;

    // the data has to be in memory before the DMA is told to read it
    __DMB();

    if (HAL_UART_Transmit_DMA(&hlpuart1, data, length) != HAL_OK)
    {
        txBusy = false;
        return DAQ_ERR_TRANSMIT;
    }

    return DAQ_OK;
}

// fires once the shift register empties, not when the DMA finishes
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == LPUART1)
    {
        daq_uart_release();
    }
}

// without this a single error leaves the buffer claimed and the stream stops
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == LPUART1)
    {
        daq_uart_release();
    }
}
