#include "alarm.h"
#include "telemetry_protocol.h"

#include <limits.h>
#include <string.h>

static void Alarm_ResetCandidates(alarm_state_t *state)
{
    for (unsigned i = 0; i < ALARM_CHANNEL_COUNT; ++i)
    {
        state->channels[i].candidate = false;
        state->channels[i].event_id = 0;
    }
}

bool Alarm_Init(alarm_state_t *state, const alarm_config_t *config)
{
    if (state == NULL)
        return false;
    memset(state, 0, sizeof(*state));
    if (config == NULL || config->voc_enter > 500 || config->voc_clear == 0 ||
        config->voc_clear >= config->voc_enter || config->up_enter > 1000 ||
        config->up_clear < 500 || config->up_clear >= config->up_enter ||
        config->confidence_min < 500 || config->confidence_min > 1000 ||
        config->hold_ms == 0 || config->hold_ms > INT32_MAX ||
        config->cooldown_ms > INT32_MAX || config->fresh_ms == 0 ||
        config->fresh_ms > INT32_MAX)
        return false;
    state->config = *config;
    state->initialized = true;
    return true;
}

static int Alarm_Condition(const alarm_state_t *state, unsigned channel,
                           const alarm_input_t *input)
{
    if (channel == ALARM_CHANNEL_EVENT)
    {
        if (input->source_protocol != 2U || !input->event_ready)
            return 0;
        if (input->event_state == TELEMETRY_EVENT_ALARM || input->event_state == TELEMETRY_EVENT_RECOVERY)
            return 1;
        return input->event_state == TELEMETRY_EVENT_NORMAL ? -1 : 0;
    }
    if (channel == ALARM_CHANNEL_VOC)
    {
        if (input->voc_index >= state->config.voc_enter)
            return 1;
        if (input->voc_index <= state->config.voc_clear)
            return -1;
        return 0;
    }
    if (!input->ai_valid || input->confidence > 1000 || input->up_score > 1000)
        return 0;
    if (input->ai_state == 3 && input->confidence >= state->config.confidence_min &&
        input->up_score >= state->config.up_enter)
        return 1;
    /* 旧预测不确定时，不确认恢复。 */
    if (input->confidence >= state->config.up_clear &&
        (input->ai_state == 2 ||
         (input->ai_state == 3 && input->up_score <= state->config.up_clear)))
        return -1;
    return 0;
}

static bool Alarm_Forward(const alarm_input_t *previous, const alarm_input_t *input)
{
    uint32_t node_elapsed = input->uptime_ms - previous->uptime_ms;
    uint32_t sequence_elapsed = input->sequence - previous->sequence;
    return node_elapsed > 0 && node_elapsed <= INT32_MAX &&
           sequence_elapsed > 0 && sequence_elapsed <= INT32_MAX;
}

static bool Alarm_Valid(const alarm_input_t *input)
{
    if (input->source_protocol > 2U)
        return false;
    bool sensors = input->fresh && input->sensors_valid &&
                   input->voc_index > 0 && input->voc_index <= 500;
    if (input->source_protocol != 2U)
        return sensors;
    uint32_t diagnostic_age = input->uptime_ms - input->event_end_ms;
    return sensors && input->event_ready &&
           input->event_state >= TELEMETRY_EVENT_NORMAL && input->event_state <= TELEMETRY_EVENT_RECOVERY &&
           diagnostic_age <= TELEMETRY_EVENT_FRESH_MS &&
           input->event_rx_age_ms <= TELEMETRY_EVENT_FRESH_MS - diagnostic_age &&
           input->event_voc_x10 > 0U && input->event_voc_x10 <= 5000U &&
           ((input->event_state != TELEMETRY_EVENT_ALARM && input->event_state != TELEMETRY_EVENT_RECOVERY) ||
            input->event_episode_id != 0U);
}

