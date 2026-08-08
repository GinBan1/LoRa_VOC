#ifndef SX1268_RADIO_H
#define SX1268_RADIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SX1268_RX_MAX_PAYLOAD 255U

typedef struct
{
    uint8_t data[SX1268_RX_MAX_PAYLOAD];
    uint8_t length;
    int16_t rssi_dbm;
    float snr_db;
    uint16_t irq_status;
} sx1268_rx_packet_t;

esp_err_t sx1268_radio_init(uint8_t *status);
esp_err_t sx1268_radio_start_receive(void);
esp_err_t sx1268_radio_poll(sx1268_rx_packet_t *packet, bool *packet_ready);
const char *sx1268_radio_error_name(esp_err_t error);

#ifdef __cplusplus
}
#endif

#endif /* SX1268_RADIO_H */
