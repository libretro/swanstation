#pragma once
#include "types.h"
#include "string.h"

#if defined(_WIN32)
#include "windows_headers.h"
#else
#include <sys/time.h>
#endif

class Timestamp
{
public:
  using UnixTimestampValue = u64;
  struct ExpandedTime
  {
    u32 Year;         // 0-...
    u32 Month;        // 1-12
    u32 DayOfMonth;   // 1-31
    u32 DayOfWeek;    // 0-6, starting at Sunday
    u32 Hour;         // 0-23
    u32 Minute;       // 0-59
    u32 Second;       // 0-59
    u32 Milliseconds; // 0-999
  };

public:
  Timestamp();
  Timestamp(const Timestamp& copy);

  // setters
  void SetUnixTimestamp(UnixTimestampValue value);

// windows-specific
#ifdef _WIN32
  void SetWindowsFileTime(const FILETIME* pFileTime);
#endif

  // operators
  bool operator==(const Timestamp& other) const;
  bool operator!=(const Timestamp& other) const;
  bool operator<(const Timestamp& other) const;
  bool operator<=(const Timestamp& other) const;
  bool operator>(const Timestamp& other) const;
  bool operator>=(const Timestamp& other) const;
  Timestamp& operator=(const Timestamp& other);

private:
#if defined(_WIN32)
  SYSTEMTIME m_value;
#else
  struct timeval m_value;
#endif
};
