#ifndef TELEMETRY_PACKET_H
#define TELEMETRY_PACKET_H

#include <stddef.h>
#include <stdint.h>

#define TELEMETRY_PACKET_MAX_SIZE  128U

/* 编码 AQ2 当前事件帧，尾部附 *CRC16（CCITT-FALSE）。
 * 基础字段含序号、运行时间、传感器值及固定规则状态；启用 APP_LIVE_AI_ENABLE 后追加模型窗结束时间、状态、得分和变化量。
 * 变化量为本窗 VOC 减前六窗均值；只有模型状态 2～5 携带有效结果。
 * 成功返回长度（不含末尾 '\0'），失败返回 0。 */
size_t TelemetryPacket_Format(char *buffer,
                              size_t buffer_size,
                              uint32_t sequence,
                              uint32_t uptime_ms);

#endif /* TELEMETRY_PACKET_H */
