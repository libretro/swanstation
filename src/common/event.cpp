#include "event.h"

#if defined(_WIN32)
#include "windows_headers.h"
#include <malloc.h>
#elif defined(__linux__) || defined(__APPLE__) || defined(__HAIKU__)
#include <errno.h>
#include <time.h>
#endif

namespace Common {

#if defined(_WIN32) && defined(USE_WIN32_EVENT_OBJECTS)

Event::Event(bool auto_reset /* = false */)
{
  m_event_handle = reinterpret_cast<void*>(CreateEvent(nullptr, auto_reset ? FALSE : TRUE, FALSE, nullptr));
}

Event::~Event()
{
  CloseHandle(reinterpret_cast<HANDLE>(m_event_handle));
}

void Event::Signal()
{
  SetEvent(reinterpret_cast<HANDLE>(m_event_handle));
}

void Event::Wait()
{
  WaitForSingleObject(reinterpret_cast<HANDLE>(m_event_handle), INFINITE);
}

void Event::Reset()
{
  ResetEvent(reinterpret_cast<HANDLE>(m_event_handle));
}

#elif defined(_WIN32)

Event::Event(bool auto_reset /* = false */) : m_auto_reset(auto_reset)
{
  InitializeCriticalSection(&m_cs);
  InitializeConditionVariable(&m_cv);
}

Event::~Event()
{
  DeleteCriticalSection(&m_cs);
}

void Event::Signal()
{
  EnterCriticalSection(&m_cs);
  m_signaled.store(true);
  WakeAllConditionVariable(&m_cv);
  LeaveCriticalSection(&m_cs);
}

void Event::Wait()
{
  m_waiters.fetch_add(1);

  EnterCriticalSection(&m_cs);
  while (!m_signaled.load())
    SleepConditionVariableCS(&m_cv, &m_cs, INFINITE);

  if (m_waiters.fetch_sub(1) == 1 && m_auto_reset)
    m_signaled.store(false);

  LeaveCriticalSection(&m_cs);
}

void Event::Reset()
{
  EnterCriticalSection(&m_cs);
  m_signaled.store(false);
  LeaveCriticalSection(&m_cs);
}

#elif defined(__linux__) || defined(__APPLE__) || defined(__HAIKU__)

Event::Event(bool auto_reset /* = false */) : m_auto_reset(auto_reset)
{
  pthread_mutex_init(&m_mutex, nullptr);
  pthread_cond_init(&m_cv, nullptr);
}

Event::~Event()
{
  pthread_cond_destroy(&m_cv);
  pthread_mutex_destroy(&m_mutex);
}

void Event::Signal()
{
  pthread_mutex_lock(&m_mutex);
  m_signaled.store(true);
  pthread_cond_broadcast(&m_cv);
  pthread_mutex_unlock(&m_mutex);
}

void Event::Wait()
{
  m_waiters.fetch_add(1);

  pthread_mutex_lock(&m_mutex);
  while (!m_signaled.load())
    pthread_cond_wait(&m_cv, &m_mutex);

  if (m_waiters.fetch_sub(1) == 1 && m_auto_reset)
    m_signaled.store(false);

  pthread_mutex_unlock(&m_mutex);
}

void Event::Reset()
{
  pthread_mutex_lock(&m_mutex);
  m_signaled.store(false);
  pthread_mutex_unlock(&m_mutex);
}

#else

Event::Event(bool auto_reset /* = false */) : m_auto_reset(auto_reset) {}

Event::~Event() = default;

void Event::Signal()
{
  std::unique_lock lock(m_mutex);
  m_signaled.store(true);
  m_cv.notify_all();
}

void Event::Wait()
{
  m_waiters.fetch_add(1);

  std::unique_lock lock(m_mutex);
  m_cv.wait(lock, [this]() { return m_signaled.load(); });

  if (m_waiters.fetch_sub(1) == 1 && m_auto_reset)
    m_signaled.store(false);
}

void Event::Reset()
{
  std::unique_lock lock(m_mutex);
  m_signaled.store(false);
}
#endif

} // namespace Common
