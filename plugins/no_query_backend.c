/* Negative-test DSO: intentionally omits mock_nav_backend_query_v1. */
__attribute__((visibility("default")))
int mock_nav_not_the_query(void)
{
    return 0;
}
