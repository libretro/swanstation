#include "string_util.h"
#include <cctype>
#include <codecvt>
#include <cstdio>
#include <sstream>

#ifdef _WIN32
#include "windows_headers.h"
#endif

namespace StringUtil {

std::string StdStringFromFormat(const char* format, ...)
{
  std::va_list ap;
  va_start(ap, format);
  std::string ret = StdStringFromFormatV(format, ap);
  va_end(ap);
  return ret;
}

std::string StdStringFromFormatV(const char* format, std::va_list ap)
{
  std::va_list ap_copy;
  va_copy(ap_copy, ap);

#ifdef _WIN32
  int len = _vscprintf(format, ap_copy);
#else
  int len = std::vsnprintf(nullptr, 0, format, ap_copy);
#endif
  va_end(ap_copy);

  std::string ret;
  ret.resize(len);
  std::vsnprintf(ret.data(), ret.size() + 1, format, ap);
  return ret;
}

bool WildcardMatch(const char* subject, const char* mask, bool case_sensitive /*= true*/)
{
  if (case_sensitive)
  {
    const char* cp = nullptr;
    const char* mp = nullptr;

    while ((*subject) && (*mask != '*'))
    {
      if ((*mask != '?') && (std::tolower(*mask) != std::tolower(*subject)))
        return false;

      mask++;
      subject++;
    }

    while (*subject)
    {
      if (*mask == '*')
      {
        if (*++mask == 0)
          return true;

        mp = mask;
        cp = subject + 1;
      }
      else
      {
        if ((*mask == '?') || (std::tolower(*mask) == std::tolower(*subject)))
        {
          mask++;
          subject++;
        }
        else
        {
          mask = mp;
          subject = cp++;
        }
      }
    }

    while (*mask == '*')
    {
      mask++;
    }

    return *mask == 0;
  }
  else
  {
    const char* cp = nullptr;
    const char* mp = nullptr;

    while ((*subject) && (*mask != '*'))
    {
      if ((*mask != *subject) && (*mask != '?'))
        return false;

      mask++;
      subject++;
    }

    while (*subject)
    {
      if (*mask == '*')
      {
        if (*++mask == 0)
          return true;

        mp = mask;
        cp = subject + 1;
      }
      else
      {
        if ((*mask == *subject) || (*mask == '?'))
        {
          mask++;
          subject++;
        }
        else
        {
          mask = mp;
          subject = cp++;
        }
      }
    }

    while (*mask == '*')
    {
      mask++;
    }

    return *mask == 0;
  }
}

std::optional<std::vector<u8>> DecodeHex(const std::string_view& in)
{
  std::vector<u8> data;
  data.reserve(in.size() / 2);

  for (size_t i = 0; i < in.size() / 2; i++)
  {
    std::optional<u8> byte = StringUtil::FromChars<u8>(in.substr(i * 2, 2), 16);
    if (byte.has_value())
      data.push_back(*byte);
    else
      return std::nullopt;
  }

  return {data};
}

#ifdef _WIN32
std::string WideStringToUTF8String(const std::wstring_view& str)
{
  std::string ret;
  if (!WideStringToUTF8String(ret, str))
    return {};

  return ret;
}

bool WideStringToUTF8String(std::string& dest, const std::wstring_view& str)
{
  int mblen = WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.length()), nullptr, 0, nullptr, nullptr);
  if (mblen < 0)
    return false;

  dest.resize(mblen);
  if (mblen > 0 && WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.length()), dest.data(), mblen,
                                       nullptr, nullptr) < 0)
  {
    return false;
  }

  return true;
}

#endif

} // namespace StringUtil
