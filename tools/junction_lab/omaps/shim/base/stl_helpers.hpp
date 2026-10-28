// Shim for base::LessBy / base::EqualsBy (Organic Maps base/stl_helpers.hpp).
#pragma once

namespace base
{
template <typename T, typename C>
struct LessByField
{
  T C::*m_field;
  bool operator()(C const & l, C const & r) const { return l.*m_field < r.*m_field; }
};

template <typename T, typename C>
struct EqualsByField
{
  T C::*m_field;
  bool operator()(C const & l, C const & r) const { return l.*m_field == r.*m_field; }
};

template <typename T, typename C>
LessByField<T, C> LessBy(T C::*p)
{
  return {p};
}

template <typename T, typename C>
EqualsByField<T, C> EqualsBy(T C::*p)
{
  return {p};
}
}  // namespace base
