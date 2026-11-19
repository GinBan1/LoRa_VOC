#include "voc_event_detector.h"
#include <math.h>
#include <string.h>
static float median(float *a,unsigned n){unsigned i,j;for(i=1;i<n;i++){float v=a[i];j=i;while(j&&a[j-1]>v){a[j]=a[j-1];j--;}a[j]=v;}return n%2?a[n/2]:.5f*(a[n/2-1]+a[n/2]);}
static float robust_scale(const VocDetector *d){float a[VOC_EVENT_BUFFER],v[VOC_EVENT_BUFFER],m,s;unsigned i;for(i=0;i<VOC_EVENT_BUFFER;i++)a[i]=d->buffer[i];m=median(a,VOC_EVENT_BUFFER);for(i=0;i<VOC_EVENT_BUFFER;i++)v[i]=fabsf(d->buffer[i]-m);s=1.4826f*median(v,VOC_EVENT_BUFFER);return s>d->config.scale_floor?s:d->config.scale_floor;}
static void reset(VocDetector *d){VocConfig c=d->config;memset(d,0,sizeof(*d));d->config=c;}
VocConfig voc_default_config(void){VocConfig c;memset(&c,0,sizeof(c));c.alpha=1-expf(-10.0f/1800.0f);c.scale_floor=1;c.on_z=3;c.off_z=1;c.fixed_threshold=200;c.period_ms=10000;c.confirm_count=3;c.recovery_count=6;return c;}
VocConfig voc_fixed_config(void){VocConfig c=voc_default_config();c.fixed_mode=1;return c;}
int voc_init(VocDetector *d,const VocConfig *c){if(!d||!c||!isfinite(c->alpha)||c->alpha<=0||c->alpha>1||!isfinite(c->scale_floor)||c->scale_floor<=0||!isfinite(c->on_z)||!isfinite(c->off_z)||c->off_z>=c->on_z||!isfinite(c->fixed_threshold)||c->fixed_threshold<=0||c->fixed_threshold>500||c->fixed_mode>1||!c->period_ms||!c->confirm_count||!c->recovery_count)return 0;memset(d,0,sizeof(*d));d->config=*c;return 1;}
VocOutput voc_step(VocDetector *d,uint32_t at,float x,uint8_t quality,uint8_t warm){
 VocOutput o;unsigned before;float on=d->config.fixed_mode?d->config.fixed_threshold:d->config.on_z;float off=d->config.fixed_mode?.8f*d->config.fixed_threshold:d->config.off_z;memset(&o,0,sizeof(o));
 if(!quality||!warm||!isfinite(x)||x<0||x>500){reset(d);o.reset=1;o.scale=d->config.scale_floor;return o;}
 if(d->have_time&&(uint32_t)(at-d->last_ms)!=d->config.period_ms){reset(d);o.reset=1;}
 d->last_ms=at;d->have_time=1;
 if(d->used<VOC_EVENT_BUFFER){d->buffer[d->used]=x;d->used++;d->baseline+=(x-d->baseline)/(float)d->used;o.baseline=d->baseline;o.scale=d->config.scale_floor;return o;}
 o.ready=1;o.baseline=d->baseline;o.scale=robust_scale(d);o.score=d->config.fixed_mode?x:(x-d->baseline)/o.scale;before=d->state;
 switch(d->state){
 case VOC_NORMAL:if(o.score>=on){d->count_on=1;d->state=d->config.confirm_count==1?VOC_ALARM:VOC_SUSPECTED;}break;
 case VOC_SUSPECTED:if(o.score>=on){if(++d->count_on>=d->config.confirm_count)d->state=VOC_ALARM;}else{d->state=VOC_NORMAL;d->count_on=0;}break;
 case VOC_ALARM:if(o.score<=off){d->state=VOC_RECOVERY;d->count_recovery=1;if(d->config.recovery_count==1)d->state=VOC_NORMAL;}break;
 case VOC_RECOVERY:if(o.score>=on){d->state=VOC_ALARM;d->count_recovery=0;}else if(o.score<=off){if(++d->count_recovery>=d->config.recovery_count)d->state=VOC_NORMAL;}else d->count_recovery=0;break;
 default:reset(d);o.reset=1;return o;
 }
 if(before==VOC_NORMAL&&d->state==VOC_NORMAL&&o.score<on){d->baseline+=d->config.alpha*(x-d->baseline);d->buffer[d->next]=x;d->next=(d->next+1U)%VOC_EVENT_BUFFER;}
 o.state=d->state;o.alarm_active=d->state==VOC_ALARM||d->state==VOC_RECOVERY;o.alarm_started=d->state==VOC_ALARM&&before!=VOC_ALARM&&before!=VOC_RECOVERY;return o;
}
