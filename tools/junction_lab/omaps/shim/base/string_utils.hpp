// Shim for the two helpers lanes_parser.cpp uses from Organic Maps base/string_utils.hpp.
#pragma once

namespace strings
{
inline bool IsASCIISpace(char c)
{
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

inline char AsciiToLower(char c)
{
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}
}  // namespace strings
