#include "app_voc_mlp_shadow.h"
#include "voc_mlp.h"
#include "voc_mlp_adapt.h"
#include "voc_mlp_adapt_model.h"

#include <math.h>
#include <string.h>

#define SHADOW_BIN_MS 10000U
#define SHADOW_WARMUP_MS 3600000U
#define SHADOW_HISTORY 7U
#define SHADOW_BOOTSTRAP 32U

const char *AppVocMlpShadow_ModelTag(void)
{
    return APP_VOC_MLP_SHADOW_ENABLE ? VOC_ADAPT_MODEL_ID : "disabled";
}

#if APP_VOC_MLP_SHADOW_ENABLE
static uint32_t bin_start, last_sequence, last_sample, last_logged;
static uint32_t init_count, init_ms, pending_reason, valid_slots;
static uint32_t last_end;
static uint16_t seen_slots;
static uint8_t have_bin, have_observation, have_init, warm_seen, have_end;
static uint8_t history_next, history_used, accepted_count;
static uint8_t state, count_on, count_clear;
static double sums[5];
static float history[SHADOW_HISTORY][5];
static voc_mlp_workspace workspace;
#if SHADOW_BOOTSTRAP != VOC_ADAPT_BOOT
#error Shadow and adaptive-model bootstrap lengths must agree
#endif
static voc_adapt_state adapt;
static float adapt_input[APP_VOC_MLP_FEATURE_COUNT];

static void reset_history(void)
{
    history_next = history_used = accepted_count = 0U;
    state = VOC_NORMAL;
    count_on = count_clear = have_end = 0U;
    last_end = 0U;
    memset(history, 0, sizeof(history));
    voc_adapt_reset(&adapt);
}

static void clear_bin(uint32_t start)
{
    bin_start = start;
    valid_slots = seen_slots = warm_seen = 0U;
    memset(sums, 0, sizeof(sums));
    have_bin = 1U;
}

void AppVocMlpShadow_Init(void)
{
    bin_start = last_sequence = last_sample = last_logged = 0U;
    init_count = init_ms = pending_reason = valid_slots = 0U;
    seen_slots = 0U;
    have_bin = have_observation = have_init = warm_seen = 0U;
    memset(sums, 0, sizeof(sums));
    memset(&workspace, 0, sizeof(workspace));
    reset_history();
}

void AppVocMlpShadow_Reset(uint32_t reason_flags)
{
    reset_history();
    have_bin = have_observation = have_init = 0U;
    pending_reason |= reason_flags & (APP_VOC_REASON_CLOCK | APP_VOC_REASON_REINIT);
}

/* back=0 表示最新合格均值。特征最多取七窗，32 窗初始化计数不代表真值。 */
static double value(unsigned back, unsigned axis)
{
    unsigned index = (history_next + SHADOW_HISTORY - 1U - back) % SHADOW_HISTORY;
    return (double)history[index][axis];
}

static double mean(unsigned back, unsigned count, unsigned axis, uint8_t logarithm)
{
    double total = 0.;
    unsigned i;
    for (i = 0U; i < count; ++i)
    {
        double x = value(back + i, axis);
        total += logarithm ? log(x) : x;
    }
    return total / (double)count;
}

static int features(float out[APP_VOC_MLP_FEATURE_COUNT])
{
    double m6, variance = 0.;
    unsigned i;
    if (history_used < SHADOW_HISTORY) return 0;
    m6 = mean(0U, 6U, 4U, 0U);
    for (i = 0U; i < 6U; ++i)
    {
        double delta = value(i, 4U) - m6;
        variance += delta * delta;
    }
    out[0] = (float)value(0U, 4U);
    out[1] = (float)mean(0U, 3U, 4U, 0U);
    out[2] = (float)sqrt(variance / 6.);
    out[3] = (float)(value(0U, 4U) - value(1U, 4U));
    out[4] = (float)(value(0U, 4U) - mean(1U, 6U, 4U, 0U));
    out[5] = (float)(mean(0U, 3U, 4U, 0U) - mean(3U, 3U, 4U, 0U));
    out[6] = (float)(log(value(0U, 3U)) - mean(1U, 6U, 3U, 1U));
    out[7] = (float)(mean(0U, 3U, 3U, 1U) - mean(3U, 3U, 3U, 1U));
    out[8] = (float)(value(0U, 0U) - mean(1U, 6U, 0U, 0U));
    out[9] = (float)(value(0U, 1U) - mean(1U, 6U, 1U, 0U));
    out[10] = (float)(mean(0U, 3U, 0U, 0U) - mean(3U, 3U, 0U, 0U));
    out[11] = (float)(mean(0U, 3U, 1U, 0U) - mean(3U, 3U, 1U, 0U));
    for (i = 0U; i < APP_VOC_MLP_FEATURE_COUNT; ++i)
        if (!isfinite(out[i])) return 0;
    return 1;
}