/* 沿用节点已确认的事件；通信中断不清除通知记录。 */
static bool Alarm_MirrorEvent(alarm_state_t *state, const alarm_input_t *input,
                              const alarm_input_t *previous, bool continuous,
                              uint32_t now_ms, alarm_event_t *event)
{
    alarm_channel_t *channel = &state->channels[ALARM_CHANNEL_EVENT];
    alarm_node_history_t *history = &state->node_history;
    bool active = input->event_state == TELEMETRY_EVENT_ALARM || input->event_state == TELEMETRY_EVENT_RECOVERY;
    bool same_episode = continuous && previous->source_protocol == 2U &&
                        previous->event_episode_id == input->event_episode_id;
    if (!continuous)
        memset(channel, 0, sizeof(*channel));
    if (!active && input->event_state != TELEMETRY_EVENT_NORMAL)
    {
        memset(channel, 0, sizeof(*channel));
        return false;
    }
    if (active)
    {
        /* 同一事件重新可见时，沿用原编号，不补发告警。 */
        if (history->closed)
            return false;
        channel->active = true;
        channel->event_id = history->alert_emitted ? history->alert_id : 0U;
        if (history->alert_emitted)
            return false;
    }
    else
    {
        bool confirmed = channel->active && same_episode && history->alert_emitted &&
                         !history->recovery_emitted && !history->closed;
        channel->active = false;
        channel->event_id = history->recovery_emitted ? history->recovery_id : 0U;
        if (history->seen)
            history->closed = true;
        if (!confirmed)
            return false;
    }
    if (++state->next_id == 0U)
        ++state->next_id;
    channel->event_id = state->next_id;
    if (active)
    {
        history->alert_emitted = true;
        history->alert_id = channel->event_id;
    }
    else
    {
        history->recovery_emitted = true;
        history->recovery_id = channel->event_id;
    }
    *event = (alarm_event_t){
        .id = channel->event_id, .timestamp_ms = now_ms,
        .sequence = input->sequence, .uptime_ms = input->uptime_ms,
        .voc_index = input->voc_index, .channel = ALARM_CHANNEL_EVENT, .active = active,
        .source_protocol = 2U, .event_state = input->event_state,
        .event_end_ms = input->event_end_ms, .event_voc_x10 = input->event_voc_x10,
        .event_episode_id = input->event_episode_id
    };
    return true;
}

static bool Alarm_ConfirmRestart(alarm_state_t *state, const alarm_input_t *input,
                                 uint32_t now_ms)
{
    /* 没有启动编号，暂以连续三帧递进判断重启；不能用于防重放。 */
    if (state->restart_frames != 0 &&
        (input->sequence == state->restart_latest.sequence ||
         input->uptime_ms == state->restart_latest.uptime_ms))
        return false;

    bool progressing = state->restart_frames != 0 &&
                       Alarm_Forward(&state->restart_latest, input) &&
                       input->uptime_ms - state->restart_latest.uptime_ms <= state->config.fresh_ms &&
                       now_ms - state->restart_last_ms > 0 &&
                       now_ms - state->restart_last_ms <= state->config.fresh_ms;
    if (!progressing)
    {
        state->restart_frames = 0;
    }
    state->restart_latest = *input;
    state->restart_last_ms = now_ms;
    return ++state->restart_frames >= ALARM_RESTART_CONFIRM_FRAMES;
}

static bool Alarm_NodeEvidenceFresh(const alarm_state_t *state, uint32_t now_ms)
{
    if (!state->have_input || !state->input_valid || state->latest.source_protocol != 2U)
        return false;
    uint32_t age = state->latest.uptime_ms - state->latest.event_end_ms;
    if (age > TELEMETRY_EVENT_FRESH_MS ||
        state->latest.event_rx_age_ms > TELEMETRY_EVENT_FRESH_MS - age)
        return false;
    age += state->latest.event_rx_age_ms;
    return now_ms - state->last_new_ms <= state->config.fresh_ms &&
           now_ms - state->last_new_ms <= TELEMETRY_EVENT_FRESH_MS - age;
}

static bool Alarm_NodeEpisodeAccept(alarm_state_t *state, const alarm_input_t *input)
{
    alarm_node_history_t *history = &state->node_history;
    if (input->event_episode_id == 0U)
        return !history->seen; /* 事件编号 0 只用于尚未发生事件的状态。 */
    if (history->seen)
    {
        uint32_t advance = input->event_episode_id - history->episode_id;
        if (advance > INT32_MAX)
            return false; /* 断续接收也不接受回退的事件编号。 */
        if (advance == 0U)
            return !(history->closed &&
                (input->event_state == TELEMETRY_EVENT_ALARM || input->event_state == TELEMETRY_EVENT_RECOVERY));
    }
    memset(history, 0, sizeof(*history));
    history->seen = true;
    history->episode_id = input->event_episode_id;
    return true;
}

