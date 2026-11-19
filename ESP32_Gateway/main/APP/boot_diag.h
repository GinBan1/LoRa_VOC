#ifndef BOOT_DIAG_H
#define BOOT_DIAG_H

#include <stddef.h>
#include <stdint.h>

/* 启动任务前初始化，记录只存于内存。 */
void Boot_Diag_Init(void);
void Boot_Diag_Task(void);
uint32_t Boot_Diag_GetBootId(void);
int Boot_Diag_GetResetReason(void);
const char *Boot_Diag_ResetName(int reason);
const char *Boot_Diag_GetResetName(void);
/* 写入邮件用的启动信息；缓冲不足返回 0，不输出残缺字段。 */
uint8_t Boot_Diag_Format(char *buffer, size_t capacity);

#endif
