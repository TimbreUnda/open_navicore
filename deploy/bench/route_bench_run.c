/* Loads /tmp/libroute_bench.so and runs it (bench only); a first argument
 * "map-tags" runs the map data survey instead. ROUTE_BENCH_CORE
 * names another libOsmAndCore_shared.so to load first (the library's RPATH
 * would otherwise win over LD_LIBRARY_PATH). */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    const char *core = getenv("ROUTE_BENCH_CORE");
    void *library;
    int (*entry)(int, char **);
    if (core != NULL && dlopen(core, RTLD_NOW | RTLD_GLOBAL) == NULL) {
        fprintf(stderr, "dlopen %s: %s\n", core, dlerror());
        return 1;
    }
    const int worker = argc > 1 && strcmp(argv[1], "worker") == 0;
    library = dlopen(worker ? "/tmp/libroute_worker_bench.so" : "/tmp/libroute_bench.so", RTLD_NOW);
    if (library == NULL) {
        fprintf(stderr, "dlopen: %s\n", dlerror());
        return 1;
    }
    if (argc > 1 && strcmp(argv[1], "map-tags") == 0) {
        entry = (int (*)(int, char **))dlsym(library, "map_tags_main");
        if (entry == NULL) {
            fprintf(stderr, "dlsym: %s\n", dlerror());
            return 1;
        }
        return entry(argc - 1, argv + 1);
    }
    entry = (int (*)(int, char **))dlsym(library, worker ? "route_worker_bench_main" : "route_bench_main");
    if (entry == NULL) {
        fprintf(stderr, "dlsym: %s\n", dlerror());
        return 1;
    }
    return entry(argc, argv);
}
