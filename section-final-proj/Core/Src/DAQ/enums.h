#ifndef DAQ_ENUMS
#define DAQ_ENUMS

typedef enum
{
    DAQ_RING_OK = 0,
    DAQ_RING_FULL,
    DAQ_RING_EMPTY
} DaqRingStatus;

typedef enum
{
    DAQ_OK = 0,
    DAQ_ERR_NULL,
    DAQ_ERR_NOT_INITIALIZED,
    DAQ_ERR_BAD_CAPACITY,
    DAQ_ERR_OVERFLOW,
    DAQ_ERR_EMPTY,
    DAQ_ERR_TRANSMIT,
    DAQ_ERR_ACQUIRE,
    DAQ_ERR_TX_BUSY,
    DAQ_ERR_RECEIVE
} DaqStatus;

#endif
