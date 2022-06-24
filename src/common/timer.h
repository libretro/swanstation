#pragma once
#include <cstdint>

namespace Common {

class Timer
{
public:
  using Value = std::uint64_t;

  Timer();

  static Value GetValue(void);
  static double ConvertValueToSeconds(Value value);
  static double ConvertValueToMilliseconds(Value value);
  static double ConvertValueToNanoseconds(Value value);
  static Value ConvertSecondsToValue(double s);

  void Reset(void);

  double GetTimeSeconds(void) const;
  double GetTimeMilliseconds(void) const;

private:
  Value m_tvStartValue;
};

} // namespace Common
