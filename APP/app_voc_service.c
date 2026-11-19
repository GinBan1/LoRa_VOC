#include "app_voc_service.h"
#include "app_environment.h"
#include "main.h"
#include "oled.h"
#include "run_logger.h"

#include <math.h>
#include <string.h>

#define SERVICE_WARMUP_MS 3600000U
#define SERVICE_FRESH_MS 2500U
#define SERVICE_DECISION_FRESH_MS 15000U

extern Environment_Data_t g_env_data;
static AppVocServiceSnapshot published;
static AppVocInput captured;
static AppVocDiagnostic diagnostic;
static AppVocMlpShadowDiagnostic shadow_diagnostic;
static AppVocAiView ai_view;
static uint32_t ai_end_ms;
#if APP_LIVE_AI_ENABLE
static float ai_delta;
#endif
static uint32_t published_sample_ms, episode_id, bootstrap_bins;
static uint32_t last_sequence, last_sample, last_now, last_init_count, last_init_ms;
static uint8_t published_sensor_ok, observed, captured_valid, diagnostic_present;
static uint8_t shadow_diagnostic_present;

static void clear_ai(uint8_t state)
{
    ai_view = (AppVocAiView){.state = state};
    ai_end_ms = 0U;
#if APP_LIVE_AI_ENABLE
    ai_delta = 0.f;
#endif
}

#if APP_LIVE_AI_ENABLE
static void snapshot_clear_ai(AppVocServiceSnapshot *snapshot, uint8_t state)
{
    snapshot->ai_state = state;
    snapshot->ai_end_ms = 0U;
    snapshot->ai_score = 0.f;
    snapshot->ai_delta = 0.f;
}
#endif

static void publish(const AppVocServiceSnapshot *snapshot, uint32_t sample_ms,
                    uint8_t sensors_ok)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    published = *snapshot;
    published_sample_ms = sample_ms;
    published_sensor_ok = sensors_ok;
    __set_PRIMASK(mask);
}

void voc_svc_init(void)
{
    AppVocServiceSnapshot initial;
    memset(&initial, 0, sizeof(initial));
    initial.state = APP_VOC_STATUS_WARMUP;
    initial.warmup_seconds = SERVICE_WARMUP_MS / 1000U;
    AppVocEvent_Init();
#if APP_VOC_EVENT_ENABLE && APP_VOC_MLP_SHADOW_ENABLE
    AppVocMlpShadow_Init();
#endif
    memset(&captured, 0, sizeof(captured));
    memset(&diagnostic, 0, sizeof(diagnostic));
    memset(&shadow_diagnostic, 0, sizeof(shadow_diagnostic));
    episode_id = bootstrap_bins = 0U;
    last_sequence = last_sample = last_now = last_init_count = last_init_ms = 0U;
    observed = captured_valid = diagnostic_present = 0U;
    shadow_diagnostic_present = 0U;
    clear_ai(APP_VOC_STATUS_WARMUP);
    publish(&initial, 0U, 0U);
    /* 沿用旧 RUN2 BOOT 元数据；正式任务不运行预测。 */
    RunLogger_App_Boot(HAL_GetTick());
}

