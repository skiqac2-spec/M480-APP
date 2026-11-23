#ifndef M480_THREADING_ALT_H
#define M480_THREADING_ALT_H
#include <windows.h>
typedef struct { CRITICAL_SECTION cs; int ok; } mbedtls_threading_mutex_t;
#endif
