#include "app_voc_event.h"
#include <math.h>
#include <string.h>
#define VOC_BIN_MS 10000U
#define VOC_WARMUP_MS 3600000U
static VocDetector detector;
static uint32_t bin_start,last_sequence,last_sample,last_logged,init_count,init_ms;
static uint32_t valid_slots,pending_reason;
static uint16_t seen_slots;
static double bin_sum;
static uint8_t have_bin,have_observation,have_init,warm_seen;
static void reset_detector(void)
{
    VocConfig cfg;
#if APP_VOC_EVENT_ADAPTIVE
    cfg=voc_default_config();
#else
    cfg=voc_fixed_config();
#endif
    (void)voc_init(&detector,&cfg);
}
static void clear_bin(uint32_t start)
{
    bin_start=start;valid_slots=0U;seen_slots=0U;bin_sum=0.;warm_seen=0U;have_bin=1U;
}
void AppVocEvent_Init(void)
{
    reset_detector();have_bin=have_observation=have_init=0U;
    bin_start=last_sequence=last_sample=last_logged=init_count=init_ms=0U;
    pending_reason=valid_slots=0U;seen_slots=0U;bin_sum=0.;warm_seen=0U;
}
void AppVocEvent_Reset(uint32_t reason_flags)
{
    reset_detector();have_bin=have_observation=have_init=0U;
    pending_reason|=reason_flags&(APP_VOC_REASON_CLOCK|APP_VOC_REASON_REINIT);
}
static void finish_bin(AppVocDiagnostic *out,uint8_t stale_closure)
{
    memset(out,0,sizeof(*out));
    out->start_ms=bin_start;out->end_ms=bin_start+VOC_BIN_MS;
    out->valid_slots=valid_slots;out->reason_flags=pending_reason;pending_reason=0U;
    out->warmup_ready=warm_seen;out->mode=APP_VOC_EVENT_ADAPTIVE?1U:0U;
    out->voc_mean=valid_slots?(float)(bin_sum/valid_slots):0.f;
    out->accepted=(uint8_t)(warm_seen && valid_slots>=9U && !stale_closure);
    if(stale_closure)out->reason_flags|=APP_VOC_REASON_CLOCK;
    if(!warm_seen)out->reason_flags|=APP_VOC_REASON_WARMUP;
    else if(valid_slots<9U)out->reason_flags|=APP_VOC_REASON_INCOMPLETE;
    out->detector=voc_step(&detector,out->end_ms,out->voc_mean,out->accepted,warm_seen);
}
int AppVocEvent_Feed(const AppVocInput *in,AppVocDiagnostic *out)
{
    uint32_t start,slot,age;
    uint8_t warm;
    int emitted=0;
    if(in==0 || out==0)return 0;
    if(have_observation && in->sequence==last_sequence)return 0;
    if(have_observation && (uint32_t)(in->sequence-last_sequence)>=0x80000000U) {
        reset_detector();have_bin=0U;pending_reason|=APP_VOC_REASON_CLOCK;
    }
    if(have_init && (init_count!=in->voc_init_count || init_ms!=in->voc_init_ms)) {
        reset_detector();have_bin=0U;pending_reason|=APP_VOC_REASON_REINIT;
    }
    init_count=in->voc_init_count;init_ms=in->voc_init_ms;have_init=1U;
    start=(in->logged_ms/VOC_BIN_MS)*VOC_BIN_MS;
    if(have_observation && (uint32_t)(in->logged_ms-last_logged)>=0x80000000U) {
        reset_detector();have_bin=0U;pending_reason|=APP_VOC_REASON_CLOCK;
    }
    if(!have_bin)clear_bin(start);
    else if(start!=bin_start) {
        uint32_t old_start=bin_start;
        uint8_t stale=(uint8_t)((uint32_t)(start-old_start)!=VOC_BIN_MS ||
             (uint32_t)(in->logged_ms-(old_start+VOC_BIN_MS))>2500U);
        finish_bin(out,stale);emitted=1;
        if((uint32_t)(start-old_start)!=VOC_BIN_MS) {
            reset_detector();pending_reason|=APP_VOC_REASON_CLOCK;
        }
        clear_bin(start);
    }
    warm=(uint8_t)((uint32_t)(in->logged_ms-in->voc_init_ms)>=VOC_WARMUP_MS &&
                  (uint32_t)(in->logged_ms-in->voc_init_ms)<0x80000000U &&
                  in->voc_process_count>=3600U);
    if(warm)warm_seen=1U;
    age=in->logged_ms-in->sample_ms;
    slot=(uint32_t)(in->scheduled_ms-bin_start)/1000U;
    if(warm && in->quality && age<=2500U &&
       (!have_observation || ((uint32_t)(in->sample_ms-last_sample)>0U &&
                             (uint32_t)(in->sample_ms-last_sample)<0x80000000U)) &&
       isfinite(in->voc) && in->voc>0.f &&
       in->voc<=500.f && slot<10U && !(seen_slots&(uint16_t)(1U<<slot))) {
        seen_slots|=(uint16_t)(1U<<slot);bin_sum+=in->voc;valid_slots++;
    }
    last_sequence=in->sequence;last_sample=in->sample_ms;last_logged=in->logged_ms;have_observation=1U;
    return emitted;
}
