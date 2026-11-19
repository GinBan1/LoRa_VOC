#include "MQTT.h"

#include <string.h>
#include <stdio.h>


#define CMD_TOPIC_PREFIX        "$creq"

/* 准备报文缓冲；未提供缓冲时动态分配。 */
void MQTT_NewBuffer(MQTT_PACKET_STRUCTURE *mqttPacket, uint32 size)
{

    uint32 i = 0;

    if(mqttPacket->_data == NULL)
    {
        mqttPacket->_memFlag = MEM_FLAG_ALLOC;

        mqttPacket->_data = (uint8 *)MQTT_MallocBuffer(size);
        if(mqttPacket->_data != NULL)
        {
            mqttPacket->_len = 0;

            mqttPacket->_size = size;

            for(; i < mqttPacket->_size; i++)
                mqttPacket->_data[i] = 0;
        }
    }
    else
    {
        mqttPacket->_memFlag = MEM_FLAG_STATIC;

        for(; i < mqttPacket->_size; i++)
            mqttPacket->_data[i] = 0;

        mqttPacket->_len = 0;

        if(mqttPacket->_size < size)
            mqttPacket->_data = NULL;
    }

}

/* 释放动态分配的报文缓冲，并清空结构。 */
void MQTT_DeleteBuffer(MQTT_PACKET_STRUCTURE *mqttPacket)
{

    if(mqttPacket->_memFlag == MEM_FLAG_ALLOC)
        MQTT_FreeBuffer(mqttPacket->_data);

    mqttPacket->_data = NULL;
    mqttPacket->_len = 0;
    mqttPacket->_size = 0;
    mqttPacket->_memFlag = MEM_FLAG_NULL;

}

int32 MQTT_DumpLength(size_t len, uint8 *buf)
{

    int32 i = 0;

    for(i = 1; i <= 4; ++i)
    {
        *buf = len % 128;
        len >>= 7;
        if(len > 0)
        {
            *buf |= 128;
            ++buf;
        }
        else
        {
            return i;
        }
    }

    return -1;
}

int32 MQTT_ReadLength(const uint8 *stream, int32 size, uint32 *len)
{

    int32 i;
    const uint8 *in = stream;
    uint32 multiplier = 1;

    *len = 0;
    for(i = 0; i < size; ++i)
    {
        *len += (in[i] & 0x7f) * multiplier;

        if(!(in[i] & 0x80))
        {
            return i + 1;
        }

        multiplier <<= 7;
        if(multiplier >= 2097152)        //128 * *128 * *128
        {
            return -2;                    // error, out of range
        }
    }

    return -1;                            // not complete

}

/* 兼容入口，只能接收完整可读的报文。 */
uint8 MQTT_UnPacketRecv(uint8 *dataPtr)
{
    size_t length = 0U;
    if (MQTT_FrameLength(dataPtr, 5U, &length) < 0 || length == 0U)
        return 255U;
    return MQTT_UnPacketRecvLength(dataPtr, length);
}

typedef struct
{
    size_t header_length;
    size_t remaining_length;
    size_t topic_length;
    size_t payload_offset;
    uint16 packet_id;
    uint8 qos;
} mqtt_frame_view_t;

int32 MQTT_FrameLength(const uint8 *data, size_t available, size_t *frame_length)
{
    uint32 remaining = 0U;
    uint32 multiplier = 1U;
    uint8 type, flags;
    size_t i;

    if (frame_length == NULL) return -1;
    *frame_length = 0U;
    if (data == NULL) return available == 0U ? 0 : -1;
    if (available == 0U) return 0;
    type = data[0] >> 4;
    flags = data[0] & 0x0FU;
    if (type < MQTT_PKT_CONNECT || type > MQTT_PKT_DISCONNECT) return -1;
    if (type == MQTT_PKT_PUBLISH)
    {
        uint8 qos = (flags >> 1) & 3U;
        if (qos == 3U || (qos == 0U && (flags & 8U))) return -1;
    }
    else if (flags != ((type == MQTT_PKT_PUBREL || type == MQTT_PKT_SUBSCRIBE ||
                        type == MQTT_PKT_UNSUBSCRIBE) ? 2U : 0U)) return -1;

    for (i = 1U; i <= 4U; ++i)
    {
        uint8 encoded;
        if (i >= available) return 0;
        encoded = data[i];
        remaining += (uint32)(encoded & 0x7FU) * multiplier;
        if ((encoded & 0x80U) == 0U)
        {
            /* 拒绝带多余续字节的长度编码。 */
            if (i > 1U && remaining < multiplier) return -1;
            *frame_length = i + 1U + (size_t)remaining;
            return available >= *frame_length ? 1 : 0;
        }
        multiplier *= 128U;
    }
    return -1;
}

