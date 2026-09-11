#include "uart.h"

// bytes the DMA can stage between drains
#define DAQ_UART_RX_BUFFER_LENGTH 256U

#define DAQ_UART_LINE_ERRORS                                                   \
    (HAL_UART_ERROR_ORE | HAL_UART_ERROR_FE | HAL_UART_ERROR_NE |              \
     HAL_UART_ERROR_PE)

// the DMA owns the caller's buffer while this is set
static volatile bool txBusy = false;

// signalled from the transmit complete ISR
static osThreadId_t consumerTask = NULL;
static uint32_t consumerEvent = 0U;

// the DMA writes this continuously, the consumer only ever reads it
static uint8_t rxBuffer[DAQ_UART_RX_BUFFER_LENGTH];

// consumer only, the DMA never reads it back
static uint32_t rxTail = 0U;

static volatile uint32_t rxErrorCount = 0U;

// signalled from the receive event ISR
static osThreadId_t rxConsumerTask = NULL;
static uint32_t rxConsumerEvent = 0U;

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

void daq_uart_set_rx_consumer(osThreadId_t task, uint32_t event)
{
    rxConsumerTask = task;
    rxConsumerEvent = event;
}

uint32_t daq_uart_receive_error_count(void)
{
    return rxErrorCount;
}

// the channel counts down the transfers it has left
static uint32_t daq_uart_rx_head(void)
{
    uint32_t remaining = __HAL_DMA_GET_COUNTER(hlpuart1.hdmarx);

    // full count is position 0
    if ((remaining == 0U) || (remaining > DAQ_UART_RX_BUFFER_LENGTH))
    {
        return 0U;
    }

    return DAQ_UART_RX_BUFFER_LENGTH - remaining;
}

DaqStatus daq_uart_receive_start(void)
{
    rxTail = 0U;

    // the tail has to be settled before a callback can observe a live channel
    __DMB();

    if (HAL_UARTEx_ReceiveToIdle_DMA(&hlpuart1, rxBuffer,
                                     DAQ_UART_RX_BUFFER_LENGTH) != HAL_OK)
    {
        return DAQ_ERR_RECEIVE;
    }

    return DAQ_OK;
}

DaqStatus daq_uart_receive(uint8_t *destination, uint16_t length,
                           uint16_t *received)
{
    if ((destination == NULL) || (received == NULL))
    {
        return DAQ_ERR_NULL;
    }

    *received = 0U;

    if (length == 0U)
    {
        return DAQ_ERR_EMPTY;
    }

    uint32_t head = daq_uart_rx_head();
    uint32_t tail = rxTail;

    if (head == tail)
    {
        return DAQ_ERR_EMPTY;
    }

    // don't read the slots before the head that says they were written
    __DMB();

    uint32_t available;
    if (head > tail)
    {
        available = head - tail;
    }
    else
    {
        available = (DAQ_UART_RX_BUFFER_LENGTH - tail) + head;
    }

    if (available > (uint32_t)length)
    {
        available = (uint32_t)length;
    }

    uint32_t index = tail;
    for (uint32_t i = 0U; i < available; i++)
    {
        destination[i] = rxBuffer[index];

        index++;
        if (index >= DAQ_UART_RX_BUFFER_LENGTH)
        {
            index = 0U;
        }
    }

    rxTail = index;

    *received = (uint16_t)available;

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

// half transfer, full transfer and an idle line all land here
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    // the fill position comes from the DMA counter instead
    (void)Size;

    if ((huart->Instance == LPUART1) && (rxConsumerTask != NULL))
    {
        (void)osThreadFlagsSet(rxConsumerTask, rxConsumerEvent);
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != LPUART1)
    {
        return;
    }

    uint32_t error = huart->ErrorCode;

    if ((error & DAQ_UART_LINE_ERRORS) != 0U)
    {
        rxErrorCount++;

        (void)daq_uart_receive_start();
    }

    if (((error & HAL_UART_ERROR_DMA) != 0U) &&
        (hlpuart1.hdmatx->ErrorCode != HAL_DMA_ERROR_NONE))
    {
        daq_uart_release();
    }
}
