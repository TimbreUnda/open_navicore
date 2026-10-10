#ifndef SM_RELOAD_ABI_H
#define SM_RELOAD_ABI_H

#include <stddef.h>
#include <stdint.h>

struct SmServiceStartInfo {
    char service_name[256];
    uint32_t user_mode;
    uint32_t startup_reason;
};

struct SmStartResponseInfo {
    uint32_t operation_result;
    char service_name[256];
};

typedef char SmAssertStartInfoSize[sizeof(SmServiceStartInfo) == 0x108 ? 1 : -1];
typedef char SmAssertStartUserModeOffset[offsetof(SmServiceStartInfo, user_mode) == 0x100 ? 1 : -1];
typedef char SmAssertStartReasonOffset[offsetof(SmServiceStartInfo, startup_reason) == 0x104 ? 1 : -1];
typedef char SmAssertStartResponseSize[sizeof(SmStartResponseInfo) == 0x104 ? 1 : -1];
typedef char SmAssertStartResponseNameOffset[offsetof(SmStartResponseInfo, service_name) == 4 ? 1 : -1];

#endif
