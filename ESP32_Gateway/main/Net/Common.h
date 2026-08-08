#ifndef __COMMON_H__
#define __COMMON_H__

/*---------------------------------------------------------------------------*/
/* Type Definition Macros                                                    */
/*---------------------------------------------------------------------------*/
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// 统一类型别名（_Bool 是GCC内置关键字，不能typedef，用bool替代）
typedef unsigned char   uint8;
typedef char            int8;
typedef unsigned short  uint16;
typedef short           int16;
typedef unsigned int    uint32;
typedef int             int32;

typedef bool            uint1;

#endif /* __COMMON_H__ */