static bool Alarm_ProcessNodeEvent(alarm_state_t *state, const alarm_input_t *input,
                                   uint32_t now_ms, alarm_event_t *event)
{
    alarm_input_t previous = state->latest;
    bool valid = Alarm_Valid(input);
    bool continuous = false;
    if (state->have_input && state->latest.source_protocol != 2U)
    {
        memset(state->channels, 0, sizeof(state->channels));
        state->have_input = false;
        state->input_valid = false;
        state->restart_frames = 0U;
    }
    if (state->have_input)
    {
        bool evidence_fresh = Alarm_NodeEvidenceFresh(state, now_ms);
        bool forward = Alarm_Forward(&state->latest, input);
        if (!evidence_fresh)
        {
            memset(&state->channels[ALARM_CHANNEL_EVENT], 0, sizeof(alarm_channel_t));
            state->input_valid = false;
        }
        /* 先查帧顺序，防止迟到的无效帧清掉有效告警。 */
        if (!forward)
        {
            if (input->sequence == state->latest.sequence || input->uptime_ms == state->latest.uptime_ms ||
                !input->fresh || !Alarm_ConfirmRestart(state, input, now_ms))
                return false;
            memset(state->channels, 0, sizeof(state->channels));
            memset(&state->node_history, 0, sizeof(state->node_history));
            state->input_valid = false;
        }
        else
        {
            continuous = evidence_fresh &&
                         input->uptime_ms - state->latest.uptime_ms <= state->config.fresh_ms;
        }
        state->restart_frames = 0U;
    }
    if (valid && continuous)
    {
        uint32_t end_delta = input->event_end_ms - previous.event_end_ms;
        uint32_t episode_delta = input->event_episode_id - previous.event_episode_id;
        if (end_delta > INT32_MAX || episode_delta > INT32_MAX ||
            (end_delta == 0U && (input->event_state != previous.event_state ||
                                input->event_episode_id != previous.event_episode_id ||
                                input->event_voc_x10 != previous.event_voc_x10)))
            return false;
    }
    if (valid && !Alarm_NodeEpisodeAccept(state, input))
        return false;
    state->latest = *input;
    state->last_new_ms = now_ms;
    state->have_input = true;
    state->input_valid = valid;
    if (!valid)
    {
        memset(&state->channels[ALARM_CHANNEL_EVENT], 0, sizeof(alarm_channel_t));
        return false;
    }
    return Alarm_MirrorEvent(state, input, &previous, continuous, now_ms, event);
}

bool Alarm_EventCurrent(const alarm_state_t *state, const alarm_input_t *snapshot,
                        uint32_t now_ms, const alarm_event_t *event,
                        uint32_t max_age_ms)
{
    if (state == NULL || snapshot == NULL || event == NULL ||
        !state->initialized || !state->have_input || !state->input_valid ||
        event->channel >= ALARM_CHANNEL_COUNT || event->id == 0 ||
        now_ms - event->timestamp_ms > max_age_ms ||
        now_ms - state->last_new_ms > state->config.fresh_ms ||
        (event->channel != ALARM_CHANNEL_EVENT && !Alarm_Valid(snapshot)))
        return false;
    const alarm_channel_t *channel = &state->channels[event->channel];
    if (channel->event_id != event->id || channel->active != event->active)
        return false;

    if (event->channel == ALARM_CHANNEL_EVENT)
    {
        /* 待处理帧不能延长已确认事件的有效期。 */
        if (!Alarm_NodeEvidenceFresh(state, now_ms))
            return false;
    }

    const alarm_input_t *input = &state->latest;
    uint32_t elapsed_since_input = now_ms - state->last_new_ms;
    if ((event->channel != ALARM_CHANNEL_EVENT || snapshot->source_protocol == 2U) &&
        Alarm_Forward(&state->latest, snapshot))
    {
        if (snapshot->uptime_ms - state->latest.uptime_ms > state->config.fresh_ms ||
            !Alarm_Valid(snapshot))
            return false;
        input = snapshot;
        elapsed_since_input = 0U;
    }
    /* 重复帧和旧帧不算新证据，仍检查上一次有效帧的时效。 */
    if (event->channel == ALARM_CHANNEL_EVENT)
    {
        uint32_t age = input->uptime_ms - input->event_end_ms;
        if (event->source_protocol != 2U || input->source_protocol != 2U ||
            input->event_episode_id != event->event_episode_id ||
            age > TELEMETRY_EVENT_FRESH_MS ||
            input->event_rx_age_ms > TELEMETRY_EVENT_FRESH_MS - age)
            return false;
        age += input->event_rx_age_ms;
        if (elapsed_since_input > TELEMETRY_EVENT_FRESH_MS - age)
            return false;
    }
    return Alarm_Condition(state, event->channel, input) == (event->active ? 1 : -1);
}

