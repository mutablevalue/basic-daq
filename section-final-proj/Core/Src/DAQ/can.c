#include "can.h"

// one slot stays empty to tell full from empty
#define DAQ_CAN_RX_QUEUE_LENGTH 9U

// every bit of a standard id has to match the filter
#define DAQ_CAN_STD_ID_MASK 0x7FFU

#define DAQ_CAN_DLC_CODES 16U
#define DAQ_CAN_MAX_LENGTH 64U

// the standard's table, codes past 8 step in fours and then double
static const uint8_t CAN_DLC_BYTES[DAQ_CAN_DLC_CODES] = {
    0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 12U, 16U, 20U, 24U, 32U, 48U, 64U};

static DaqCanFrame rxQueue[DAQ_CAN_RX_QUEUE_LENGTH];
static volatile uint32_t rxHead = 0U;
static volatile uint32_t rxTail = 0U;
static volatile uint32_t rxDropped = 0U;

// somewhere to dump a frame that arrives with the queue already full
static uint8_t rxDiscard[DAQ_CAN_MAX_LENGTH];

static volatile bool busOff = false;

static osThreadId_t rxConsumerTask = NULL;
static uint32_t rxConsumerEvent = 0U;

static osThreadId_t txConsumerTask = NULL;
static uint32_t txConsumerEvent = 0U;

// smallest legal frame size that still fits, the remainder is padding
static uint32_t daq_can_length_to_dlc(uint8_t length)
{
    uint32_t code = 0U;

    while ((code < (DAQ_CAN_DLC_CODES - 1U)) && (CAN_DLC_BYTES[code] < length))
    {
        code++;
    }

    return code;
}

// the task to signal when a frame arrives
void daq_can_set_rx_consumer(osThreadId_t task, uint32_t event)
{
    rxConsumerTask = task;
    rxConsumerEvent = event;
}

// the task to signal when the transmit queue drains
void daq_can_set_tx_consumer(osThreadId_t task, uint32_t event)
{
    txConsumerTask = task;
    txConsumerEvent = event;
}

// call once after the consumer is set
DaqStatus daq_can_start(void)
{
    FDCAN_FilterTypeDef filter;
    // One slot reserved
    filter.IdType = FDCAN_STANDARD_ID;
    filter.FilterIndex = 0U;
    filter.FilterType = FDCAN_FILTER_MASK;
    filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    filter.FilterID1 = DAQ_CAN_SAMPLE_ID;
    filter.FilterID2 = DAQ_CAN_STD_ID_MASK;

    if (HAL_FDCAN_ConfigFilter(&hfdcan1, &filter) != HAL_OK)
    {
        return DAQ_ERR_ACQUIRE;
    }

    // every id on the bus and make the filter above pointless
    if (HAL_FDCAN_ConfigGlobalFilter(&hfdcan1, FDCAN_REJECT, FDCAN_REJECT,
                                     FDCAN_REJECT_REMOTE,
                                     FDCAN_REJECT_REMOTE) != HAL_OK)
    {
        return DAQ_ERR_ACQUIRE;
    }

    if (HAL_FDCAN_Start(&hfdcan1) != HAL_OK)
    {
        return DAQ_ERR_ACQUIRE;
    }

    rxHead = 0U;
    rxTail = 0U;

    // the queue has to be settled before a callback can observe it
    __DMB();

    // the queue is empty already, drop the transition left over from boot
    __HAL_FDCAN_CLEAR_FLAG(&hfdcan1, FDCAN_FLAG_TX_FIFO_EMPTY);

    // last, a frame can land in the queue as soon as this returns
    if (HAL_FDCAN_ActivateNotification(&hfdcan1,
                                       FDCAN_IT_RX_FIFO0_NEW_MESSAGE |
                                           FDCAN_IT_TX_FIFO_EMPTY |
                                           FDCAN_IT_BUS_OFF,
                                       0U) != HAL_OK)
    {
        return DAQ_ERR_ACQUIRE;
    }

    return DAQ_OK;
}

