#include "sm_reload_abi.h"

#include <assert.h>
#include <string.h>

int main()
{
    SmServiceStartInfo request;
    SmStartResponseInfo response;
    memset(&request, 0, sizeof(request));
    memset(&response, 0, sizeof(response));
    memcpy(request.service_name, "nv_navigation", sizeof("nv_navigation"));
    memcpy(response.service_name, "nv_navigation", sizeof("nv_navigation"));
    assert(sizeof(request) == 0x108);
    assert(sizeof(response) == 0x104);
    assert(request.user_mode == 0);
    assert(request.startup_reason == 0);
    assert(response.operation_result == 0);
    assert(strcmp(request.service_name, response.service_name) == 0);
    return 0;
}
