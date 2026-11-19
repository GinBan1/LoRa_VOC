#ifndef SMTP_CLIENT_H
#define SMTP_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 回调返回传输字节数，0 表示结束，-1 失败，-2 需重试。 */
typedef struct {
    void *context;
    int (*Read)(void *, char *, size_t);
    int (*Write)(void *, const char *, size_t);
    uint32_t (*NowMs)(void *);
    void (*WaitMs)(void *, uint32_t);
    bool (*IsCurrent)(void *);
} smtp_io_t;

typedef struct {
    const char *from;
    const char *to;
    const char *user_base64;
    const char *password_base64;
    const char *subject;
    const char *body;
    const char *message_id;
    const char *date;
} smtp_message_t;

typedef enum {
    SMTP_OK = 0,
    SMTP_INVALID,
    SMTP_IO,
    SMTP_TIMEOUT,
    SMTP_REJECTED,
    SMTP_STALE,
    SMTP_AMBIGUOUS
} smtp_result_t;

bool SMTP_AddressValid(const char *address);
/* 调用前须建立已验证证书和主机名的 TLS 连接。 */
smtp_result_t SMTP_Send(const smtp_io_t *io, const smtp_message_t *message);
/**
 * @brief 使用已校验的 TLS 连接发送预编码邮件。
 * 主题为 RFC 2047 Base64 单段，不超过 67 字节；正文 Base64 每行不超过 76 列。
 */
smtp_result_t SMTP_SendMime(const smtp_io_t *io, const smtp_message_t *message);

#endif
