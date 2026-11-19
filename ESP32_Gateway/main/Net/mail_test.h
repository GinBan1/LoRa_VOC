#ifndef MAIL_TEST_H
#define MAIL_TEST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "smtp_client.h"

#define MAIL_TEST_EXPIRY_MS 300000U
#define MAIL_TEST_INTERVAL_MS 60000U

typedef struct {
    char line[32];
    size_t used;
    bool discard;
} mail_test_input_t;

typedef struct {
    uint32_t id;
    uint32_t requested_ms;
    uint32_t due_ms;
    uint32_t last_request_ms;
    unsigned attempts;
    bool pending;
    bool have_request;
} mail_test_state_t;

/* 只接受完整的 mail test 命令，不回显输入。 */
bool Mail_TestInput(mail_test_input_t *input, unsigned char byte);
/* 调用方负责串行访问；测试状态不影响正式告警。 */
bool Mail_TestRequest(mail_test_state_t *state, uint32_t now);
bool Mail_TestCurrent(const mail_test_state_t *state, uint32_t id, uint32_t now);
bool Mail_TestDue(mail_test_state_t *state, uint32_t now, bool network_ready, bool alarm_pending);
void Mail_TestComplete(mail_test_state_t *state, uint32_t id, uint32_t now, smtp_result_t result);

#endif
