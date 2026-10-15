/* Negative-test DSO: its DT_NEEDED target is deliberately removed after link. */
extern int open_navi_missing_dependency(void);

__attribute__((visibility("default")))
int open_navi_backend_query_v1(void *unused)
{
    (void)unused;
    return open_navi_missing_dependency();
}
