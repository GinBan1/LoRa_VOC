#ifndef JSON_GUARD_H
#define JSON_GUARD_H
#include <stdbool.h>
#include <stddef.h>
/**
 * @brief 先检查 JSON 长度、UTF-8 和嵌套，再交给旧版 cJSON。
 * 容器最多嵌套 16 层；数字最多 127 字节，须为有限值且指数绝对值不超过 308。
 * 拒绝转义的 NUL，避免 C 字符串提前截断。
 */
bool Json_GuardValid(const char *data, size_t length);
/* 写入不含外层引号的 JSON 字符串；容量不足时清空并返回 false。 */
bool Json_EscapeString(const char *source, char *output, size_t capacity);
#endif
