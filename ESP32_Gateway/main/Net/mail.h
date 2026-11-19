#ifndef MAIL_H
#define MAIL_H

#include <stdbool.h>
#include "alarm.h"

void Mail_Init(void);
/* 将通知入队；同通道的新状态覆盖旧状态，发送交给邮件任务。 */
bool Mail_SendAlarm(const alarm_event_t *event);
/* 手动请求测试邮件，只入队，不直接发送。 */
bool Mail_RequestTest(void);

#endif
