/* Negative-test DSO: its DT_NEEDED target is deliberately removed after link. */
extern int mock_nav_missing_dependency(void);

__attribute__((visibility("default")))
int mock_nav_backend_query_v1(void *unused)
{
    (void)unused;
    return mock_nav_missing_dependency();
}
