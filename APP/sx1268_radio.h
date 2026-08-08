#ifndef SX1268_RADIO_H
#define SX1268_RADIO_H

#include <stdint.h>

typedef enum
{
    SX1268_OK = 0,
    SX1268_BUSY_TIMEOUT,
    SX1268_SPI_ERROR,
    SX1268_BAD_PARAM,
    SX1268_NOT_FOUND
} sx1268_result_t;

#define SX1268_IRQ_TX_DONE  0x0001U
#define SX1268_IRQ_TIMEOUT  0x0200U
#define SX1268_IRQ_ALL      0x03FFU

sx1268_result_t SX1268_Init(uint32_t frequency_hz, int8_t power_dbm,
                            uint8_t *status);
sx1268_result_t SX1268_GetStatus(uint8_t *status);
sx1268_result_t SX1268_StartTx(const uint8_t *payload, uint8_t length,
                               uint32_t timeout_ms);
sx1268_result_t SX1268_GetAndClearIrq(uint16_t *irq);
sx1268_result_t SX1268_Standby(void);
void SX1268_FrontEndIdle(void);
const char *SX1268_ResultString(sx1268_result_t result);

#endif /* SX1268_RADIO_H */
