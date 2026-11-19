#include "my_uart.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <rt_sys.h>
#include "run_logger.h"

/* 完整 Arm C 库需重定向 _sys_*；否则脱离调试器开机会触发半主机断点。 */
#if defined(__ARMCC_VERSION)
/* 保留禁用半主机符号，供链接器检查。 */
__attribute__((used)) static void (*const no_semihosting_guard)(void) =
    __use_no_semihosting;
#endif

#define UART_FH_STDIN   ((FILEHANDLE)0x8001)
#define UART_FH_STDOUT  ((FILEHANDLE)0x8002)
#define UART_FH_STDERR  ((FILEHANDLE)0x8003)

const char __stdin_name[]  = ":STDIN";
const char __stdout_name[] = ":STDOUT";
const char __stderr_name[] = ":STDERR";

#if !RUN_LOGGER_ACTIVE
static int uart_stdio_ready(void)
{
    return (huart1.Instance == USART1) &&
           (huart1.gState != HAL_UART_STATE_RESET);
}
#endif

FILEHANDLE _sys_open(const char *name, int openmode)
{
    (void)openmode;

    if (name == NULL) {
        return -1;
    }
    if (strcmp(name, __stdin_name) == 0) {
        return UART_FH_STDIN;
    }
    if (strcmp(name, __stdout_name) == 0) {
        return UART_FH_STDOUT;
    }
    if (strcmp(name, __stderr_name) == 0) {
        return UART_FH_STDERR;
    }
    return -1;
}

int _sys_close(FILEHANDLE fh)
{
    return ((fh == UART_FH_STDIN) || (fh == UART_FH_STDOUT) ||
            (fh == UART_FH_STDERR)) ? 0 : -1;
}

int _sys_write(FILEHANDLE fh, const unsigned char *buf, unsigned len, int mode)
{
#if RUN_LOGGER_ACTIVE
    /* RUN 帧走独立发送函数，不经 stdio。 */
    (void)fh; (void)buf; (void)len; (void)mode;
    return 0;
#else
    unsigned remaining = len;
    (void)mode;

    if (((fh != UART_FH_STDOUT) && (fh != UART_FH_STDERR)) ||
        (buf == NULL) || !uart_stdio_ready()) {
        return (int)len;
    }

    while (remaining > 0U) {
        uint16_t chunk = (remaining > 0xFFFFU) ? 0xFFFFU : (uint16_t)remaining;
        if (HAL_UART_Transmit(&huart1, (uint8_t *)buf, chunk, HAL_MAX_DELAY) != HAL_OK) {
            return (int)remaining;
        }
        buf += chunk;
        remaining -= chunk;
    }
    return 0;
#endif
}

int _sys_read(FILEHANDLE fh, unsigned char *buf, unsigned len, int mode)
{
    (void)fh;
    (void)buf;
    (void)mode;
    return (int)(len | 0x80000000U);
}

int _sys_istty(FILEHANDLE fh)
{
    return ((fh == UART_FH_STDIN) || (fh == UART_FH_STDOUT) ||
            (fh == UART_FH_STDERR)) ? 1 : 0;
}

int _sys_seek(FILEHANDLE fh, long pos)
{
    (void)fh;
    (void)pos;
    return -1;
}

int _sys_ensure(FILEHANDLE fh)
{
    return ((fh == UART_FH_STDOUT) || (fh == UART_FH_STDERR)) ? 0 : -1;
}

long _sys_flen(FILEHANDLE fh)
{
    (void)fh;
    return 0;
}

int _sys_tmpnam2(char *name, int sig, unsigned maxlen)
{
    (void)name;
    (void)sig;
    (void)maxlen;
    return -1;
}

char *_sys_command_string(char *cmd, int len)
{
    if ((cmd != NULL) && (len > 0)) {
        cmd[0] = '\0';
    }
    return cmd;
}

void _ttywrch(int ch)
{
#if RUN_LOGGER_ACTIVE
    (void)ch;
#else
    uint8_t data = (uint8_t)ch;
    if (uart_stdio_ready()) {
        (void)HAL_UART_Transmit(&huart1, &data, 1U, HAL_MAX_DELAY);
    }
#endif
}

__attribute__((noreturn)) void _sys_exit(int return_code)
{
    (void)return_code;
    while (1) {
    }
}

/* 重定向 printf。 */
int fputc(int ch, FILE *f)
{
    (void)f;
#if !RUN_LOGGER_ACTIVE
    HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
#endif
    return ch;
}

/* MicroLib 所需的 C++ 运行库兼容接口。 */

/* 登记静态对象析构函数。 */
typedef void (*destructor_fn)(void *);
static destructor_fn _dtor_fn;
static void *_dtor_arg;

int __aeabi_atexit(void *arg, void (*destructor)(void *), void *dso_handle)
{
    (void)dso_handle;
    _dtor_fn = destructor;
    _dtor_arg = arg;
    return 0;
}

/* 保存退出回调。 */
static void (*_atexit_fns[32])(void);
static int _atexit_count = 0;

int atexit(void (*fn)(void))
{
    if (_atexit_count < 32) {
        _atexit_fns[_atexit_count++] = fn;
        return 0;
    }
    return -1;
}

void _atexit_init(void) {}
void *_atexit_mutex = 0;

/* 断言处理。 */
void __aeabi_assert(const char *expr, const char *file, int line)
{
    (void)expr; (void)file; (void)line;
    while (1) {}
}

/* micro_string.cpp 依赖的宽字符接口。 */
#include <wchar.h>
size_t wcslen(const wchar_t *s) { const wchar_t *p = s; while (*p) p++; return p - s; }
wchar_t *wmemchr(const wchar_t *s, wchar_t c, size_t n) { for (size_t i = 0; i < n; i++) if (s[i] == c) return (wchar_t *)&s[i]; return 0; }
int wmemcmp(const wchar_t *s1, const wchar_t *s2, size_t n) { for (size_t i = 0; i < n; i++) { if (s1[i] != s2[i]) return s1[i] - s2[i]; } return 0; }
unsigned long wcstoul(const wchar_t *nptr, wchar_t **endptr, int base) { (void)base; if (endptr) *endptr = (wchar_t *)nptr; return 0; }
long wcstol(const wchar_t *nptr, wchar_t **endptr, int base) { (void)base; if (endptr) *endptr = (wchar_t *)nptr; return 0; }
long long wcstoll(const wchar_t *nptr, wchar_t **endptr, int base) { (void)base; if (endptr) *endptr = (wchar_t *)nptr; return 0; }
unsigned long long wcstoull(const wchar_t *nptr, wchar_t **endptr, int base) { (void)base; if (endptr) *endptr = (wchar_t *)nptr; return 0; }
int __2swprintf(wchar_t *s, size_t n, const wchar_t *fmt, ...) { (void)n; (void)fmt; if (s) s[0] = 0; return 0; }
double __wcstod_int(const wchar_t *s, wchar_t **end) { if (end) *end = (wchar_t *)s; return 0.0; }
float __wcstof_int(const wchar_t *s, wchar_t **end) { if (end) *end = (wchar_t *)s; return 0.0f; }
