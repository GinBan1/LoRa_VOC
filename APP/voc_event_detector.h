#ifndef VOC_EVENT_H
#define VOC_EVENT_H
#include <stdint.h>
#define VOC_EVENT_BUFFER 32U
typedef enum { VOC_NORMAL=0,VOC_SUSPECTED=1,VOC_ALARM=2,VOC_RECOVERY=3 } VocState;
typedef struct {float alpha,scale_floor,on_z,off_z,fixed_threshold;uint32_t period_ms;uint16_t confirm_count,recovery_count;uint8_t fixed_mode;} VocConfig;
typedef struct {VocConfig config;float buffer[VOC_EVENT_BUFFER],baseline;uint32_t last_ms;uint16_t used,next,count_on,count_recovery;uint8_t have_time,state;} VocDetector;
typedef struct {float baseline,scale,score;uint8_t ready,state,alarm_active,alarm_started,reset;} VocOutput;
VocConfig voc_default_config(void);
VocConfig voc_fixed_config(void);
int voc_init(VocDetector *,const VocConfig *);
/* 每次输入一个合格的十秒 VOC 均值；质量不符或未预热时重置状态。 */
VocOutput voc_step(VocDetector *,uint32_t,float,uint8_t,uint8_t);
#endif
