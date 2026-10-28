// Shim for platform::CountryFile: the junction lab has a single "mwm" (all OBF files).
#pragma once

#include <string>
#include <utility>

namespace platform
{
class CountryFile
{
public:
  explicit CountryFile(std::string name) : m_name(std::move(name)) {}
  std::string const & GetName() const { return m_name; }

private:
  std::string m_name;
};
}  // namespace platform
