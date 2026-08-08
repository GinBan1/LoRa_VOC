#include "boot_trace.h"

#include "main.h"
#include "usart.h"

#include <stddef.h>

#define BOOT_TRACE_UART_TIMEOUT_MS  100U

static uint16_t boot_trace_length(const char *text)
{
    uint16_t length = 0U;

    if (text == NULL)
    {
        return 0U;
    }

    while ((text[length] != '\0') && (length < UINT16_MAX))
    {
        length++;
    }
    return length;
}

static void boot_trace_write_u32(uint32_t value)
{
    char digits[10];
    uint32_t count = 0U;

    do
    {
        digits[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while ((value != 0U) && (count < sizeof(digits)));

    while (count > 0U)
    {
        uint8_t character = (uint8_t)digits[--count];
        (void)HAL_UART_Transmit(&huart1,
                               &character,
                               1U,
                               BOOT_TRACE_UART_TIMEOUT_MS);
    }
}

void BootTrace_Write(const char *text)
{
    uint16_t length = boot_trace_length(text);

    if (length > 0U)
    {
        (void)HAL_UART_Transmit(&huart1,
                               (uint8_t *)text,
                               length,
                               BOOT_TRACE_UART_TIMEOUT_MS);
    }
}

void BootTrace_AssertFailed(const char *file, uint32_t line)
{
    BootTrace_Write("\r\n[RTOS] FATAL: configASSERT at ");
    BootTrace_Write(file);
    BootTrace_Write(":");
    boot_trace_write_u32(line);
    BootTrace_Write("\r\n");

    __disable_irq();
    for (;;)
    {
    }
}

void BootTrace_Fatal(const char *reason)
{
    BootTrace_Write("\r\n[FATAL] ");
    BootTrace_Write(reason);
    BootTrace_Write("\r\n");
}