static bool MQTT_View(const uint8 *data, size_t length, mqtt_frame_view_t *view)
{
    size_t total = 0U;
    if (view == NULL || MQTT_FrameLength(data, length, &total) != 1 || total != length)
        return false;
    memset(view, 0, sizeof(*view));
    view->header_length = 2U;
    while ((data[view->header_length - 1U] & 0x80U) != 0U)
        ++view->header_length;
    view->remaining_length = length - view->header_length;
    return true;
}

static bool MQTT_TopicValid(const uint8 *topic, size_t length)
{
    size_t i = 0U;
    if (length == 0U) return false;
    while (i < length)
    {
        uint32 code;
        unsigned trailing;
        uint8 first = topic[i++];
        if (first == 0U || first == '+' || first == '#') return false;
        if (first < 0x80U) continue;
        if (first >= 0xC2U && first <= 0xDFU) { code = first & 0x1FU; trailing = 1U; }
        else if (first >= 0xE0U && first <= 0xEFU) { code = first & 0x0FU; trailing = 2U; }
        else if (first >= 0xF0U && first <= 0xF4U) { code = first & 7U; trailing = 3U; }
        else return false;
        if (length - i < trailing) return false;
        for (unsigned n = 0U; n < trailing; ++n)
        {
            uint8 next = topic[i++];
            if ((next & 0xC0U) != 0x80U) return false;
            code = (code << 6) | (next & 0x3FU);
        }
        if ((trailing == 1U && code < 0x80U) || (trailing == 2U && code < 0x800U) ||
            (trailing == 3U && code < 0x10000U) || code > 0x10FFFFU ||
            (code >= 0xD800U && code <= 0xDFFFU)) return false;
    }
    return true;
}

static bool MQTT_PublishView(const uint8 *data, size_t length, mqtt_frame_view_t *view)
{
    size_t offset;
    if (!MQTT_View(data, length, view) || data[0] >> 4 != MQTT_PKT_PUBLISH ||
        view->remaining_length < 2U) return false;
    offset = view->header_length;
    view->topic_length = ((size_t)data[offset] << 8) | data[offset + 1U];
    offset += 2U;
    if (view->topic_length > length - offset ||
        !MQTT_TopicValid(data + offset, view->topic_length)) return false;
    offset += view->topic_length;
    view->qos = (data[0] >> 1) & 3U;
    if (view->qos != 0U)
    {
        if (length - offset < 2U) return false;
        view->packet_id = (uint16)(((uint16)data[offset] << 8) | data[offset + 1U]);
        if (view->packet_id == 0U) return false;
        offset += 2U;
    }
    view->payload_offset = offset;
    return length - offset <= UINT16_MAX;
}

static bool MQTT_CommandTopic(const uint8 *data, const mqtt_frame_view_t *view)
{
    const size_t prefix_length = sizeof(CMD_TOPIC_PREFIX) - 1U;
    const uint8 *topic = data + view->header_length + 2U;
    return view->topic_length >= prefix_length &&
           memcmp(topic, CMD_TOPIC_PREFIX, prefix_length) == 0 &&
           (view->topic_length == prefix_length || topic[prefix_length] == '/');
}

uint8 MQTT_UnPacketRecvLength(const uint8 *data, size_t length)
{
    mqtt_frame_view_t view;
    uint8 type;
    const uint8 *body;
    if (!MQTT_View(data, length, &view)) return 255U;
    type = data[0] >> 4;
    body = data + view.header_length;
    switch (type)
    {
        case MQTT_PKT_PUBLISH:
            if (!MQTT_PublishView(data, length, &view)) return 255U;
            return MQTT_CommandTopic(data, &view) ? MQTT_PKT_CMD : MQTT_PKT_PUBLISH;
        case MQTT_PKT_CONNACK:
            if (view.remaining_length != 2U || body[0] > 1U || body[1] > 5U ||
                (body[1] != 0U && body[0] != 0U)) return 255U;
            break;
        case MQTT_PKT_PUBACK:
        case MQTT_PKT_PUBREC:
        case MQTT_PKT_PUBREL:
        case MQTT_PKT_PUBCOMP:
        case MQTT_PKT_UNSUBACK:
            if (view.remaining_length != 2U || (body[0] == 0U && body[1] == 0U)) return 255U;
            break;
        case MQTT_PKT_SUBACK:
            if (view.remaining_length < 3U || (body[0] == 0U && body[1] == 0U)) return 255U;
            for (size_t i = 2U; i < view.remaining_length; ++i)
                if (body[i] > 2U && body[i] != 0x80U) return 255U;
            break;
        case MQTT_PKT_PINGREQ:
        case MQTT_PKT_PINGRESP:
        case MQTT_PKT_DISCONNECT:
            if (view.remaining_length != 0U) return 255U;
            break;
        default:
            /* 接收端只解析服务器返回的报文。 */
            return 255U;
    }
    return type;
}

uint8 MQTT_UnPacketConnectAckLength(const uint8 *data, size_t length)
{
    return MQTT_UnPacketRecvLength(data, length) == MQTT_PKT_CONNACK ? data[length - 1U] : 255U;
}

