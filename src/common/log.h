#pragma once
#include "types.h"
#include <cinttypes>
#include <cstdarg>
#include <mutex>

enum class LogLevel : u8
{
  None = 0,    // Silences all log traffic
  Error = 1,   // "ErrorPrint"
  Warning = 2, // "WarningPrint"
  Perf = 3,    // "PerfPrint"
  Info = 4,    // "InfoPrint"
  Verbose = 5, // "VerbosePrint"
  Dev = 6,     // "DevPrint"
  Profile = 7, // "ProfilePrint"
  Debug = 8,   // "DebugPrint"
  Trace = 9,   // "TracePrint"
  Count = 10
};

namespace Log {
// log message callback type
using CallbackFunctionType = void (*)(void* pUserParam, const char* channelName, const char* functionName,
                                      LogLevel level, const char* message);

// registers a log callback
void RegisterCallback(CallbackFunctionType callbackFunction, void* pUserParam);

// unregisters a log callback
void UnregisterCallback(CallbackFunctionType callbackFunction, void* pUserParam);

// adds a standard console output
void SetConsoleOutputParams(bool enabled, const char* channelFilter = nullptr, LogLevel levelFilter = LogLevel::Trace);

// Sets global filtering level, messages below this level won't be sent to any of the logging sinks.
void SetFilterLevel(LogLevel level);

// writes a message to the log
void Write(const char* channelName, const char* functionName, LogLevel level, const char* message);
void Writef(const char* channelName, const char* functionName, LogLevel level, const char* format, ...)
  printflike(4, 5);
void Writev(const char* channelName, const char* functionName, LogLevel level, const char* format, std::va_list ap);
} // namespace Log

// log wrappers
#define Log_SetChannel(ChannelName) static const char* ___LogChannel___ = #ChannelName;
#define Log_ErrorPrint(msg) Log::Write(___LogChannel___, __func__, LogLevel::Error, msg)
#define Log_ErrorPrintf(...) Log::Writef(___LogChannel___, __func__, LogLevel::Error, __VA_ARGS__)
#define Log_WarningPrint(msg) Log::Write(___LogChannel___, __func__, LogLevel::Warning, msg)
#define Log_WarningPrintf(...) Log::Writef(___LogChannel___, __func__, LogLevel::Warning, __VA_ARGS__)
#define Log_InfoPrint(msg) Log::Write(___LogChannel___, __func__, LogLevel::Info, msg)
#define Log_InfoPrintf(...) Log::Writef(___LogChannel___, __func__, LogLevel::Info, __VA_ARGS__)
