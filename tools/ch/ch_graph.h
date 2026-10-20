// The directed road graph the hierarchy is built from (written by build_graph.py).
#pragma once

#include <cstdint>
#include <vector>

namespace onc_ch {

using i64 = int64_t;

// Edge arrays are parallel; node ids are 0..n-1.
struct Graph {
    u32 n = 0, m = 0;
    std::vector<int32_t> x, y;
    std::vector<u32> from, to, cost, time, pf, pt;
    std::vector<float> len;
    std::vector<i64> road;
    std::vector<uint8_t> rev;
};

}  // namespace onc_ch