uint8 MQTT_UnPacketSubscribeLength(const uint8 *data, size_t length)
{
    mqtt_frame_view_t view;
    const uint8 *body;
    if (MQTT_UnPacketRecvLength(data, length) != MQTT_PKT_SUBACK || !MQTT_View(data, length, &view))
        return 255U;
    body = data + view.header_length;
    if ((((uint16)body[0] << 8) | body[1]) != MQTT_SUBSCRIBE_ID) return 255U;
    for (size_t i = 2U; i < view.remaining_length; ++i)
        if (body[i] == 0x80U) return 1U;
    return 0U;
}

uint1 MQTT_UnPacketPublishAckLength(const uint8 *data, size_t length)
{
    return MQTT_UnPacketRecvLength(data, length) != MQTT_PKT_PUBACK ||
           (((uint16)data[2] << 8) | data[3]) != MQTT_PUBLISH_ID;
}

/* 组装 CONNECT 报文。 */
uint8 MQTT_PacketConnect(const int8 *user, const int8 *password, const int8 *devid,
                        uint16 cTime, uint1 clean_session, uint1 qos,
                        const int8 *will_topic, const int8 *will_msg, int32 will_retain,
                        MQTT_PACKET_STRUCTURE *mqttPacket)
{

    uint8 flags = 0;
    uint8 will_topic_len = 0;
    uint16 total_len = 15;
    int16 len = 0, devid_len = strlen(devid);

    if(!devid)
        return 1;

    total_len += devid_len + 2;

    /* 置位时清除旧会话。 */
    if(clean_session)
    {
        flags |= MQTT_CONNECT_CLEAN_SESSION;
    }

    /* 异常断线后由服务端发布遗嘱。 */
    if(will_topic)
    {
        flags |= MQTT_CONNECT_WILL_FLAG;
        will_topic_len = strlen(will_topic);
        total_len += 4 + will_topic_len + strlen(will_msg);
    }

    /* 写入遗嘱消息的 QoS。 */
    switch((unsigned char)qos)
    {
        case MQTT_QOS_LEVEL0:
            flags |= MQTT_CONNECT_WILL_QOS0;                            //最多一次
        break;

        case MQTT_QOS_LEVEL1:
            flags |= (MQTT_CONNECT_WILL_FLAG | MQTT_CONNECT_WILL_QOS1);    //最少一次
        break;

        case MQTT_QOS_LEVEL2:
            flags |= (MQTT_CONNECT_WILL_FLAG | MQTT_CONNECT_WILL_QOS2);    //只有一次
        break;

        default:
        return 2;
    }

    /* 设置遗嘱的保留标志。 */
    if(will_retain)
    {
        flags |= (MQTT_CONNECT_WILL_FLAG | MQTT_CONNECT_WILL_RETAIN);
    }

    /* 账号为空 密码为空 */
    if(!user || !password)
    {
        return 3;
    }
    flags |= MQTT_CONNECT_USER_NAME | MQTT_CONNECT_PASSORD;

    total_len += strlen(user) + strlen(password) + 4;

    /* 分配内存 */
    MQTT_NewBuffer(mqttPacket, total_len);
    if(mqttPacket->_data == NULL)
        return 4;

    memset(mqttPacket->_data, 0, total_len);


    /* 固定头部：连接请求类型 */
    mqttPacket->_data[mqttPacket->_len++] = MQTT_PKT_CONNECT << 4;

    /* 固定头部：剩余长度值 */
    len = MQTT_DumpLength(total_len - 5, mqttPacket->_data + mqttPacket->_len);
    if(len < 0)
    {
        MQTT_DeleteBuffer(mqttPacket);
        return 5;
    }
    else
        mqttPacket->_len += len;


    /* 可变头部：协议名长度 和 协议名 */
    mqttPacket->_data[mqttPacket->_len++] = 0;
    mqttPacket->_data[mqttPacket->_len++] = 4;
    mqttPacket->_data[mqttPacket->_len++] = 'M';
    mqttPacket->_data[mqttPacket->_len++] = 'Q';
    mqttPacket->_data[mqttPacket->_len++] = 'T';
    mqttPacket->_data[mqttPacket->_len++] = 'T';

    /* 可变头部：协议级别 4 */
    mqttPacket->_data[mqttPacket->_len++] = 4;

    /* 可变头部：连接标志(该函数开头处理的数据) */
    mqttPacket->_data[mqttPacket->_len++] = flags;

    /* 可变头部：保持连接的时间(秒) */
    mqttPacket->_data[mqttPacket->_len++] = MOSQ_MSB(cTime);
    mqttPacket->_data[mqttPacket->_len++] = MOSQ_LSB(cTime);


    /* 消息体：设备 ID长度、设备 ID */
    mqttPacket->_data[mqttPacket->_len++] = MOSQ_MSB(devid_len);
    mqttPacket->_data[mqttPacket->_len++] = MOSQ_LSB(devid_len);

    memcpy((int8 *)mqttPacket->_data + mqttPacket->_len, devid, devid_len);
    mqttPacket->_len += devid_len;

    /* 消息体：遗嘱标志 和 遗嘱内容 */
    if(flags & MQTT_CONNECT_WILL_FLAG)
    {
        unsigned short mLen = 0;

        if(!will_msg)
            will_msg = "";

        mLen = strlen(will_topic);
        mqttPacket->_data[mqttPacket->_len++] = MOSQ_MSB(mLen);
        mqttPacket->_data[mqttPacket->_len++] = MOSQ_LSB(mLen);
        memcpy((int8 *)mqttPacket->_data + mqttPacket->_len, will_topic, mLen);
        mqttPacket->_len += mLen;

        mLen = strlen(will_msg);
        mqttPacket->_data[mqttPacket->_len++] = MOSQ_MSB(mLen);
        mqttPacket->_data[mqttPacket->_len++] = MOSQ_LSB(mLen);
        memcpy((int8 *)mqttPacket->_data + mqttPacket->_len, will_msg, mLen);
        mqttPacket->_len += mLen;
    }

    /* 写入用户名。 */
    if(flags & MQTT_CONNECT_USER_NAME)
    {
        unsigned short user_len = strlen(user);

        mqttPacket->_data[mqttPacket->_len++] = MOSQ_MSB(user_len);
        mqttPacket->_data[mqttPacket->_len++] = MOSQ_LSB(user_len);
        memcpy((int8 *)mqttPacket->_data + mqttPacket->_len, user, user_len);
        mqttPacket->_len += user_len;
    }

    /* 消息体：密码 */
    if(flags & MQTT_CONNECT_PASSORD)
    {
        unsigned short psw_len = strlen(password);

        /* OneNET 可能截断过长的 Token。 */

        mqttPacket->_data[mqttPacket->_len++] = MOSQ_MSB(psw_len);
        mqttPacket->_data[mqttPacket->_len++] = MOSQ_LSB(psw_len);
        memcpy((int8 *)mqttPacket->_data + mqttPacket->_len, password, psw_len);
        mqttPacket->_len += psw_len;
    }

    return 0;

}