static void update_state(AppVocMlpShadowDiagnostic *out)
{
    uint8_t before = state;
    float score = out->score;
    switch (state)
    {
    case VOC_NORMAL:
        if (score >= .5f) { count_on = 1U; state = VOC_SUSPECTED; }
        break;
    case VOC_SUSPECTED:
        if (score >= .5f)
        {
            if (++count_on >= 3U) state = VOC_ALARM;
        }
        else { state = VOC_NORMAL; count_on = 0U; }
        break;
    case VOC_ALARM:
        if (score <= .35f) { state = VOC_RECOVERY; count_clear = 1U; }
        break;
    case VOC_RECOVERY:
        if (score >= .5f) { state = VOC_ALARM; count_clear = 0U; }
        else if (score <= .35f)
        {
            if (++count_clear >= 6U)
            {
                state = VOC_NORMAL;
                count_on = count_clear = 0U;
            }
        }
        else count_clear = 0U;
        break;
    default:
        reset_history();
        out->ready = 0U;
        out->reason_flags |= APP_VOC_MLP_REASON_NUMERIC;
        break;
    }
    out->state = state;
    out->active = (uint8_t)(state == VOC_ALARM || state == VOC_RECOVERY);
    out->alarm_started = (uint8_t)(state == VOC_ALARM && before != VOC_ALARM && before != VOC_RECOVERY);
}

static void finish_bin(const AppVocDiagnostic *completed, uint8_t stale,
                       AppVocMlpShadowDiagnostic *out)
{
    float means[5];
    unsigned i;
    uint8_t before;
    uint8_t expected_accepted = (uint8_t)(warm_seen && valid_slots >= 9U && !stale);
    memset(out, 0, sizeof(*out));
    out->start_ms = bin_start;
    out->end_ms = bin_start + SHADOW_BIN_MS;
    out->valid_slots = valid_slots;
    out->reason_flags = pending_reason;
    pending_reason = 0U;
    if (stale) out->reason_flags |= APP_VOC_REASON_CLOCK;
    if (!warm_seen) out->reason_flags |= APP_VOC_REASON_WARMUP;
    else if (valid_slots < 9U) out->reason_flags |= APP_VOC_REASON_INCOMPLETE;
    for (i = 0U; i < 5U; ++i)
        means[i] = valid_slots ? (float)(sums[i] / (double)valid_slots) : 0.f;

    /* 固定规则诊断只提供窗口边界和质量，不代替模型得分；不一致时丢弃该窗。 */
    if (completed == NULL || completed->start_ms != out->start_ms ||
        completed->end_ms != out->end_ms || completed->valid_slots != valid_slots ||
        completed->warmup_ready != warm_seen || completed->accepted != expected_accepted ||
        completed->voc_mean != means[4])
        out->reason_flags |= APP_VOC_MLP_REASON_MISMATCH;
    if (completed != NULL)
        out->reason_flags |= completed->reason_flags;
    /* 连续段的首个完整窗可用于初始化，不继承旧段状态。 */
    if (out->reason_flags & (APP_VOC_REASON_CLOCK | APP_VOC_REASON_REINIT))
        reset_history();
    out->accepted = (uint8_t)(expected_accepted &&
        !(out->reason_flags & ~(APP_VOC_REASON_CLOCK | APP_VOC_REASON_REINIT)));
    if (!out->accepted)
    {
        reset_history();
        return;
    }
    if (have_end && (uint32_t)(out->end_ms - last_end) != SHADOW_BIN_MS)
        reset_history();
    last_end = out->end_ms;
    have_end = 1U;
    memcpy(history[history_next], means, sizeof(means));
    history_next = (uint8_t)((history_next + 1U) % SHADOW_HISTORY);
    if (history_used < SHADOW_HISTORY) ++history_used;
    if (accepted_count < SHADOW_BOOTSTRAP + 1U) ++accepted_count;
    if (accepted_count <= SHADOW_BOOTSTRAP)
    {
        voc_adapt_boot_push(&adapt, means[4]);
        return;
    }
    if (!features(out->features) ||
        !voc_adapt_inputs(&adapt, out->features, adapt_input) ||
        !voc_mlp_predict(&voc_adapt_model, adapt_input, &workspace, &out->score))
    {
        reset_history();
        memset(out->features, 0, sizeof(out->features));
        out->score = 0.f;
        out->accepted = 0U;
        out->reason_flags |= APP_VOC_MLP_REASON_NUMERIC;
        return;
    }
    out->ready = 1U;
    before = state;
    update_state(out);
    if (out->ready)
        (void)voc_adapt_update(&adapt, out->features, out->score,
                              before == VOC_NORMAL && state == VOC_NORMAL);
}