bool Alarm_Process(alarm_state_t *state, const alarm_input_t *input,
                   uint32_t now_ms, alarm_event_t *event)
{
    if (state == NULL || input == NULL || event == NULL || !state->initialized)
        return false;

    if (state->have_input && state->latest.source_protocol == 2U && input->source_protocol != 2U)
        return false; /* 旧版帧不能覆盖有效的 AQ2 告警。 */
    if (input->source_protocol == 2U)
        return Alarm_ProcessNodeEvent(state, input, now_ms, event);

    bool valid = Alarm_Valid(input);
    bool duplicate = state->have_input &&
                     input->sequence == state->latest.sequence &&
                     input->uptime_ms == state->latest.uptime_ms;
    if (!valid || (duplicate && now_ms - state->last_new_ms > state->config.fresh_ms))
    {
        Alarm_ResetCandidates(state);
        state->restart_frames = 0;
        state->input_valid = false;
        return false;
    }
    if (duplicate)
        return false;

    bool continuous = false;
    if (state->have_input && input->source_protocol != state->latest.source_protocol)
    {
        /* 切换协议不算恢复。 */
        memset(state->channels, 0, sizeof(state->channels));
        state->have_input = false;
        state->input_valid = false;
        state->restart_frames = 0U;
    }
    if (state->have_input)
    {
        uint32_t node_elapsed = input->uptime_ms - state->latest.uptime_ms;
        bool forward = Alarm_Forward(&state->latest, input);
        if (!forward)
        {
            if (input->sequence == state->latest.sequence || node_elapsed == 0 ||
                !Alarm_ConfirmRestart(state, input, now_ms))
                return false;
            /* 确认重启后，清除旧会话的冷却和确认记录。 */
            memset(state->channels, 0, sizeof(state->channels));
            state->input_valid = false;
        }
        state->restart_frames = 0;
        continuous = forward && state->input_valid &&
                     node_elapsed <= state->config.fresh_ms &&
                     now_ms - state->last_new_ms <= state->config.fresh_ms;
        if (!continuous)
            Alarm_ResetCandidates(state);
    }
    state->latest = *input;
    state->last_new_ms = now_ms;
    state->have_input = true;
    state->input_valid = true;

    bool emitted = false;
    for (unsigned i = 0; i < ALARM_CHANNEL_EVENT; ++i)
    {
        alarm_channel_t *channel = &state->channels[i];
        int condition = Alarm_Condition(state, i, input);
        bool transition = channel->active ? condition == -1 : condition == 1;
        bool event_still_current = channel->active ? condition == 1 : condition == -1;
        if (!event_still_current)
            channel->event_id = 0;
        if (!transition)
        {
            channel->candidate = false;
            continue;
        }
        if (!channel->candidate || !continuous)
        {
            channel->candidate = true;
            channel->candidate_ms = now_ms;
        }
        if (emitted || now_ms - channel->candidate_ms < state->config.hold_ms)
            continue;
        if (!channel->active && channel->raised_before &&
            now_ms - channel->last_raise_ms < state->config.cooldown_ms)
            continue;

        channel->active = !channel->active;
        channel->candidate = false;
        if (channel->active)
        {
            channel->raised_before = true;
            channel->last_raise_ms = now_ms;
        }
        if (++state->next_id == 0)
            ++state->next_id;
        channel->event_id = state->next_id;
        *event = (alarm_event_t){
            .id = channel->event_id,
            .timestamp_ms = now_ms,
            .sequence = input->sequence,
            .uptime_ms = input->uptime_ms,
            .voc_index = input->voc_index,
            .up_score = input->up_score,
            .confidence = input->confidence,
            .channel = (uint8_t)i,
            .active = channel->active
        };
        emitted = true;
    }
    return emitted;
}