/* 组装 DISCONNECT 报文。 */
uint1 MQTT_PacketDisConnect(MQTT_PACKET_STRUCTURE *mqttPacket)
{

    MQTT_NewBuffer(mqttPacket, 2);
    if(mqttPacket->_data == NULL)
        return 1;


    /* 固定头部：头部消息 */
    mqttPacket->_data[mqttPacket->_len++] = MQTT_PKT_DISCONNECT << 4;

    /* 固定头部：剩余长度值 */
    mqttPacket->_data[mqttPacket->_len++] = 0;

    return 0;

}

/* 解析连接应答。 */
uint8 MQTT_UnPacketConnectAck(uint8 *rev_data)
{
    size_t length = 0U;
    if (MQTT_FrameLength(rev_data, 5U, &length) < 0 || length == 0U) return 255U;
    return MQTT_UnPacketConnectAckLength(rev_data, length);
}

/* 组装 OneNET 数据点上传报文。 */
uint1 MQTT_PacketSaveData(const int8 *pro_id, const char *dev_name,
                                int16 send_len, int8 *type_bin_head, MQTT_PACKET_STRUCTURE *mqttPacket)
{

    char topic_buf[48];

    snprintf(topic_buf, sizeof(topic_buf), "$sys/%s/%s/thing/property/post", pro_id, dev_name);

    if(MQTT_PacketPublish(MQTT_PUBLISH_ID, topic_buf, NULL, send_len + 0, MQTT_QOS_LEVEL1, 0, 1, mqttPacket) == 0)
    {
//
    }
    else
        return 1;

    return 0;

}

/* 组装二进制文件上传报文。 */
uint1 MQTT_PacketSaveBinData(const int8 *name, int16 file_len, MQTT_PACKET_STRUCTURE *mqttPacket)
{

    uint1 result = 1;
    int8 *bin_head = NULL;
    uint8 bin_head_len = 0;
    int8 *payload = NULL;
    int32 payload_size = 0;

    bin_head = (int8 *)MQTT_MallocBuffer(13 + strlen(name));
    if(bin_head == NULL)
        return result;

    sprintf(bin_head, "{\"ds_id\":\"%s\"}", name);

    bin_head_len = strlen(bin_head);
    payload_size = 7 + bin_head_len + file_len;

    payload = (int8 *)MQTT_MallocBuffer(payload_size - file_len);
    if(payload == NULL)
    {
        MQTT_FreeBuffer(bin_head);

        return result;
    }

    payload[0] = 2;                        //类型

    payload[1] = MOSQ_MSB(bin_head_len);
    payload[2] = MOSQ_LSB(bin_head_len);

    memcpy(payload + 3, bin_head, bin_head_len);

    payload[bin_head_len + 3] = (file_len >> 24) & 0xFF;
    payload[bin_head_len + 4] = (file_len >> 16) & 0xFF;
    payload[bin_head_len + 5] = (file_len >> 8) & 0xFF;
    payload[bin_head_len + 6] = file_len & 0xFF;

    if(MQTT_PacketPublish(MQTT_PUBLISH_ID, "$dp", payload, payload_size, MQTT_QOS_LEVEL1, 0, 1, mqttPacket) == 0)
        result = 0;

    MQTT_FreeBuffer(bin_head);
    MQTT_FreeBuffer(payload);

    return result;

}

