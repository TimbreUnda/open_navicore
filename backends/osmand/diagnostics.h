#pragma once

#include <cstddef>
#include <cstdint>

namespace navicore_osmand {
void trace(const char* message) noexcept;
void trace_pointer(const char* stage, uint32_t action, int32_t x,
                   int32_t y) noexcept;
void trace_result(const char* stage, std::uint64_t revision,
                  std::size_t count) noexcept;
}
