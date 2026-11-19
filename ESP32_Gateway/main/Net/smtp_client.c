#include "smtp_client.h"

#include <stdio.h>
#include <string.h>

#define SMTP_DEADLINE_MS 45000U
#define SMTP_REPLY_LIMIT 16384U
#define SMTP_LINE_LIMIT 512U

static void SMTP_Clear(void *buffer, size_t size)
{
    volatile unsigned char *p = buffer;
    while (size--) *p++ = 0;
}

typedef struct {
    const smtp_io_t *io;
    uint32_t started_ms;
    char buffer[512];
    size_t position;
    size_t length;
} smtp_session_t;

static bool SMTP_TextValid(const char *text, size_t limit)
{
    if (text == NULL) return false;
    for (size_t i = 0; i <= limit; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c == 0) return i != 0;
        if (i == limit || c < 32 || c > 126) return false;
    }
    return false;
}

bool SMTP_AddressValid(const char *address)
{
    if (!SMTP_TextValid(address, 120)) return false;
    const char *at = strchr(address, '@');
    if (at == NULL || at == address || at[1] == 0 || strchr(at + 1, '@')) return false;
    for (const char *p = address; *p; ++p) {
        unsigned char c = (unsigned char)*p;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || strchr("._+-@", c))) return false;
    }
    return true;
}

static bool SMTP_Base64Span(const char *text, size_t n)
{
    bool padding = false;
    if (n == 0U || n % 4U != 0U) return false;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c == '=') { padding = true; if (i < n - 2) return false; }
        else if (padding || !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '+' || c == '/')) return false;
    }
    return true;
}

static bool SMTP_Base64Valid(const char *text)
{
    return SMTP_TextValid(text, 400) && SMTP_Base64Span(text, strlen(text));
}

static bool SMTP_BodyValid(const char *text)
{
    if (text == NULL) return false;
    size_t line = 0;
    for (size_t i = 0; i < 2048; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c == 0) return line == 0;
        if (c == '\r') {
            if (text[i + 1] != '\n') return false;
            ++i;
            line = 0;
        } else {
            if (c < 32 || c > 126 || ++line > 480) return false;
        }
    }
    return false;
}

static bool SMTP_MimeValid(const char *subject, const char *body)
{
    size_t n = strlen(subject);
    if (n < 16U || n > 67U || memcmp(subject, "=?UTF-8?B?", 10U) != 0 ||
        memcmp(subject + n - 2U, "?=", 2U) != 0 ||
        !SMTP_Base64Span(subject + 10, n - 12U) || !*body) return false;
    const char *start = body;
    while (*start) {
        const char *end = strstr(start, "\r\n");
        if (!end || (size_t)(end - start) > 76U ||
            !SMTP_Base64Span(start, (size_t)(end - start))) return false;
        if (end[2] && end[-1] == '=') return false;
        start = end + 2;
    }
    return true;
}

static smtp_result_t SMTP_Check(smtp_session_t *session)
{
    const smtp_io_t *io = session->io;
    if ((uint32_t)(io->NowMs(io->context) - session->started_ms) >= SMTP_DEADLINE_MS)
        return SMTP_TIMEOUT;
    if (io->IsCurrent && !io->IsCurrent(io->context)) return SMTP_STALE;
    return SMTP_OK;
}

static smtp_result_t SMTP_Write(smtp_session_t *session, const char *data, size_t size)
{
    size_t offset = 0;
    while (offset < size) {
        smtp_result_t result = SMTP_Check(session);
        if (result != SMTP_OK) return result;
        int n = session->io->Write(session->io->context, data + offset, size - offset);
        if (n == -2) { session->io->WaitMs(session->io->context, 10); continue; }
        if (n <= 0 || (size_t)n > size - offset) return SMTP_IO;
        offset += (size_t)n;
    }
    return SMTP_OK;
}

static smtp_result_t SMTP_Line(smtp_session_t *session, char *line, size_t *length)
{
    size_t count = 0;
    for (;;) {
        smtp_result_t result = SMTP_Check(session);
        if (result != SMTP_OK) return result;
        if (session->position == session->length) {
            int n = session->io->Read(session->io->context, session->buffer, sizeof(session->buffer));
            if (n == -2) { session->io->WaitMs(session->io->context, 10); continue; }
            if (n <= 0 || (size_t)n > sizeof(session->buffer)) return SMTP_IO;
            session->position = 0;
            session->length = (size_t)n;
        }
        char c = session->buffer[session->position++];
        if (count && line[count - 1] == '\r' && c != '\n') return SMTP_REJECTED;
        if (count >= SMTP_LINE_LIMIT) return SMTP_REJECTED;
        line[count++] = c;
        if (c == '\n') {
            if (count < 2 || line[count - 2] != '\r') return SMTP_REJECTED;
            line[count - 2] = 0;
            *length = count;
            return SMTP_OK;
        }
        if (c != '\r' && ((unsigned char)c < 32 || (unsigned char)c > 126)) return SMTP_REJECTED;
    }
}

static smtp_result_t SMTP_Reply(smtp_session_t *session, int expected, int alternative)
{
    char line[SMTP_LINE_LIMIT + 1];
    size_t total = 0;
    int first_code = 0;
    for (;;) {
        size_t length = 0;
        smtp_result_t result = SMTP_Line(session, line, &length);
        if (result != SMTP_OK) return result;
        if ((total += length) > SMTP_REPLY_LIMIT || length < 5) return SMTP_REJECTED;
        if (line[0] < '0' || line[0] > '9' || line[1] < '0' || line[1] > '9' ||
            line[2] < '0' || line[2] > '9' ||
            (line[3] != ' ' && line[3] != '-' && line[3] != 0)) return SMTP_REJECTED;
        int code = (line[0] - '0') * 100 + (line[1] - '0') * 10 + line[2] - '0';
        if (first_code && code != first_code) return SMTP_REJECTED;
        first_code = code;
        if (line[3] != '-') return code == expected || code == alternative ? SMTP_OK : SMTP_REJECTED;
    }
}

