#pragma once

namespace Common {
template<typename T>
constexpr bool IsAligned(T value, unsigned int alignment)
{
  return (value % static_cast<T>(alignment)) == 0;
}
template<typename T>
constexpr T AlignUp(T value, unsigned int alignment)
{
  return (value + static_cast<T>(alignment - 1)) / static_cast<T>(alignment) * static_cast<T>(alignment);
}
template<typename T>
constexpr bool IsAlignedPow2(T value, unsigned int alignment)
{
  return (value & static_cast<T>(alignment - 1)) == 0;
}
template<typename T>
constexpr T AlignUpPow2(T value, unsigned int alignment)
{
  return (value + static_cast<T>(alignment - 1)) & static_cast<T>(~static_cast<T>(alignment - 1));
}
template<typename T>
constexpr T AlignDownPow2(T value, unsigned int alignment)
{
  return value & static_cast<T>(~static_cast<T>(alignment - 1));
}

#define IS_POW2(value) (((value) & ((value) - 1)) == 0)

template<typename T>
constexpr T PreviousPow2(T value)
{
  value |= (value >> 1);
  value |= (value >> 2);
  value |= (value >> 4);
  value |= (value >> 8);
  value |= (value >> 16);
  return value - (value >> 1);
}
} // namespace Common
