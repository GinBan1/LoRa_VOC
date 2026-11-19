#ifndef VOC_MLP_ADAPT_H
#define VOC_MLP_ADAPT_H

#include "voc_mlp.h"
#include <math.h>

#define VOC_ADAPT_BOOT 32U

typedef struct {
    unsigned count;
    float center, variance;
    float boot[VOC_ADAPT_BOOT];
} voc_adapt_state;

static inline float voc_adapt_clamp(float x, float low, float high)
{
    return x < low ? low : (x > high ? high : x);
}

static inline void voc_adapt_reset(voc_adapt_state *a)
{
    a->count = 0U;
    a->center = a->variance = 0.f;
    /* 完成初始化前不会读取尚未写入的条目。 */
}

static inline void voc_adapt_boot_push(voc_adapt_state *a, float voc)
{
    unsigned i;
    float scale;
    if (a->count >= VOC_ADAPT_BOOT) return;
    a->boot[a->count++] = voc;
    if (a->count < VOC_ADAPT_BOOT) return;
    /* 每段连续数据仅在初始化时排序，不占推理路径。 */
    for (i = 1U; i < VOC_ADAPT_BOOT; ++i)
    {
        float x = a->boot[i];
        unsigned j = i;
        while (j > 0U && x < a->boot[j - 1U])
        {
            a->boot[j] = a->boot[j - 1U];
            --j;
        }
        a->boot[j] = x;
    }
    a->center = a->boot[15];
    scale = voc_adapt_clamp((a->boot[23] - a->boot[7]) / 1.349f, 8.f, 40.f);
    a->variance = scale * scale;
}

static inline bool voc_adapt_inputs(const voc_adapt_state *a,
                                    const float raw[VOC_MLP_INPUT_MAX],
                                    float input[VOC_MLP_INPUT_MAX])
{
    unsigned i;
    float scale;
    if (a->count != VOC_ADAPT_BOOT || !isfinite(a->center) ||
        !isfinite(a->variance) || a->variance < 64.f || a->variance > 1600.f)
        return false;
    scale = sqrtf(a->variance);
    input[0] = (raw[0] - a->center) / scale;
    input[1] = (raw[1] - a->center) / scale;
    for (i = 2U; i < VOC_MLP_INPUT_MAX; ++i) input[i] = raw[i];
    return true;
}

/* 先按旧背景计算得分，再更新背景。 */
static inline bool voc_adapt_update(voc_adapt_state *a,
                                    const float raw[VOC_MLP_INPUT_MAX],
                                    float score, bool stayed_normal)
{
    static const float alpha = 1.f / 64.f;
    float scale, d;
    if (a->count != VOC_ADAPT_BOOT || !stayed_normal || !isfinite(score) ||
        score < 0.f || score > .35f || !isfinite(raw[0]) || !isfinite(raw[4])) return false;
    scale = sqrtf(a->variance);
    d = raw[0] - a->center;
    if (fabsf(raw[4]) > .75f * scale || fabsf(d) > 2.f * scale) return false;
    a->center += alpha * d;
    a->variance = voc_adapt_clamp((1.f - alpha) * a->variance + alpha * (d * d),
                                 64.f, 1600.f);
    return true;
}

#endif
