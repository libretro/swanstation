#include "timer.h"
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#include "windows_headers.h"
#else
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#endif

namespace Common {

#ifdef _WIN32

static double s_counter_frequency;

Timer::Value Timer::GetValue(void)
{
  static bool s_counter_initialized = false;
  // even if this races, it should still result in the same value..
  if (!s_counter_initialized)
  {
    LARGE_INTEGER Freq;
    QueryPerformanceFrequency(&Freq);
    s_counter_frequency = static_cast<double>(Freq.QuadPart) / 1000000000.0;
    s_counter_initialized = true;
  }

  Timer::Value ReturnValue;
  QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&ReturnValue));
  return ReturnValue;
}

double Timer::ConvertValueToNanoseconds(Timer::Value value)
{
  return (static_cast<double>(value) / s_counter_frequency);
}

double Timer::ConvertValueToMilliseconds(Timer::Value value)
{
  return ((static_cast<double>(value) / s_counter_frequency) / 1000000.0);
}

double Timer::ConvertValueToSeconds(Timer::Value value)
{
  return ((static_cast<double>(value) / s_counter_frequency) / 1000000000.0);
}

Timer::Value Timer::ConvertSecondsToValue(double s)
{
  return static_cast<Value>((s * 1000000000.0) * s_counter_frequency);
}
#else
Timer::Value Timer::GetValue(void)
{
  struct timespec tv;
  clock_gettime(CLOCK_MONOTONIC, &tv);
  return ((Value)tv.tv_nsec + (Value)tv.tv_sec * 1000000000);
}

double Timer::ConvertValueToNanoseconds(Timer::Value value)
{
  return static_cast<double>(value);
}

double Timer::ConvertValueToMilliseconds(Timer::Value value)
{
  return (static_cast<double>(value) / 1000000.0);
}

double Timer::ConvertValueToSeconds(Timer::Value value)
{
  return (static_cast<double>(value) / 1000000000.0);
}

Timer::Value Timer::ConvertSecondsToValue(double s)
{
  return static_cast<Value>(s * 1000000000.0);
}
#endif

Timer::Timer()
{
  Reset();
}

void Timer::Reset()
{
  m_tvStartValue = GetValue();
}

double Timer::GetTimeSeconds() const
{
  return ConvertValueToSeconds(GetValue() - m_tvStartValue);
}

double Timer::GetTimeMilliseconds() const
{
  return ConvertValueToMilliseconds(GetValue() - m_tvStartValue);
}

} // namespace Common