int AppVocMlpShadow_Feed(const AppVocInput *input, const float axes[5],
                         const AppVocDiagnostic *completed,
                         AppVocMlpShadowDiagnostic *out)
{
    uint32_t start, slot, elapsed;
    uint8_t warm, axis_valid = 1U;
    int emitted = 0;
    unsigned i;
    if (input == NULL || axes == NULL || out == NULL) return 0;
    if (have_observation && input->sequence == last_sequence) return 0;
    if ((have_observation && ((uint32_t)(input->sequence - last_sequence) >= 0x80000000U ||
                             (uint32_t)(input->logged_ms - last_logged) >= 0x80000000U)) ||
        (have_init && (init_count != input->voc_init_count || init_ms != input->voc_init_ms)))
    {
        uint32_t reason = have_init && (init_count != input->voc_init_count || init_ms != input->voc_init_ms) ? APP_VOC_REASON_REINIT : APP_VOC_REASON_CLOCK;
        AppVocMlpShadow_Reset(reason);
    }
    init_count = input->voc_init_count;
    init_ms = input->voc_init_ms;
    have_init = 1U;
    start = (input->logged_ms / SHADOW_BIN_MS) * SHADOW_BIN_MS;
    if (!have_bin) clear_bin(start);
    else if (start != bin_start)
    {
        uint8_t stale = (uint8_t)((uint32_t)(start - bin_start) != SHADOW_BIN_MS ||
            (uint32_t)(input->logged_ms - (bin_start + SHADOW_BIN_MS)) > 2500U);
        uint32_t old_start = bin_start;
        finish_bin(completed, stale, out);
        emitted = 1;
        if ((uint32_t)(start - old_start) != SHADOW_BIN_MS)
        {
            reset_history();
            pending_reason |= APP_VOC_REASON_CLOCK;
        }
        clear_bin(start);
    }
    elapsed = input->logged_ms - input->voc_init_ms;
    warm = (uint8_t)(elapsed >= SHADOW_WARMUP_MS && elapsed < 0x80000000U && input->voc_process_count >= 3600U);
    if (warm) warm_seen = 1U;
    slot = (uint32_t)(input->scheduled_ms - bin_start) / 1000U;
    for (i = 0U; i < 5U; ++i) if (!isfinite(axes[i])) axis_valid = 0U;
    if (axes[0] < -40.f || axes[0] > 85.f || axes[1] < 0.f || axes[1] > 100.f ||
        axes[2] < 300.f || axes[2] > 1100.f || axes[3] <= 0.f || axes[3] > 65535.f ||
        axes[4] != input->voc) axis_valid = 0U;
    if (warm && input->quality && axis_valid && (uint32_t)(input->logged_ms - input->sample_ms) <= 2500U &&
        (!have_observation || ((uint32_t)(input->sample_ms - last_sample) > 0U &&
                              (uint32_t)(input->sample_ms - last_sample) < 0x80000000U)) &&
        isfinite(input->voc) && input->voc > 0.f && input->voc <= 500.f && slot < 10U &&
        !(seen_slots & (uint16_t)(1U << slot)))
    {
        seen_slots |= (uint16_t)(1U << slot);
        for (i = 0U; i < 5U; ++i) sums[i] += (double)axes[i];
        ++valid_slots;
    }
    last_sequence = input->sequence;
    last_sample = input->sample_ms;
    last_logged = input->logged_ms;
    have_observation = 1U;
    return emitted;
}
#else
void AppVocMlpShadow_Init(void) {}
void AppVocMlpShadow_Reset(uint32_t reason_flags) { (void)reason_flags; }
int AppVocMlpShadow_Feed(const AppVocInput *input, const float axes[5],
                         const AppVocDiagnostic *completed, AppVocMlpShadowDiagnostic *out)
{
    (void)input; (void)axes; (void)completed; (void)out;
    return 0;
}
#endif
