#include "my_uart.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- printf retarget ----
int fputc(int ch, FILE *f)
{
    (void)f;
    HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
    return ch;
}

// ---- C++ runtime stubs for MicroLib ----

// atexit (static destructor registration)
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

// atexit internals (MicroLib missing)
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

// assert
void __aeabi_assert(const char *expr, const char *file, int line)
{
    (void)expr; (void)file; (void)line;
    while (1) {}
}

// Wide character stubs (not used by TFLM, but referenced by micro_string.cpp)
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