// The one electrical resolver both feeds (CAN and Signal K) write into. Lives in external RAM.
#pragma once
#include "electrical.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialised on first use (from the startup task, before the CAN or Signal K tasks exist). Never NULL.
el_t *app_electrical(void);

#ifdef __cplusplus
}
#endif