/* 解析下发命令。 */
uint8 MQTT_UnPacketCmd(uint8 *rev_data, int8 **cmdid, int8 **req, uint16 *req_len)
{

    int8 *dataPtr = strchr((int8 *)rev_data + 6, '/');    //加6是跳过头信息

    uint32 remain_len = 0;

    if(dataPtr == NULL)                                    //未找到'/'
        return 1;
    dataPtr++;                                            //跳过'/'

    MQTT_ReadLength(rev_data + 1, 4, &remain_len);        //读取剩余字节

    *cmdid = (int8 *)MQTT_MallocBuffer(37);                //cmdid固定36字节，多分配一个结束符的位置
    if(*cmdid == NULL)
        return 2;

    memset(*cmdid, 0, 37);                                //全部清零
    memcpy(*cmdid, (const int8 *)dataPtr, 36);            //复制cmdid
    dataPtr += 36;

    *req_len = remain_len - 44;                            //命令长度 = 剩余长度(remain_len) - 2 - 5($creq) - 1(\) - cmdid长度
    *req = (int8 *)MQTT_MallocBuffer(*req_len + 1);        //分配命令长度+1
    if(*req == NULL)
    {
        MQTT_FreeBuffer(*cmdid);
        return 3;
    }

    memset(*req, 0, *req_len + 1);                        //清零
    memcpy(*req, (const int8 *)dataPtr, *req_len);        //复制命令

    return 0;

}

/* 组装命令应答。 */
uint1 MQTT_PacketCmdResp(const int8 *cmdid, const int8 *req, MQTT_PACKET_STRUCTURE *mqttPacket)
{

    uint16 cmdid_len = strlen(cmdid);
    bool status = 0;
    (void)req;

    int8 *payload = MQTT_MallocBuffer(cmdid_len + 7);
    if(payload == NULL)
        return 1;

    memset(payload, 0, cmdid_len + 7);
    memcpy(payload, "$crsp/", 6);
    memcpy(payload + 6, cmdid, cmdid_len);

    if(MQTT_PacketPublish(MQTT_PUBLISH_ID, payload, req, strlen(req), MQTT_QOS_LEVEL0, 0, 1, mqttPacket) == 0)
        status = 0;
    else
        status = 1;

    MQTT_FreeBuffer(payload);

    return status;

}

/* 组装 SUBSCRIBE 报文。 */
uint8 MQTT_PacketSubscribe(uint16 pkt_id, enum MqttQosLevel qos, const int8 *topics[], uint8 topics_cnt, MQTT_PACKET_STRUCTURE *mqttPacket)
{

    uint32 topic_len = 0, remain_len = 0;
    int16 len = 0;
    uint8 i = 0;

    if(pkt_id == 0)
        return 1;

    /* 计算主题长度 */
    for(; i < topics_cnt; i++)
    {
        if(topics[i] == NULL)
            return 2;

        topic_len += strlen(topics[i]);
    }

    /* 长度含报文编号和主题过滤器；每个过滤器另含 2 字节长度及 1 字节 QoS。 */
    remain_len = 2 + 3 * topics_cnt + topic_len;

    /* 分配内存 */
    MQTT_NewBuffer(mqttPacket, remain_len + 5);
    if(mqttPacket->_data == NULL)
        return 3;


    /* 固定头部：头部消息 */
    mqttPacket->_data[mqttPacket->_len++] = MQTT_PKT_SUBSCRIBE << 4 | 0x02;

    /* 固定头部：剩余长度值 */
    len = MQTT_DumpLength(remain_len, mqttPacket->_data + mqttPacket->_len);
    if(len < 0)
    {
        MQTT_DeleteBuffer(mqttPacket);
        return 4;
    }
    else
        mqttPacket->_len += len;


    /* 载荷：报文编号 */
    mqttPacket->_data[mqttPacket->_len++] = MOSQ_MSB(pkt_id);
    mqttPacket->_data[mqttPacket->_len++] = MOSQ_LSB(pkt_id);

    /* 载荷：主题 */
    for(i = 0; i < topics_cnt; i++)
    {
        topic_len = strlen(topics[i]);
        mqttPacket->_data[mqttPacket->_len++] = MOSQ_MSB(topic_len);
        mqttPacket->_data[mqttPacket->_len++] = MOSQ_LSB(topic_len);

        memcpy((int8 *)mqttPacket->_data + mqttPacket->_len, topics[i], topic_len);
        mqttPacket->_len += topic_len;

        mqttPacket->_data[mqttPacket->_len++] = qos & 0xFF;
    }

    return 0;

}

