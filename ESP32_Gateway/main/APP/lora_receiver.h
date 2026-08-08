#ifndef LORA_RECEIVER_H
#define LORA_RECEIVER_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "telemetry_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LORA_RECEIVER_FRESH_MS 15000U

typedef struct
{
    bool radio_ready;
    bool telemetry_valid;
    bool telemetry_fresh;
    uint8_t radio_status;
    uint32_t packet_count;
    uint32_t valid_packet_count;
    uint32_t error_count;
    uint32_t parse_error_count;
    uint32_t crc_error_count;
    uint32_t last_rx_ms;
    int16_t rssi_dbm;
    float snr_db;
    uint8_t payload_length;
    telemetry_packet_t telemetry;
} lora_receiver_snapshot_t;

esp_err_t lora_receiver_init(void);
void lora_receiver_get_snapshot(lora_receiver_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif

#endif /* LORA_RECEIVER_H */
