#ifndef OPEN_NAVI_H
#define OPEN_NAVI_H

#include <stdint.h>

#if defined(__GNUC__)
#define PANA_EXPORT __attribute__((visibility("default")))
#else
#define PANA_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t word[6];
} PanaServerVersionHeadUnit;

typedef struct {
    uint16_t api_major;
    uint16_t api_minor;
    uint16_t version_a;
    uint16_t version_b;
    uint32_t packed_version;
    const char *version_text;
} PanaClientVersionNavigationSoftwareModule;

typedef struct {
    const void *path0;
    const void *path1;
    const void *path2;
    const void *path3;
    uint32_t reserved4_8[5];
    const void *path9;
    const void *path10;
} PanaInitializationData;

typedef uint32_t (*PanaGenericVirtual)(void *self, ...);

typedef struct {
    PanaGenericVirtual slot[32];
} PanaRootVtable;

typedef struct {
    const PanaRootVtable *vptr;
    void *host_proxy;
} PanaRootObject;

typedef uint32_t (*PanaGetAdapterVirtual)(void *self,
                                          uint32_t adapter,
                                          void **interface_out);

PANA_EXPORT uint32_t panaVersionCheck(
    PanaServerVersionHeadUnit server,
    PanaClientVersionNavigationSoftwareModule *client,
    uint32_t *compatibility);

PANA_EXPORT uint32_t panaInitialize(const void *init_record);
PANA_EXPORT void *panaGetSoftwareInfrastructure(void);
PANA_EXPORT void panaDeinitialize(void);

#if defined(OPEN_NAVI_TESTING)
uint32_t open_navi_gps_test_call_count(void);
uintptr_t open_navi_gps_test_last_record_address(void);
uint32_t open_navi_hmi_test_transition_reply_count(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