/* 解析订阅确认。 */
uint8 MQTT_UnPacketSubscribe(uint8 *rev_data)
{
    size_t length = 0U;
    if (MQTT_FrameLength(rev_data, 5U, &length) < 0 || length == 0U) return 255U;
    return MQTT_UnPacketSubscribeLength(rev_data, length);
}

/* 组装 UNSUBSCRIBE 报文。 */
uint8 MQTT_PacketUnSubscribe(uint16 pkt_id, const int8 *topics[], uint8 topics_cnt, MQTT_PACKET_STRUCTURE *mqttPacket)
{

    uint32 topic_len = 0, remain_len = 0;
    int16 len = 0;
    uint8 i = 0;

    if(pkt_id == 0)
        return 1;

    /* 计算主题长度 */
    for(; i < topics_cnt; i++)
    {
        if(topics[i] == NULL)
            return 2;

        topic_len += strlen(topics[i]);
    }

    /* 长度含 2 字节报文编号和每个主题的长度字段及主题内容。 */
    remain_len = 2 + (topics_cnt << 1) + topic_len;

    /* 分配内存 */
    MQTT_NewBuffer(mqttPacket, remain_len + 5);
    if(mqttPacket->_data == NULL)
        return 3;


    /* 固定头部：头部消息 */
    mqttPacket->_data[mqttPacket->_len++] = MQTT_PKT_UNSUBSCRIBE << 4 | 0x02;

    /* 固定头部：剩余长度值 */
    len = MQTT_DumpLength(remain_len, mqttPacket->_data + mqttPacket->_len);
    if(len < 0)
    {
        MQTT_DeleteBuffer(mqttPacket);
        return 4;
    }
    else
        mqttPacket->_len += len;


    /* 载荷：报文编号 */
    mqttPacket->_data[mqttPacket->_len++] = MOSQ_MSB(pkt_id);
    mqttPacket->_data[mqttPacket->_len++] = MOSQ_LSB(pkt_id);

    /* 载荷：主题 */
    for(i = 0; i < topics_cnt; i++)
    {
        topic_len = strlen(topics[i]);
        mqttPacket->_data[mqttPacket->_len++] = MOSQ_MSB(topic_len);
        mqttPacket->_data[mqttPacket->_len++] = MOSQ_LSB(topic_len);

        memcpy((int8 *)mqttPacket->_data + mqttPacket->_len, topics[i], topic_len);
        mqttPacket->_len += topic_len;
    }

    return 0;

}

/* 解析取消订阅确认。 */
uint1 MQTT_UnPacketUnSubscribe(uint8 *rev_data)
{

    uint1 result = 1;

    if(rev_data[2] == MOSQ_MSB(MQTT_UNSUBSCRIBE_ID) && rev_data[3] == MOSQ_LSB(MQTT_UNSUBSCRIBE_ID))
    {
        result = 0;
    }

    return result;

}

