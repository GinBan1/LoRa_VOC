#include "json_guard.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define JSON_GUARD_MAX_DEPTH 16U
typedef struct { const uint8_t *data; size_t length, offset; } json_cursor_t;

static void Json_Space(json_cursor_t *cursor)
{
    while (cursor->offset < cursor->length)
    {
        uint8_t c = cursor->data[cursor->offset];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        ++cursor->offset;
    }
}

static bool Json_Hex4(json_cursor_t *cursor, uint32_t *value)
{
    *value = 0U;
    if (cursor->length - cursor->offset < 4U) return false;
    for (unsigned i = 0U; i < 4U; ++i)
    {
        uint8_t c = cursor->data[cursor->offset++];
        uint32_t digit;
        if (c >= '0' && c <= '9') digit = (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') digit = (uint32_t)(c - 'a') + 10U;
        else if (c >= 'A' && c <= 'F') digit = (uint32_t)(c - 'A') + 10U;
        else return false;
        *value = (*value << 4) | digit;
    }
    return true;
}

static bool Json_String(json_cursor_t *cursor)
{
    if (cursor->offset >= cursor->length || cursor->data[cursor->offset++] != '"')
        return false;
    while (cursor->offset < cursor->length)
    {
        uint8_t c = cursor->data[cursor->offset++];
        if (c == '"') return true;
        if (c < 0x20U) return false;
        if (c == '\\')
        {
            uint32_t code, low;
            if (cursor->offset >= cursor->length) return false;
            c = cursor->data[cursor->offset++];
            if (c == '"' || c == '\\' || c == '/' || c == 'b' || c == 'f' ||
                c == 'n' || c == 'r' || c == 't') continue;
            if (c != 'u' || !Json_Hex4(cursor, &code) || code == 0U) return false;
            if (code >= 0xDC00U && code <= 0xDFFFU) return false;
            if (code >= 0xD800U && code <= 0xDBFFU)
            {
                if (cursor->length - cursor->offset < 6U ||
                    cursor->data[cursor->offset] != '\\' ||
                    cursor->data[cursor->offset + 1U] != 'u') return false;
                cursor->offset += 2U;
                if (!Json_Hex4(cursor, &low) || low < 0xDC00U || low > 0xDFFFU) return false;
            }
        }
        else if (c >= 0x80U)
        {
            uint32_t code;
            unsigned trailing;
            if (c >= 0xC2U && c <= 0xDFU) { code = c & 0x1FU; trailing = 1U; }
            else if (c >= 0xE0U && c <= 0xEFU) { code = c & 0x0FU; trailing = 2U; }
            else if (c >= 0xF0U && c <= 0xF4U) { code = c & 7U; trailing = 3U; }
            else return false;
            if (cursor->length - cursor->offset < trailing) return false;
            for (unsigned i = 0U; i < trailing; ++i)
            {
                uint8_t next = cursor->data[cursor->offset++];
                if ((next & 0xC0U) != 0x80U) return false;
                code = (code << 6) | (next & 0x3FU);
            }
            if ((trailing == 1U && code < 0x80U) || (trailing == 2U && code < 0x800U) ||
                (trailing == 3U && code < 0x10000U) || code > 0x10FFFFU ||
                (code >= 0xD800U && code <= 0xDFFFU)) return false;
        }
    }
    return false;
}

static bool Json_Digit(uint8_t c) { return c >= '0' && c <= '9'; }

static bool Json_Number(json_cursor_t *cursor)
{
    size_t start = cursor->offset;
    char number[128], *end;
    if (cursor->data[cursor->offset] == '-') ++cursor->offset;
    if (cursor->offset >= cursor->length) return false;
    if (cursor->data[cursor->offset] == '0') ++cursor->offset;
    else
    {
        if (cursor->data[cursor->offset] < '1' || cursor->data[cursor->offset] > '9') return false;
        do { ++cursor->offset; }
        while (cursor->offset < cursor->length && Json_Digit(cursor->data[cursor->offset]));
    }
    if (cursor->offset < cursor->length && cursor->data[cursor->offset] == '.')
    {
        ++cursor->offset;
        if (cursor->offset >= cursor->length || !Json_Digit(cursor->data[cursor->offset])) return false;
        do { ++cursor->offset; }
        while (cursor->offset < cursor->length && Json_Digit(cursor->data[cursor->offset]));
    }
    if (cursor->offset < cursor->length &&
        (cursor->data[cursor->offset] == 'e' || cursor->data[cursor->offset] == 'E'))
    {
        unsigned exponent = 0U;
        ++cursor->offset;
        if (cursor->offset < cursor->length &&
            (cursor->data[cursor->offset] == '+' || cursor->data[cursor->offset] == '-')) ++cursor->offset;
        if (cursor->offset >= cursor->length || !Json_Digit(cursor->data[cursor->offset])) return false;
        do
        {
            exponent = exponent * 10U + (unsigned)(cursor->data[cursor->offset++] - '0');
            if (exponent > 308U) return false;
        } while (cursor->offset < cursor->length && Json_Digit(cursor->data[cursor->offset]));
    }
    size_t length = cursor->offset - start;
    if (length >= sizeof(number)) return false;
    memcpy(number, cursor->data + start, length);
    number[length] = '\0';
    double value = strtod(number, &end);
    return end == number + length && isfinite(value);
}

static bool Json_Value(json_cursor_t *cursor, unsigned depth)
{
    Json_Space(cursor);
    if (cursor->offset >= cursor->length) return false;
    uint8_t first = cursor->data[cursor->offset];
    if (first == '"') return Json_String(cursor);
    if (first == '{' || first == '[')
    {
        uint8_t close = first == '{' ? '}' : ']';
        if (depth >= JSON_GUARD_MAX_DEPTH) return false;
        ++cursor->offset;
        Json_Space(cursor);
        if (cursor->offset < cursor->length && cursor->data[cursor->offset] == close)
        { ++cursor->offset; return true; }
        for (;;)
        {
            if (first == '{')
            {
                if (!Json_String(cursor)) return false;
                Json_Space(cursor);
                if (cursor->offset >= cursor->length || cursor->data[cursor->offset++] != ':') return false;
            }
            if (!Json_Value(cursor, depth + 1U)) return false;
            Json_Space(cursor);
            if (cursor->offset >= cursor->length) return false;
            uint8_t next = cursor->data[cursor->offset++];
            if (next == close) return true;
            if (next != ',') return false;
            Json_Space(cursor);
        }
    }
    if (first == '-' || Json_Digit(first)) return Json_Number(cursor);
    const char *literal = first == 't' ? "true" : first == 'f' ? "false" : first == 'n' ? "null" : NULL;
    if (literal == NULL) return false;
    size_t length = strlen(literal);
    if (cursor->length - cursor->offset < length ||
        memcmp(cursor->data + cursor->offset, literal, length) != 0) return false;
    cursor->offset += length;
    return true;
}

bool Json_GuardValid(const char *data, size_t length)
{
    if (data == NULL || length == 0U || memchr(data, '\0', length) != NULL) return false;
    json_cursor_t cursor = {(const uint8_t *)data, length, 0U};
    if (!Json_Value(&cursor, 0U)) return false;
    Json_Space(&cursor);
    return cursor.offset == length;
}

bool Json_EscapeString(const char *source, char *output, size_t capacity)
{
    static const char hex[] = "0123456789abcdef";
    size_t position = 0U;
    if (output == NULL || capacity == 0U) return false;
    output[0] = '\0';
    if (source == NULL) return false;
    while (*source)
    {
        uint8_t c = (uint8_t)*source++;
        size_t needed = c < 0x20U ? 6U : (c == '"' || c == '\\') ? 2U : 1U;
        if (capacity - position <= needed) { output[0] = '\0'; return false; }
        if (c < 0x20U)
        {
            output[position++] = '\\'; output[position++] = 'u';
            output[position++] = '0'; output[position++] = '0';
            output[position++] = hex[c >> 4]; output[position++] = hex[c & 15U];
        }
        else
        {
            if (needed == 2U) output[position++] = '\\';
            output[position++] = (char)c;
        }
    }
    output[position] = '\0';
    return true;
}
