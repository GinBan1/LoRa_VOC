#ifndef BOOT_TRACE_H
#define BOOT_TRACE_H

#include <stdint.h>

void BootTrace_Write(const char *text);
void BootTrace_AssertFailed(const char *file, uint32_t line);
void BootTrace_Fatal(const char *reason);

#endif /* BOOT_TRACE_H */