/* 组装 PUBLISH 报文。 */
uint8 MQTT_PacketPublish(uint16 pkt_id, const int8 *topic,
                        const int8 *payload, uint32 payload_len,
                        enum MqttQosLevel qos, int32 retain, int32 own,
                        MQTT_PACKET_STRUCTURE *mqttPacket)
{

    uint32 total_len = 0, topic_len = 0;
    uint32 data_len = 0;
    int32 len = 0;
    uint8 flags = 0;

    /* 报文编号检查 */
    if(pkt_id == 0)
        return 1;

    /* $dp为系统上传数据点的指令 */
    for(topic_len = 0; topic[topic_len] != '\0'; ++topic_len)
    {
        if((topic[topic_len] == '#') || (topic[topic_len] == '+'))
            return 2;
    }

    /* Publish消息 */
    flags |= MQTT_PKT_PUBLISH << 4;

    /* retain标志 */
    if(retain)
        flags |= 0x01;

    /* 总长度 */
    total_len = topic_len + payload_len + 2;

    /* 写入遗嘱消息的 QoS。 */
    switch(qos)
    {
        case MQTT_QOS_LEVEL0:
            flags |= MQTT_CONNECT_WILL_QOS0;    //最多一次
        break;

        case MQTT_QOS_LEVEL1:
            flags |= 0x02;                        //最少一次
            total_len += 2;
        break;

        case MQTT_QOS_LEVEL2:
            flags |= 0x04;                        //只有一次
            total_len += 2;
        break;

        default:
        return 3;
    }

    /* 分配内存 */
    if(payload != NULL)
    {
        if(payload[0] == 2)
        {
            uint32 data_len_t = 0;

            while(payload[data_len_t++] != '}');
            data_len_t -= 3;
            data_len = data_len_t + 7;
            data_len_t = payload_len - data_len;

            MQTT_NewBuffer(mqttPacket, total_len + 3 - data_len_t);

            if(mqttPacket->_data == NULL)
                return 4;

            memset(mqttPacket->_data, 0, total_len + 3 - data_len_t);
        }
        else
        {
            MQTT_NewBuffer(mqttPacket, total_len + 5);

            if(mqttPacket->_data == NULL)
                return 4;

            memset(mqttPacket->_data, 0, total_len + 5);
        }
    }
    else
    {
        MQTT_NewBuffer(mqttPacket, total_len + 5);

        if(mqttPacket->_data == NULL)
            return 4;

        memset(mqttPacket->_data, 0, total_len + 5);
    }


    /* 固定头部：头部消息 */
    mqttPacket->_data[mqttPacket->_len++] = flags;

    /* 固定头部：剩余长度值 */
    len = MQTT_DumpLength(total_len, mqttPacket->_data + mqttPacket->_len);
    if(len < 0)
    {
        MQTT_DeleteBuffer(mqttPacket);
        return 5;
    }
    else
        mqttPacket->_len += len;


    /* 可变头部：写入主题长度、主题 */
    mqttPacket->_data[mqttPacket->_len++] = MOSQ_MSB(topic_len);
    mqttPacket->_data[mqttPacket->_len++] = MOSQ_LSB(topic_len);

    memcpy((int8 *)mqttPacket->_data + mqttPacket->_len, topic, topic_len);
    mqttPacket->_len += topic_len;
    if(qos != MQTT_QOS_LEVEL0)
    {
        mqttPacket->_data[mqttPacket->_len++] = MOSQ_MSB(pkt_id);
        mqttPacket->_data[mqttPacket->_len++] = MOSQ_LSB(pkt_id);
    }

    /* 可变头部：写入载荷 */
    if(payload != NULL)
    {
        if(payload[0] == 2)
        {
            memcpy((int8 *)mqttPacket->_data + mqttPacket->_len, payload, data_len);
            mqttPacket->_len += data_len;
        }
        else
        {
            memcpy((int8 *)mqttPacket->_data + mqttPacket->_len, payload, payload_len);
            mqttPacket->_len += payload_len;
        }
    }

    return 0;

}

/* 兼容入口，只能解包完整可读的 PUBLISH 报文。 */
uint8 MQTT_UnPacketPublishLength(const uint8 *data, size_t length,
    int8 **topic, uint16 *topic_len, int8 **payload, uint16 *payload_len,
    uint8 *qos, uint16 *pkt_id)
{
    mqtt_frame_view_t view;
    int8 *topic_copy, *payload_copy;
    size_t payload_size;

    /* 失败时清空输出，避免调用方拿到半成品缓冲。 */
    if (topic != NULL) *topic = NULL;
    if (payload != NULL) *payload = NULL;
    if (topic_len != NULL) *topic_len = 0U;
    if (payload_len != NULL) *payload_len = 0U;
    if (qos != NULL) *qos = 0U;
    if (pkt_id != NULL) *pkt_id = 0U;
    if (topic == NULL || payload == NULL || topic == payload || topic_len == NULL ||
        payload_len == NULL || qos == NULL || pkt_id == NULL ||
        !MQTT_PublishView(data, length, &view)) return 255U;
    if (MQTT_CommandTopic(data, &view)) return MQTT_PKT_CMD;
    payload_size = length - view.payload_offset;
    topic_copy = MQTT_MallocBuffer(view.topic_length + 1U);
    if (topic_copy == NULL) return 255U;
    payload_copy = MQTT_MallocBuffer(payload_size + 1U);
    if (payload_copy == NULL)
    {
        MQTT_FreeBuffer(topic_copy);
        return 255U;
    }
    memcpy(topic_copy, data + view.header_length + 2U, view.topic_length);
    topic_copy[view.topic_length] = '\0';
    memcpy(payload_copy, data + view.payload_offset, payload_size);
    payload_copy[payload_size] = '\0';
    *topic = topic_copy;
    *payload = payload_copy;
    *topic_len = (uint16)view.topic_length;
    *payload_len = (uint16)payload_size;
    *qos = view.qos;
    *pkt_id = view.packet_id;
    return 0U;
}

uint8 MQTT_UnPacketPublish(uint8 *rev_data, int8 **topic, uint16 *topic_len,
                          int8 **payload, uint16 *payload_len, uint8 *qos, uint16 *pkt_id)
{
    size_t length = 0U;
    if (MQTT_FrameLength(rev_data, 5U, &length) < 0) length = 0U;
    return MQTT_UnPacketPublishLength(rev_data, length, topic, topic_len,
                                      payload, payload_len, qos, pkt_id);
}