// copies into the controller's queue -> data now free
DaqStatus daq_can_send(uint32_t id, const uint8_t *data, uint8_t length)
{
    if (data == NULL)
    {
        return DAQ_ERR_NULL;
    }

    if ((uint32_t)length > DAQ_CAN_MAX_LENGTH)
    {
        return DAQ_ERR_BAD_CAPACITY;
    }

    uint32_t dlc = daq_can_length_to_dlc(length);

    // the hal copies a whole dlc worth, so the padding has to be ours
    uint8_t payload[DAQ_CAN_MAX_LENGTH] = {0};
    for (uint32_t i = 0U; i < (uint32_t)length; i++)
    {
        payload[i] = data[i];
    }

    FDCAN_TxHeaderTypeDef header;
    header.Identifier = id;
    header.IdType = FDCAN_STANDARD_ID;
    header.TxFrameType = FDCAN_DATA_FRAME;
    header.DataLength = dlc;
    header.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    header.BitRateSwitch = FDCAN_BRS_ON;
    header.FDFormat = FDCAN_FD_CAN;
    header.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    header.MessageMarker = 0U;

    if (HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &header, payload) != HAL_OK)
    {
        // three tx slots, a full queue means the bus has not caught up yet
        return DAQ_ERR_TX_BUSY;
    }

    return DAQ_OK;
}

// oldest queued frame, DAQ_ERR_EMPTY when nothing is waiting
DaqStatus daq_can_receive(DaqCanFrame *frame)
{
    if (frame == NULL)
    {
        return DAQ_ERR_NULL;
    }

    uint32_t tail = rxTail;

    if (tail == rxHead)
    {
        return DAQ_ERR_EMPTY;
    }

    // don't read the slot before the head that says it was written
    __DMB();

    *frame = rxQueue[tail];

    uint32_t nextTail = tail + 1U;
    if (nextTail >= DAQ_CAN_RX_QUEUE_LENGTH)
    {
        nextTail = 0U;
    }

    // finish the copy before the tail frees the slot
    __DMB();

    rxTail = nextTail;

    return DAQ_OK;
}

uint32_t daq_can_dropped_count(void)
{
    return rxDropped;
}

bool daq_can_bus_off(void)
{
    return busOff;
}

void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs)
{
    if ((hfdcan->Instance != FDCAN1) ||
        ((RxFifo0ITs & FDCAN_IT_RX_FIFO0_NEW_MESSAGE) == 0U))
    {
        return;
    }

    FDCAN_RxHeaderTypeDef header;
    bool queued = false;

    // one interrupt can stand for several frames
    while (HAL_FDCAN_GetRxFifoFillLevel(hfdcan, FDCAN_RX_FIFO0) > 0U)
    {
        uint32_t head = rxHead;

        uint32_t nextHead = head + 1U;
        if (nextHead >= DAQ_CAN_RX_QUEUE_LENGTH)
        {
            nextHead = 0U;
        }

        if (nextHead == rxTail)
        {
            // drained regardless, a frame left behind re-raises the interrupt
            (void)HAL_FDCAN_GetRxMessage(hfdcan, FDCAN_RX_FIFO0, &header,
                                         rxDiscard);
            rxDropped++;
            continue;
        }

        // the hal writes into the slot itself, so the frame is copied once
        if (HAL_FDCAN_GetRxMessage(hfdcan, FDCAN_RX_FIFO0, &header,
                                   rxQueue[head].data) != HAL_OK)
        {
            break;
        }

        rxQueue[head].id = header.Identifier;

        // 4 bit DLC
        rxQueue[head].length = CAN_DLC_BYTES[header.DataLength & 0x0FU];

        // fill the slot before the head that publishes it
        __DMB();

        rxHead = nextHead;
        queued = true;
    }

    if (queued && (rxConsumerTask != NULL))
    {
        (void)osThreadFlagsSet(rxConsumerTask, rxConsumerEvent);
    }
}

// the queue drained, so a send rejected for a full queue is worth retrying
void HAL_FDCAN_TxFifoEmptyCallback(FDCAN_HandleTypeDef *hfdcan)
{
    if ((hfdcan->Instance == FDCAN1) && (txConsumerTask != NULL))
    {
        (void)osThreadFlagsSet(txConsumerTask, txConsumerEvent);
    }
}

// bus off is the only state that stops the node, the rest it recovers from
void HAL_FDCAN_ErrorStatusCallback(FDCAN_HandleTypeDef *hfdcan,
                                   uint32_t ErrorStatusITs)
{
    if ((hfdcan->Instance == FDCAN1) &&
        ((ErrorStatusITs & FDCAN_IT_BUS_OFF) != 0U))
    {
        busOff = true;
    }
}
