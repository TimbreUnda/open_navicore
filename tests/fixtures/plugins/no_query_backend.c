/* Negative-test DSO: intentionally omits open_navi_backend_query_v1. */
__attribute__((visibility("default")))
int open_navi_not_the_query(void)
{
    return 0;
}
