#include "log.h"
#include "lockfree.h"
#include <cstdio>

namespace Log {

static AtomicPairTable<8> s_callbacks;

static LogLevel s_filter_level = LogLevel::Trace;

void RegisterCallback(CallbackFunctionType callbackFunction, void* pUserParam)
{
  s_callbacks.Insert(reinterpret_cast<void*>(callbackFunction), pUserParam);
}

void UnregisterCallback(CallbackFunctionType callbackFunction, void* pUserParam)
{
  void* const fn = reinterpret_cast<void*>(callbackFunction);
  s_callbacks.Remove([fn, pUserParam](void* a, void* b) { return a == fn && b == pUserParam; });
}

static void ExecuteCallbacks(const char* channelName, const char* functionName, LogLevel level, const char* message)
{
  s_callbacks.ForEach([&](void* fn, void* param) {
    reinterpret_cast<CallbackFunctionType>(fn)(param, channelName, functionName, level, message);
    return false;
  });
}

void SetFilterLevel(LogLevel level)
{
  s_filter_level = level;
}

void Write(const char* channelName, const char* functionName, LogLevel level, const char* message)
{
  if (level > s_filter_level)
    return;

  ExecuteCallbacks(channelName, functionName, level, message);
}

void Writef(const char* channelName, const char* functionName, LogLevel level, const char* format, ...)
{
  if (level > s_filter_level)
    return;

  std::va_list ap;
  va_start(ap, format);
  Writev(channelName, functionName, level, format, ap);
  va_end(ap);
}

void Writev(const char* channelName, const char* functionName, LogLevel level, const char* format, std::va_list ap)
{
  if (level > s_filter_level)
    return;

  std::va_list apCopy;
  va_copy(apCopy, ap);

#ifdef _WIN32
  uint32_t requiredSize = static_cast<uint32_t>(_vscprintf(format, apCopy));
#else
  uint32_t requiredSize = std::vsnprintf(nullptr, 0, format, apCopy);
#endif
  va_end(apCopy);

  if (requiredSize < 256)
  {
    char buffer[256];
    std::vsnprintf(buffer, countof(buffer), format, ap);
    ExecuteCallbacks(channelName, functionName, level, buffer);
  }
  else
  {
    char* buffer = new char[requiredSize + 1];
    std::vsnprintf(buffer, requiredSize + 1, format, ap);
    ExecuteCallbacks(channelName, functionName, level, buffer);
    delete[] buffer;
  }
}

} // namespace Log
