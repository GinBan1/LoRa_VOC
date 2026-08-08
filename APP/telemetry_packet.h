#ifndef TELEMETRY_PACKET_H
#define TELEMETRY_PACKET_H

#include <stddef.h>
#include <stdint.h>

#define TELEMETRY_PACKET_MAX_SIZE  128U

/*
 * Formats one null-terminated AQ1 telemetry packet.
 *
 * Body:
 * AQ1,seq,uptime_ms,flags,temp_x10,humidity_x10,pressure_x10,sraw_voc,
 * voc_index,ai_state,confidence_x1000,normal_x1000,warning_x1000
 *
 * The body is followed by *%04X containing CRC16-CCITT-FALSE over the body.
 * Returns the packet length excluding the null terminator, or 0 on failure.
 */
size_t TelemetryPacket_Format(char *buffer,
                              size_t buffer_size,
                              uint32_t sequence,
                              uint32_t uptime_ms);

#endif /* TELEMETRY_PACKET_H */