/* 组装 QoS 1 的发布确认。 */
uint1 MQTT_PacketPublishAck(uint16 pkt_id, MQTT_PACKET_STRUCTURE *mqttPacket)
{

    MQTT_NewBuffer(mqttPacket, 4);
    if(mqttPacket->_data == NULL)
        return 1;


    /* 固定头部：头部消息 */
    mqttPacket->_data[mqttPacket->_len++] = MQTT_PKT_PUBACK << 4;

    /* 固定头部：剩余长度 */
    mqttPacket->_data[mqttPacket->_len++] = 2;


    /* 可变头部：报文编号长度 */
    mqttPacket->_data[mqttPacket->_len++] = pkt_id >> 8;
    mqttPacket->_data[mqttPacket->_len++] = pkt_id & 0xff;

    return 0;

}

/* 解析发布确认。 */
uint1 MQTT_UnPacketPublishAck(uint8 *rev_data)
{
    size_t length = 0U;
    if (MQTT_FrameLength(rev_data, 5U, &length) < 0 || length == 0U) return 1U;
    return MQTT_UnPacketPublishAckLength(rev_data, length);
}

/* 组装 QoS 2 的 PUBREC 报文。 */
uint1 MQTT_PacketPublishRec(uint16 pkt_id, MQTT_PACKET_STRUCTURE *mqttPacket)
{

    MQTT_NewBuffer(mqttPacket, 4);
    if(mqttPacket->_data == NULL)
        return 1;


    /* 固定头部：头部消息 */
    mqttPacket->_data[mqttPacket->_len++] = MQTT_PKT_PUBREC << 4;

    /* 固定头部：剩余长度 */
    mqttPacket->_data[mqttPacket->_len++] = 2;


    /* 可变头部：报文编号长度 */
    mqttPacket->_data[mqttPacket->_len++] = pkt_id >> 8;
    mqttPacket->_data[mqttPacket->_len++] = pkt_id & 0xff;

    return 0;

}

/* 解析 PUBREC。 */
uint1 MQTT_UnPacketPublishRec(uint8 *rev_data)
{

    if(rev_data[1] != 2)
        return 1;

    if(rev_data[2] == MOSQ_MSB(MQTT_PUBLISH_ID) && rev_data[3] == MOSQ_LSB(MQTT_PUBLISH_ID))
        return 0;
    else
        return 1;

}

/* 组装 QoS 2 的 PUBREL 报文。 */
uint1 MQTT_PacketPublishRel(uint16 pkt_id, MQTT_PACKET_STRUCTURE *mqttPacket)
{

    MQTT_NewBuffer(mqttPacket, 4);
    if(mqttPacket->_data == NULL)
        return 1;


    /* 固定头部：头部消息 */
    mqttPacket->_data[mqttPacket->_len++] = MQTT_PKT_PUBREL << 4 | 0x02;

    /* 固定头部：剩余长度 */
    mqttPacket->_data[mqttPacket->_len++] = 2;


    /* 可变头部：报文编号长度 */
    mqttPacket->_data[mqttPacket->_len++] = pkt_id >> 8;
    mqttPacket->_data[mqttPacket->_len++] = pkt_id & 0xff;

    return 0;

}

/* 解析 PUBREL。 */
uint1 MQTT_UnPacketPublishRel(uint8 *rev_data, uint16 pkt_id)
{

    if(rev_data[1] != 2)
        return 1;

    if(rev_data[2] == MOSQ_MSB(pkt_id) && rev_data[3] == MOSQ_LSB(pkt_id))
        return 0;
    else
        return 1;

}

/* 组装 QoS 2 的 PUBCOMP 报文。 */
uint1 MQTT_PacketPublishComp(uint16 pkt_id, MQTT_PACKET_STRUCTURE *mqttPacket)
{

    MQTT_NewBuffer(mqttPacket, 4);
    if(mqttPacket->_data == NULL)
        return 1;


    /* 固定头部：头部消息 */
    mqttPacket->_data[mqttPacket->_len++] = MQTT_PKT_PUBCOMP << 4;

    /* 固定头部：剩余长度 */
    mqttPacket->_data[mqttPacket->_len++] = 2;


    /* 可变头部：报文编号长度 */
    mqttPacket->_data[mqttPacket->_len++] = pkt_id >> 8;
    mqttPacket->_data[mqttPacket->_len++] = pkt_id & 0xff;

    return 0;

}

/* 解析 PUBCOMP。 */
uint1 MQTT_UnPacketPublishComp(uint8 *rev_data)
{

    if(rev_data[1] != 2)
        return 1;

    if(rev_data[2] == MOSQ_MSB(MQTT_PUBLISH_ID) && rev_data[3] == MOSQ_LSB(MQTT_PUBLISH_ID))
        return 0;
    else
        return 1;

}

/* 组装 PINGREQ 报文。 */
uint1 MQTT_PacketPing(MQTT_PACKET_STRUCTURE *mqttPacket)
{

    MQTT_NewBuffer(mqttPacket, 2);
    if(mqttPacket->_data == NULL)
        return 1;


    /* 固定头部：头部消息 */
    mqttPacket->_data[mqttPacket->_len++] = MQTT_PKT_PINGREQ << 4;

    /* 固定头部：剩余长度 */
    mqttPacket->_data[mqttPacket->_len++] = 0;

    return 0;

}
