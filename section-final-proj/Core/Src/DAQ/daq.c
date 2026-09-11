#include "daq.h"
#include "timer.h"
#include "uart.h"

// DMA target -> not volatile
static uint16_t adcBuf[ADC_BUF_LEN];

// samples are popped straight into the frame,
static DAQSample daqTxBuf[DAQ_TX_FRAME_SAMPLES];

// set by daq_acquire_start, read by the DMA callbacks
static Daq *acquireTarget = NULL;

DaqStatus daq_init(Daq *daq, DAQSample *buffer, uint32_t capacity)
{
    if ((daq == NULL) || (buffer == NULL))
    {
        return DAQ_ERR_NULL;
    }

    // one slot is reserved by the ring, so 2 is the smallest useful array
    if (capacity < 2U)
    {
        return DAQ_ERR_BAD_CAPACITY;
    }

    daq->ring.buffer = buffer;
    daq->ring.capacity = capacity;
    daq->ring.head = 0U;
    daq->ring.tail = 0U;

    daq->overflowCount = 0U;

    // the ring has to be fully set up before the flag that opens it to the ISR
    __DMB();

    daq->initialized = true;

    return DAQ_OK;
}

DaqStatus daq_store_sample(Daq *daq, const DAQSample *sample)
{
    if ((daq == NULL) || (sample == NULL))
    {
        return DAQ_ERR_NULL;
    }

    if (!daq->initialized)
    {
        return DAQ_ERR_NOT_INITIALIZED;
    }

    if (daq_ring_push(&daq->ring, sample) == DAQ_RING_FULL)
    {
        daq->overflowCount++;
        return DAQ_ERR_OVERFLOW;
    }

    return DAQ_OK;
}

DaqStatus daq_take_sample(Daq *daq, DAQSample *sample)
{
    if ((daq == NULL) || (sample == NULL))
    {
        return DAQ_ERR_NULL;
    }

    if (!daq->initialized)
    {
        return DAQ_ERR_NOT_INITIALIZED;
    }

    if (daq_ring_pop(&daq->ring, sample) == DAQ_RING_EMPTY)
    {
        return DAQ_ERR_EMPTY;
    }

    return DAQ_OK;
}

uint32_t daq_count(const Daq *daq)
{
    if ((daq == NULL) || !daq->initialized)
    {
        return 0U;
    }

    return daq_ring_size(&daq->ring);
}

uint32_t daq_capacity(const Daq *daq)
{
    if ((daq == NULL) || !daq->initialized)
    {
        return 0U;
    }

    return daq_ring_capacity(&daq->ring);
}

uint32_t daq_overflow_count(const Daq *daq)
{
    if ((daq == NULL) || !daq->initialized)
    {
        return 0U;
    }

    return daq->overflowCount;
}

DaqStatus daq_flush_samples(Daq *daq)
{
    if (daq == NULL)
    {
        return DAQ_ERR_NULL;
    }

    if (!daq->initialized)
    {
        return DAQ_ERR_NOT_INITIALIZED;
    }

    daq_ring_reset(&daq->ring);

    return DAQ_OK;
}

static void daq_ingest_block(const uint16_t *block, uint32_t length)
{
    // wrap safe
    uint32_t blockEnd = daq_timer_now();
    uint32_t blockStart = blockEnd - (length * DAQ_SAMPLE_PERIOD_US);

    for (uint32_t i = 0U; i < length; i++)
    {
        DAQSample sample = {
            .timestamp = blockStart + (i * DAQ_SAMPLE_PERIOD_US),
            .data = block[i],
            .channel = 0U,
        };

        // overflow drops the sample and bumps the count
        (void)daq_store_sample(acquireTarget, &sample);
    }
}

DaqStatus daq_acquire_start(Daq *daq)
{
    if (daq == NULL)
    {
        return DAQ_ERR_NULL;
    }

    if (!daq->initialized)
    {
        return DAQ_ERR_NOT_INITIALIZED;
    }

    acquireTarget = daq;

    // the target has to be visible before an ISR can observe a running DMA
    __DMB();

    // must precede the first conversion
    if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED) != HAL_OK)
    {
        return DAQ_ERR_ACQUIRE;
    }

    if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adcBuf, ADC_BUF_LEN) != HAL_OK)
    {
        return DAQ_ERR_ACQUIRE;
    }

    if (HAL_TIM_Base_Start(&htim3) != HAL_OK)
    {
        return DAQ_ERR_ACQUIRE;
    }

    return DAQ_OK;
}

DaqStatus daq_service_tx(Daq *daq)
{
    if (daq == NULL)
    {
        return DAQ_ERR_NULL;
    }

    if (!daq->initialized)
    {
        return DAQ_ERR_NOT_INITIALIZED;
    }

    if (daq_uart_busy())
    {
        return DAQ_ERR_TX_BUSY;
    }

    uint32_t count = 0U;
    while (count < DAQ_TX_FRAME_SAMPLES)
    {
        if (daq_take_sample(daq, &daqTxBuf[count]) != DAQ_OK)
        {
            break;
        }

        count++;
    }

    if (count == 0U)
    {
        return DAQ_ERR_EMPTY;
    }

    // on failure these samples are already out of the ring and are lost
    return daq_uart_send((const uint8_t *)daqTxBuf,
                         (uint16_t)(count * sizeof(DAQSample)));
}

// first half settled, the DMA has moved on to the second
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
    if ((hadc->Instance == ADC1) && (acquireTarget != NULL))
    {
        daq_ingest_block(&adcBuf[0], ADC_BLOCK_LEN);
        (void)osThreadFlagsSet(txTaskHandle, DAQ_TX_EVENT);
    }
}

// second half settled, circular mode has wrapped back to the first
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if ((hadc->Instance == ADC1) && (acquireTarget != NULL))
    {
        daq_ingest_block(&adcBuf[ADC_BLOCK_LEN], ADC_BLOCK_LEN);
        (void)osThreadFlagsSet(txTaskHandle, DAQ_TX_EVENT);
    }
}
