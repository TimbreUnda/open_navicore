// Shim for Organic Maps base/internal/message.hpp: DebugPrint for std::vector.
#pragma once

#include <sstream>
#include <string>
#include <vector>

template <typename T>
std::string DebugPrint(std::vector<T> const & v)
{
  std::ostringstream out;
  out << "[";
  for (size_t i = 0; i < v.size(); ++i)
    out << (i ? ", " : "") << DebugPrint(v[i]);
  out << "]";
  return out.str();
}
