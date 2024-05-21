#pragma once
#include "types.h"
#include <cstdarg>
#include <cstddef>
#include <cstring>
#include <iomanip>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#if defined(__has_include) && __has_include(<charconv>)
#include <charconv>
#ifndef _MSC_VER
#include <sstream>
#endif
#else
#include <sstream>
#endif

namespace StringUtil {

/// Constructs a std::string from a format string.
std::string StdStringFromFormat(const char* format, ...) printflike(1, 2);
std::string StdStringFromFormatV(const char* format, std::va_list ap);

/// Checks if a wildcard matches a search string.
bool WildcardMatch(const char* subject, const char* mask, bool case_sensitive = true);

/// Platform-independent strcasecmp
static inline int Strcasecmp(const char* s1, const char* s2)
{
#ifdef _MSC_VER
  return _stricmp(s1, s2);
#else
  return strcasecmp(s1, s2);
#endif
}

/// Platform-independent strcasecmp
static inline int Strncasecmp(const char* s1, const char* s2, std::size_t n)
{
#ifdef _MSC_VER
  return _strnicmp(s1, s2, n);
#else
  return strncasecmp(s1, s2, n);
#endif
}

/// Wrapper arond std::from_chars
template<typename T, std::enable_if_t<std::is_integral<T>::value, bool> = true>
inline std::optional<T> FromChars(const std::string_view& str, int base = 10)
{
  T value;

#if defined(__has_include) && __has_include(<charconv>)
  const std::from_chars_result result = std::from_chars(str.data(), str.data() + str.length(), value, base);
  if (result.ec != std::errc())
    return std::nullopt;
#else
  std::string temp(str);
  std::istringstream ss(temp);
  ss >> std::setbase(base) >> value;
  if (ss.fail())
    return std::nullopt;
#endif

  return value;
}

template<typename T, std::enable_if_t<std::is_floating_point<T>::value, bool> = true>
inline std::optional<T> FromChars(const std::string_view& str)
{
  T value;

#if defined(__has_include) && __has_include(<charconv>) && defined(_MSC_VER)
  const std::from_chars_result result = std::from_chars(str.data(), str.data() + str.length(), value);
  if (result.ec != std::errc())
    return std::nullopt;
#else
  /// libstdc++ does not support from_chars with floats yet
  std::string temp(str);
  std::istringstream ss(temp);
  ss >> value;
  if (ss.fail())
    return std::nullopt;
#endif

  return value;
}

/// Explicit override for booleans
template<>
inline std::optional<bool> FromChars(const std::string_view& str, int base)
{
  if (Strncasecmp("true", str.data(), str.length()) == 0 || Strncasecmp("yes", str.data(), str.length()) == 0 ||
      Strncasecmp("on", str.data(), str.length()) == 0 || Strncasecmp("1", str.data(), str.length()) == 0)
  {
    return true;
  }

  if (Strncasecmp("false", str.data(), str.length()) == 0 || Strncasecmp("no", str.data(), str.length()) == 0 ||
      Strncasecmp("off", str.data(), str.length()) == 0 || Strncasecmp("0", str.data(), str.length()) == 0)
  {
    return false;
  }

  return std::nullopt;
}

/// Encode/decode hexadecimal byte buffers
std::optional<std::vector<u8>> DecodeHex(const std::string_view& str);

/// starts_with from C++20
ALWAYS_INLINE static bool StartsWith(const std::string_view& str, const char* prefix)
{
  return (str.compare(0, std::strlen(prefix), prefix) == 0);
}
ALWAYS_INLINE static bool EndsWith(const std::string_view& str, const char* suffix)
{
  const std::size_t suffix_length = std::strlen(suffix);
  return (str.length() >= suffix_length && str.compare(str.length() - suffix_length, suffix_length, suffix) == 0);
}

} // namespace StringUtil