static smtp_result_t SMTP_Command(smtp_session_t *session, const char *command, int expected, int alternative)
{
    smtp_result_t result = SMTP_Write(session, command, strlen(command));
    return result == SMTP_OK ? SMTP_Reply(session, expected, alternative) : result;
}

static smtp_result_t SMTP_SendImpl(const smtp_io_t *io, const smtp_message_t *message, bool mime)
{
    if (!io || !io->Read || !io->Write || !io->NowMs || !io->WaitMs || !message ||
        !SMTP_AddressValid(message->from) || !SMTP_AddressValid(message->to) ||
        !SMTP_Base64Valid(message->user_base64) || !SMTP_Base64Valid(message->password_base64) ||
        !SMTP_TextValid(message->subject, 120) || !SMTP_AddressValid(message->message_id) ||
        (message->date && !SMTP_TextValid(message->date, 64)) ||
        !SMTP_BodyValid(message->body) ||
        (mime && !SMTP_MimeValid(message->subject, message->body))) return SMTP_INVALID;
    smtp_session_t session = { .io = io, .started_ms = io->NowMs(io->context) };
    char command[768];
    smtp_result_t result = SMTP_Reply(&session, 220, 220);
#define SMTP_STEP(expression) do { result = (expression); if (result != SMTP_OK) return result; } while (0)
    if (result != SMTP_OK) return result;
    SMTP_STEP(SMTP_Command(&session, "EHLO air-gateway.local\r\n", 250, 250));
    SMTP_STEP(SMTP_Command(&session, "AUTH LOGIN\r\n", 334, 334));
    int n = snprintf(command, sizeof(command), "%s\r\n", message->user_base64);
    if (n < 0 || (size_t)n >= sizeof(command)) {
        SMTP_Clear(command, sizeof(command));
        return SMTP_INVALID;
    }
    result = SMTP_Command(&session, command, 334, 334);
    SMTP_Clear(command, sizeof(command));
    if (result != SMTP_OK) return result;
    n = snprintf(command, sizeof(command), "%s\r\n", message->password_base64);
    if (n < 0 || (size_t)n >= sizeof(command)) {
        SMTP_Clear(command, sizeof(command));
        return SMTP_INVALID;
    }
    result = SMTP_Command(&session, command, 235, 235);
    SMTP_Clear(command, sizeof(command));
    if (result != SMTP_OK) return result;
    n = snprintf(command, sizeof(command), "MAIL FROM:<%s>\r\n", message->from);
    if (n < 0 || (size_t)n >= sizeof(command)) return SMTP_INVALID;
    SMTP_STEP(SMTP_Command(&session, command, 250, 250));
    n = snprintf(command, sizeof(command), "RCPT TO:<%s>\r\n", message->to);
    if (n < 0 || (size_t)n >= sizeof(command)) return SMTP_INVALID;
    SMTP_STEP(SMTP_Command(&session, command, 250, 251));
    SMTP_STEP(SMTP_Command(&session, "DATA\r\n", 354, 354));
    n = snprintf(command, sizeof(command),
                 "From: <%s>\r\nTo: <%s>\r\nSubject: %s\r\nMessage-ID: <%s>\r\n"
                 "MIME-Version: 1.0\r\nContent-Type: text/plain; charset=%s\r\n%s",
                 message->from, message->to, message->subject, message->message_id,
                 mime ? "UTF-8" : "us-ascii",
                 mime ? "Content-Transfer-Encoding: base64\r\n" : "");
    if (n < 0 || (size_t)n >= sizeof(command)) return SMTP_INVALID;
    SMTP_STEP(SMTP_Write(&session, command, (size_t)n));
    if (message->date) {
        n = snprintf(command, sizeof(command), "Date: %s\r\n", message->date);
        if (n < 0 || (size_t)n >= sizeof(command)) return SMTP_INVALID;
        SMTP_STEP(SMTP_Write(&session, command, (size_t)n));
    }
    SMTP_STEP(SMTP_Write(&session, "\r\n", 2));
    const char *start = message->body;
    while (*start) {
        const char *end = strstr(start, "\r\n");
        if (!end) return SMTP_INVALID;
        if (*start == '.') SMTP_STEP(SMTP_Write(&session, ".", 1));
        SMTP_STEP(SMTP_Write(&session, start, (size_t)(end + 2 - start)));
        start = end + 2;
    }
    SMTP_STEP(SMTP_Check(&session));
    result = SMTP_Write(&session, ".\r\n", 3);
    if (result != SMTP_OK) return SMTP_AMBIGUOUS;
    result = SMTP_Reply(&session, 250, 250);
    if (result == SMTP_REJECTED) return result;
    if (result != SMTP_OK) return SMTP_AMBIGUOUS;
    /* 服务器已接收正文，退出失败不重发。 */
    (void)SMTP_Write(&session, "QUIT\r\n", 6);
    return SMTP_OK;
#undef SMTP_STEP
}

smtp_result_t SMTP_Send(const smtp_io_t *io, const smtp_message_t *message)
{
    return SMTP_SendImpl(io, message, false);
}

smtp_result_t SMTP_SendMime(const smtp_io_t *io, const smtp_message_t *message)
{
    return SMTP_SendImpl(io, message, true);
}
