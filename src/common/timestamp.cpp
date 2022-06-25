#include "timestamp.h"
#include <cstring>
#include <ctime>
#include <tuple>

#if defined(_WIN32)

// http://support.microsoft.com/kb/167296
static void UnixTimeToFileTime(time_t t, LPFILETIME pft)
{
  LONGLONG ll;
  ll = Int32x32To64(t, 10000000ULL) + 116444736000000000ULL;
  pft->dwLowDateTime = (DWORD)ll;
  pft->dwHighDateTime = ll >> 32;
}

static void UnixTimeToSystemTime(time_t t, LPSYSTEMTIME pst)
{
  FILETIME ft;
  UnixTimeToFileTime(t, &ft);
  FileTimeToSystemTime(&ft, pst);
}

static time_t FileTimeToUnixTime(const FILETIME* pft)
{
  LONGLONG ll = ((LONGLONG)pft->dwHighDateTime) << 32 | (LONGLONG)pft->dwLowDateTime;
  ll -= 116444736000000000ULL;
  ll /= 10000000ULL;
  return (time_t)ll;
}

static time_t SystemTimeToUnixTime(const SYSTEMTIME* pst)
{
  FILETIME ft;
  SystemTimeToFileTime(pst, &ft);
  return FileTimeToUnixTime(&ft);
}
#endif

Timestamp::Timestamp()
{
#if defined(_WIN32)
  m_value.wYear = 1970;
  m_value.wMonth = 1;
  m_value.wDayOfWeek = 0;
  m_value.wDay = 1;
  m_value.wHour = 0;
  m_value.wMinute = 0;
  m_value.wSecond = 0;
  m_value.wMilliseconds = 0;
#else
  m_value.tv_sec = 0;
  m_value.tv_usec = 0;
#endif
}

Timestamp::Timestamp(const Timestamp& copy)
{
  std::memcpy(&m_value, &copy.m_value, sizeof(m_value));
}

void Timestamp::SetUnixTimestamp(UnixTimestampValue value)
{
#if defined(_WIN32)
  UnixTimeToSystemTime((time_t)value, &m_value);
#else
  m_value.tv_sec = (time_t)value;
  m_value.tv_usec = 0;
#endif
}

String Timestamp::ToString(const char* format) const
{
  SmallString destination;
  ToString(destination, format);
  return String(destination);
}

void Timestamp::ToString(String& destination, const char* format) const
{
  tm localTime;
  char buffer[256];
#if defined(_WIN32)
  time_t unixTime = (time_t)(UnixTimestampValue)SystemTimeToUnixTime(&m_value);
  localtime_s(&localTime, &unixTime);
#else
  time_t unixTime = (time_t)(UnixTimestampValue)m_value.tv_sec;
  localtime_r(&unixTime, &localTime);
#endif
  strftime(buffer, countof(buffer) - 1, format, &localTime);
  buffer[countof(buffer) - 1] = 0;

  destination.Clear();
  destination.AppendString(buffer);
}

bool Timestamp::operator==(const Timestamp& other) const
{
  return std::memcmp(&m_value, &other.m_value, sizeof(m_value)) == 0;
}

bool Timestamp::operator!=(const Timestamp& other) const
{
  return !operator==(other);
}

bool Timestamp::operator<(const Timestamp& other) const
{
#if defined(_WIN32)
  return std::tie(m_value.wYear, m_value.wMonth, m_value.wDay, m_value.wHour, m_value.wMinute, m_value.wSecond,
                  m_value.wMilliseconds) < std::tie(other.m_value.wYear, other.m_value.wMonth, other.m_value.wDay,
                                                    other.m_value.wHour, other.m_value.wMinute, other.m_value.wSecond,
                                                    other.m_value.wMilliseconds);
#else
  return std::tie(m_value.tv_sec, m_value.tv_usec) < std::tie(other.m_value.tv_sec, other.m_value.tv_usec);
#endif
}

bool Timestamp::operator<=(const Timestamp& other) const
{
#if defined(_WIN32)
  return std::tie(m_value.wYear, m_value.wMonth, m_value.wDay, m_value.wHour, m_value.wMinute, m_value.wSecond,
                  m_value.wMilliseconds) <= std::tie(other.m_value.wYear, other.m_value.wMonth, other.m_value.wDay,
                                                     other.m_value.wHour, other.m_value.wMinute, other.m_value.wSecond,
                                                     other.m_value.wMilliseconds);
#else
  return std::tie(m_value.tv_sec, m_value.tv_usec) <= std::tie(other.m_value.tv_sec, other.m_value.tv_usec);
#endif
}

bool Timestamp::operator>(const Timestamp& other) const
{
#if defined(_WIN32)
  return std::tie(m_value.wYear, m_value.wMonth, m_value.wDay, m_value.wHour, m_value.wMinute, m_value.wSecond,
                  m_value.wMilliseconds) > std::tie(other.m_value.wYear, other.m_value.wMonth, other.m_value.wDay,
                                                    other.m_value.wHour, other.m_value.wMinute, other.m_value.wSecond,
                                                    other.m_value.wMilliseconds);
#else
  return std::tie(m_value.tv_sec, m_value.tv_usec) > std::tie(other.m_value.tv_sec, other.m_value.tv_usec);
#endif
}

bool Timestamp::operator>=(const Timestamp& other) const
{
#if defined(_WIN32)
  return std::tie(m_value.wYear, m_value.wMonth, m_value.wDay, m_value.wHour, m_value.wMinute, m_value.wSecond,
                  m_value.wMilliseconds) >= std::tie(other.m_value.wYear, other.m_value.wMonth, other.m_value.wDay,
                                                     other.m_value.wHour, other.m_value.wMinute, other.m_value.wSecond,
                                                     other.m_value.wMilliseconds);
#else
  return std::tie(m_value.tv_sec, m_value.tv_usec) >= std::tie(other.m_value.tv_sec, other.m_value.tv_usec);
#endif
}

Timestamp& Timestamp::operator=(const Timestamp& other)
{
  std::memcpy(&m_value, &other.m_value, sizeof(m_value));
  return *this;
}

#if defined(_WIN32)
void Timestamp::SetWindowsFileTime(const FILETIME* pFileTime)
{
  FileTimeToSystemTime(pFileTime, &m_value);
}
#endif