void voc_svc_step(uint32_t scheduled_ms)
{
    SGP40_VOCSnapshot_t voc;
    AppVocServiceSnapshot snapshot;
    uint32_t now = HAL_GetTick();
    uint32_t elapsed;
    uint8_t warm, quality, changed, backward, fresh;
#if APP_VOC_EVENT_ENABLE && APP_VOC_MLP_SHADOW_ENABLE
    float shadow_axes[5];
#endif

    diagnostic_present = 0U;
    shadow_diagnostic_present = 0U;
    Environment_App_GetVocSnapshot(&voc);
    captured.sequence = Environment_App_GetSampleSequence();
    captured.sample_ms = Environment_App_GetLastSampleMs();
    captured.scheduled_ms = scheduled_ms;
    captured.logged_ms = now;
    captured.voc_init_count = voc.init_count;
    captured.voc_init_ms = voc.init_ms;
    captured.voc_process_count = voc.process_count;
    captured.voc = (float)g_env_data.voc_index;
    quality = (uint8_t)(g_env_data.bme680_valid && g_env_data.sgp40_valid &&
        isfinite(g_env_data.temperature) && g_env_data.temperature >= -40.f && g_env_data.temperature <= 85.f &&
        isfinite(g_env_data.humidity) && g_env_data.humidity >= 0.f && g_env_data.humidity <= 100.f &&
        isfinite(g_env_data.pressure) && g_env_data.pressure >= 300.f && g_env_data.pressure <= 1100.f &&
        g_env_data.sgp40_raw > 0U);
    captured.quality = quality;
    fresh = (uint8_t)(captured.sequence != 0U &&
                     (uint32_t)(now - captured.sample_ms) <= SERVICE_FRESH_MS);
    elapsed = now - voc.init_ms;
    warm = (uint8_t)(elapsed >= SERVICE_WARMUP_MS && elapsed < 0x80000000U && voc.process_count >= 3600U);
    changed = (uint8_t)(observed && (voc.init_count != last_init_count || voc.init_ms != last_init_ms));
    backward = (uint8_t)(observed &&
        ((uint32_t)(captured.sequence - last_sequence) >= 0x80000000U ||
         (uint32_t)(captured.sample_ms - last_sample) >= 0x80000000U ||
         (uint32_t)(now - last_now) >= 0x80000000U));

    snapshot = published; /* 只有应用任务写入快照。 */
    if (changed || backward)
    {
        /* 连续段变化时丢弃未完成的旧窗口。 */
        AppVocEvent_Reset((changed ? APP_VOC_REASON_REINIT : 0U) |
                          (backward ? APP_VOC_REASON_CLOCK : 0U));
#if APP_VOC_EVENT_ENABLE && APP_VOC_MLP_SHADOW_ENABLE
        AppVocMlpShadow_Reset((changed ? APP_VOC_REASON_REINIT : 0U) |
                             (backward ? APP_VOC_REASON_CLOCK : 0U));
#endif
        bootstrap_bins = 0U;
        snapshot.ready = 0U;
        snapshot.event_end_ms = 0U;
        snapshot.voc_mean = 0.f;
        snapshot.state = APP_VOC_STATUS_INVALID;
    }
#if APP_VOC_EVENT_ENABLE
    diagnostic_present = (uint8_t)AppVocEvent_Feed(&captured, &diagnostic);
#endif
#if APP_VOC_EVENT_ENABLE && APP_VOC_MLP_SHADOW_ENABLE
    shadow_axes[0] = g_env_data.temperature;
    shadow_axes[1] = g_env_data.humidity;
    shadow_axes[2] = g_env_data.pressure;
    shadow_axes[3] = (float)g_env_data.sgp40_raw;
    shadow_axes[4] = captured.voc;
    shadow_diagnostic_present = (uint8_t)AppVocMlpShadow_Feed(&captured, shadow_axes,
        diagnostic_present ? &diagnostic : NULL, &shadow_diagnostic);
    if (!warm)
        clear_ai(APP_VOC_STATUS_WARMUP);
    else if (!quality || !fresh || !(captured.voc >= 1.f && captured.voc <= 500.f) || backward)
        clear_ai(APP_VOC_STATUS_INVALID);
    else if (changed || ai_view.state == APP_VOC_STATUS_WARMUP)
        clear_ai(APP_VOC_STATUS_COLLECT);
    else if (shadow_diagnostic_present)
    {
        const AppVocMlpShadowDiagnostic *d = &shadow_diagnostic;
        if (d->ready && d->accepted && !d->reason_flags && d->state <= VOC_RECOVERY &&
            (uint32_t)(now - d->end_ms) <= SERVICE_DECISION_FRESH_MS &&
            d->score >= 0.f && d->score <= 1.f &&
            d->features[0] >= 1.f && d->features[0] <= 500.f)
        {
            ai_view = (AppVocAiView){d->score, d->features[0],
                (uint8_t)(APP_VOC_STATUS_NORMAL + d->state), 1U};
            ai_end_ms = d->end_ms;
#if APP_LIVE_AI_ENABLE
            ai_delta = d->features[4]; /* 本窗均值减去前六窗的均值。 */
#endif
        }
        else
            clear_ai(d->accepted && !d->reason_flags ? APP_VOC_STATUS_COLLECT : APP_VOC_STATUS_INVALID);
    }
#else
    clear_ai(warm ? APP_VOC_STATUS_INVALID : APP_VOC_STATUS_WARMUP);
#endif
    if (diagnostic_present)
    {
        if (diagnostic.detector.reset || !diagnostic.accepted)
            bootstrap_bins = 0U;
        else if (bootstrap_bins < VOC_EVENT_BUFFER)
            ++bootstrap_bins;
        snapshot.event_end_ms = diagnostic.end_ms;
        snapshot.voc_mean = diagnostic.voc_mean;
        snapshot.ready = diagnostic.detector.ready;
        if (diagnostic.detector.alarm_started)
        {
            if (++episode_id == 0U) ++episode_id;
        }
        if (diagnostic.detector.ready)
            snapshot.state = (uint8_t)(APP_VOC_STATUS_NORMAL + diagnostic.detector.state);
        else
            snapshot.state = diagnostic.warmup_ready ? APP_VOC_STATUS_COLLECT : APP_VOC_STATUS_WARMUP;
    }
    snapshot.episode_id = episode_id;
    snapshot.bootstrap_bins = bootstrap_bins;
    snapshot.warmup_seconds = elapsed < SERVICE_WARMUP_MS ?
        (SERVICE_WARMUP_MS - elapsed + 999U)/1000U : 0U;
    snapshot.sample_ms = captured.sample_ms;
    snapshot.sample_sequence = captured.sequence;
    snapshot.temperature = g_env_data.temperature;
    snapshot.humidity = g_env_data.humidity;
    snapshot.pressure = g_env_data.pressure;
    snapshot.sraw_voc = g_env_data.sgp40_raw;
    snapshot.voc_index = g_env_data.voc_index;
    snapshot.bme_valid = (uint8_t)(fresh && g_env_data.bme680_valid &&
        isfinite(snapshot.temperature) && snapshot.temperature >= -40.f && snapshot.temperature <= 85.f &&
        isfinite(snapshot.humidity) && snapshot.humidity >= 0.f && snapshot.humidity <= 100.f &&
        isfinite(snapshot.pressure) && snapshot.pressure >= 300.f && snapshot.pressure <= 1100.f);
    snapshot.sgp_valid = (uint8_t)(fresh && g_env_data.sgp40_valid &&
        snapshot.sraw_voc > 0U && snapshot.voc_index >= 0 && snapshot.voc_index <= 500);
    if (!warm)
    {
        snapshot.ready = 0U;
        snapshot.state = APP_VOC_STATUS_WARMUP;
    }
    else if (!quality || !fresh || captured.voc <= 0.f || captured.voc > 500.f || backward)
    {
        snapshot.ready = 0U;
        snapshot.state = APP_VOC_STATUS_INVALID;
    }
    else if (snapshot.event_end_ms == 0U ||
             (uint32_t)(now - snapshot.event_end_ms) > SERVICE_DECISION_FRESH_MS)
    {
        snapshot.ready = 0U;
        snapshot.state = bootstrap_bins ? APP_VOC_STATUS_COLLECT : APP_VOC_STATUS_INVALID;
    }
#if !APP_VOC_EVENT_ENABLE
    snapshot.ready = 0U;
    snapshot.state = APP_VOC_STATUS_INVALID;
#endif
#if APP_LIVE_AI_ENABLE
    snapshot_clear_ai(&snapshot, ai_view.ready ? APP_VOC_STATUS_INVALID : ai_view.state);
    if (ai_view.ready && snapshot.ready && ai_end_ms != 0U &&
        ai_end_ms == snapshot.event_end_ms && isfinite(ai_delta) &&
        ai_delta >= -499.f && ai_delta <= 499.f)
    {
        snapshot.ai_end_ms = ai_end_ms;
        snapshot.ai_state = ai_view.state;
        snapshot.ai_score = ai_view.score;
        snapshot.ai_delta = ai_delta;
    }
#endif
    publish(&snapshot, captured.sample_ms, (uint8_t)(quality && fresh));
    captured_valid = 1U;
    observed = 1U;
    last_sequence = captured.sequence;
    last_sample = captured.sample_ms;
    last_now = now;
    last_init_count = voc.init_count;
    last_init_ms = voc.init_ms;
}

