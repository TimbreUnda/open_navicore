// Host entry for deploy/bench/route_bench.cpp (on the radio route_bench_run dlopens it).
extern "C" int route_bench_main(int argc, char** argv);

int main(int argc, char** argv)
{
    return route_bench_main(argc, argv);
}
