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
  static Value ConvertMillisecondsToValue(double s);
  static Value ConvertNanosecondsToValue(double ns);
  static void SleepUntil(Value value, bool exact);

  void Reset(void);

  double GetTimeSeconds(void) const;
  double GetTimeMilliseconds(void) const;

private:
  Value m_tvStartValue;
};

} // namespace Common
