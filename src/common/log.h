#pragma once
#include "types.h"
#include <cinttypes>
#include <mutex>

enum LOGLEVEL
{
  LOGLEVEL_NONE = 0,    // Silences all log traffic
  LOGLEVEL_ERROR = 1,   // "ErrorPrint"
  LOGLEVEL_WARNING = 2, // "WarningPrint"
  LOGLEVEL_PERF = 3,    // "PerfPrint"
  LOGLEVEL_INFO = 4,    // "InfoPrint"
  LOGLEVEL_VERBOSE = 5, // "VerbosePrint"
  LOGLEVEL_DEV = 6,     // "DevPrint"
  LOGLEVEL_PROFILE = 7, // "ProfilePrint"
  LOGLEVEL_DEBUG = 8,   // "DebugPrint"
  LOGLEVEL_TRACE = 9,   // "TracePrint"
  LOGLEVEL_COUNT = 10
};

namespace Log {
// log message callback type
using CallbackFunctionType = void (*)(void* pUserParam, const char* channelName, const char* functionName,
                                      LOGLEVEL level, const char* message);

// registers a log callback
void RegisterCallback(CallbackFunctionType callbackFunction, void* pUserParam);

// unregisters a log callback
void UnregisterCallback(CallbackFunctionType callbackFunction, void* pUserParam);

// adds a standard console output
void SetConsoleOutputParams(bool enabled, const char* channelFilter = nullptr, LOGLEVEL levelFilter = LOGLEVEL_TRACE);

// Sets global filtering level, messages below this level won't be sent to any of the logging sinks.
void SetFilterLevel(LOGLEVEL level);

// writes a message to the log
void Write(const char* channelName, const char* functionName, LOGLEVEL level, const char* message);
void Writef(const char* channelName, const char* functionName, LOGLEVEL level, const char* format, ...) printflike(4, 5);
void Writev(const char* channelName, const char* functionName, LOGLEVEL level, const char* format, va_list ap);
} // namespace Log

// log wrappers
#define Log_SetChannel(ChannelName) static const char* ___LogChannel___ = #ChannelName;
#define Log_ErrorPrint(msg) Log::Write(___LogChannel___, __func__, LOGLEVEL_ERROR, msg)
#define Log_ErrorPrintf(...) Log::Writef(___LogChannel___, __func__, LOGLEVEL_ERROR, __VA_ARGS__)
#define Log_WarningPrint(msg) Log::Write(___LogChannel___, __func__, LOGLEVEL_WARNING, msg)
#define Log_WarningPrintf(...) Log::Writef(___LogChannel___, __func__, LOGLEVEL_WARNING, __VA_ARGS__)
#define Log_InfoPrint(msg) Log::Write(___LogChannel___, __func__, LOGLEVEL_INFO, msg)
#define Log_InfoPrintf(...) Log::Writef(___LogChannel___, __func__, LOGLEVEL_INFO, __VA_ARGS__)