void voc_svc_read(AppVocServiceSnapshot *out, uint32_t now_ms)
{
    uint32_t mask, sample_ms;
    uint8_t sensors_ok;
    if (out == NULL) return;
    mask = __get_PRIMASK();
    __disable_irq();
    *out = published;
    sample_ms = published_sample_ms;
    sensors_ok = published_sensor_ok;
    __set_PRIMASK(mask);
    /* 时钟回退或回绕时，不上报超前的事件时间。 */
    if (out->event_end_ms != 0U &&
        (uint32_t)(now_ms - out->event_end_ms) >= 0x80000000U)
    {
        out->event_end_ms = 0U;
        out->ready = 0U;
        out->state = APP_VOC_STATUS_INVALID;
    }
    if (!sensors_ok || (uint32_t)(now_ms - sample_ms) > SERVICE_FRESH_MS ||
        (out->ready && (uint32_t)(now_ms - out->event_end_ms) > SERVICE_DECISION_FRESH_MS))
    {
        out->ready = 0U;
        out->state = APP_VOC_STATUS_INVALID;
    }
    if ((uint32_t)(now_ms - sample_ms) > SERVICE_FRESH_MS)
        out->bme_valid = out->sgp_valid = 0U;
#if APP_LIVE_AI_ENABLE
    if (out->ai_end_ms != 0U && (!out->ready || out->ai_end_ms != out->event_end_ms ||
        (uint32_t)(now_ms - out->ai_end_ms) > SERVICE_DECISION_FRESH_MS))
        snapshot_clear_ai(out, APP_VOC_STATUS_INVALID);
#endif
}

void voc_svc_ai_view(AppVocAiView *out, uint32_t now_ms)
{
    if (out == NULL) return;
    *out = ai_view;
    if (out->ready && ((uint32_t)(now_ms - ai_end_ms) > SERVICE_DECISION_FRESH_MS ||
                      (uint32_t)(now_ms - published_sample_ms) > SERVICE_FRESH_MS))
        *out = (AppVocAiView){.state = APP_VOC_STATUS_INVALID};
}

int voc_svc_take_evt(AppVocDiagnostic *out)
{
    if (out == NULL || !diagnostic_present) return 0;
    *out = diagnostic;
    diagnostic_present = 0U;
    return 1;
}

int voc_svc_input(AppVocInput *out)
{
    if (out == NULL || !captured_valid) return 0;
    *out = captured;
    return 1;
}

int voc_svc_take_ai(AppVocMlpShadowDiagnostic *out)
{
    if (out == NULL || !shadow_diagnostic_present) return 0;
    *out = shadow_diagnostic;
    shadow_diagnostic_present = 0U;
    return 1;
}
