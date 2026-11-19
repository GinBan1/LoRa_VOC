#ifndef ALARM_APP_H
#define ALARM_APP_H

#include "alarm.h"

void Alarm_App_Init(void);
bool Alarm_App_EventCurrent(const alarm_event_t *event);

#endif
