#include "mail_test.h"

#include <string.h>

bool Mail_TestInput(mail_test_input_t *input, unsigned char byte)
{
    if (!input) return false;
    if (byte == '\r' || byte == '\n') {
        bool command = !input->discard && input->used == 9U &&
                       memcmp(input->line, "mail test", 9U) == 0;
        input->used = 0;
        input->discard = false;
        return command;
    }
    if (input->discard) return false;
    if (byte < 32U || byte > 126U || input->used >= sizeof(input->line)) {
        input->discard = true;
        input->used = 0;
        return false;
    }
    input->line[input->used++] = (char)byte;
    return false;
}

bool Mail_TestRequest(mail_test_state_t *state, uint32_t now)
{
    if (!state) return false;
    if (state->pending && (uint32_t)(now - state->requested_ms) >= MAIL_TEST_EXPIRY_MS)
        state->pending = false;
    if (state->pending || (state->have_request &&
        (uint32_t)(now - state->last_request_ms) < MAIL_TEST_INTERVAL_MS)) return false;
    if (++state->id == 0) ++state->id;
    state->requested_ms = now;
    state->last_request_ms = now;
    state->due_ms = now;
    state->attempts = 0;
    state->pending = true;
    state->have_request = true;
    return true;
}

bool Mail_TestCurrent(const mail_test_state_t *state, uint32_t id, uint32_t now)
{
    return state && state->pending && state->id == id &&
           (uint32_t)(now - state->requested_ms) < MAIL_TEST_EXPIRY_MS;
}

bool Mail_TestDue(mail_test_state_t *state, uint32_t now, bool network_ready, bool alarm_pending)
{
    if (!state || !state->pending) return false;
    if (!Mail_TestCurrent(state, state->id, now)) {
        state->pending = false;
        return false;
    }
    return network_ready && !alarm_pending && (int32_t)(now - state->due_ms) >= 0;
}

void Mail_TestComplete(mail_test_state_t *state, uint32_t id, uint32_t now, smtp_result_t result)
{
    if (!state || !state->pending || state->id != id) return;
    if (!Mail_TestCurrent(state, id, now) || result == SMTP_OK || result == SMTP_STALE ||
        result == SMTP_AMBIGUOUS || result == SMTP_INVALID || ++state->attempts >= 3U) {
        state->pending = false;
    } else {
        state->due_ms = now + (30000U << (state->attempts - 1U));
    }
}
