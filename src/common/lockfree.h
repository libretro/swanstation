#pragma once
#ifdef _WIN32
#include "windows_headers.h"
#endif
#include <retro_atomic.h>
#include <retro_miscellaneous.h>
#include <cstdint>
#include <cstring>

#if defined(RETRO_ATOMIC_BACKEND_CXX11) || !defined(RETRO_ATOMIC_LOCK_FREE) || !defined(RETRO_ATOMIC_HAS_PTR) || \
  !defined(RETRO_ATOMIC_HAS_CAS)
#error "swanstation needs a lock-free retro_atomic backend other than std::atomic (RETRO_ATOMIC_FORCE_*)"
#endif

/* A handle published once and read lock-free: a pointer, or a 64-bit
 * Vulkan non-dispatchable handle on a 32-bit host. */
template<typename T, bool PtrSized = (sizeof(T) == sizeof(void*))>
class AtomicSlot;

template<typename T>
class AtomicSlot<T, true>
{
public:
  AtomicSlot() { retro_atomic_ptr_init(&m_value, nullptr); }
  AtomicSlot(const AtomicSlot&) = delete;
  AtomicSlot& operator=(const AtomicSlot&) = delete;

  T Load() const { return FromRaw(retro_atomic_load_acquire_ptr(const_cast<retro_atomic_ptr_t*>(&m_value))); }
  void Store(T v) { retro_atomic_store_release_ptr(&m_value, ToRaw(v)); }
  T Exchange(T v) { return FromRaw(retro_atomic_exchange_ptr(&m_value, ToRaw(v))); }
  bool CompareExchange(T expected, T desired)
  {
    return retro_atomic_cas_ptr(&m_value, ToRaw(expected), ToRaw(desired)) != 0;
  }

private:
  static void* ToRaw(T v)
  {
    void* r;
    std::memcpy(&r, &v, sizeof(r));
    return r;
  }
  static T FromRaw(void* r)
  {
    T v;
    std::memcpy(&v, &r, sizeof(v));
    return v;
  }

  retro_atomic_ptr_t m_value;
};

#if defined(RETRO_ATOMIC_HAS_64)
template<typename T>
class AtomicSlot<T, false>
{
  static_assert(sizeof(T) == sizeof(int64_t), "AtomicSlot holds pointer- or 64-bit-sized handles");

public:
  AtomicSlot() { retro_atomic_64_init(&m_value, 0); }
  AtomicSlot(const AtomicSlot&) = delete;
  AtomicSlot& operator=(const AtomicSlot&) = delete;

  T Load() const { return FromRaw(retro_atomic_load_acquire_64(const_cast<retro_atomic_64_t*>(&m_value))); }
  void Store(T v) { retro_atomic_store_release_64(&m_value, ToRaw(v)); }
  T Exchange(T v) { return FromRaw(retro_atomic_exchange_64(&m_value, ToRaw(v))); }
  bool CompareExchange(T expected, T desired)
  {
    return retro_atomic_cas_64(&m_value, ToRaw(expected), ToRaw(desired)) != 0;
  }

private:
  static int64_t ToRaw(T v)
  {
    int64_t r;
    std::memcpy(&r, &v, sizeof(r));
    return r;
  }
  static T FromRaw(int64_t r)
  {
    T v;
    std::memcpy(&v, &r, sizeof(v));
    return v;
  }

  alignas(8) retro_atomic_64_t m_value;
};
#else
template<typename T>
class AtomicSlot<T, false>
{
  static_assert(sizeof(T) == 0, "AtomicSlot of a non-pointer-sized handle needs RETRO_ATOMIC_HAS_64");
};
#endif

/* Sequence counters wrap rather than overflow. */
static inline int NextSequence(int seq, unsigned step = 1)
{
  return static_cast<int>(static_cast<unsigned>(seq) + step);
}

/* Fixed-capacity registry of pointer pairs. Readers walk it without
 * locks, from any thread or a signal handler; each slot is a seqlock.
 * Static storage only: zero-initialisation is the empty state, so it is
 * usable before any constructor has run. */
template<unsigned N>
class AtomicPairTable
{
public:
  /* a must be non-null; false when full. */
  bool Insert(void* a, void* b)
  {
    for (Slot& s : m_slots)
    {
      int seq;
      if (!Claim(s, &seq))
        continue;
      const bool free_slot = (retro_atomic_load_relaxed_ptr(&s.a) == nullptr);
      if (free_slot)
      {
        retro_atomic_store_relaxed_ptr(&s.a, a);
        retro_atomic_store_relaxed_ptr(&s.b, b);
      }
      retro_atomic_store_release_int(&s.seq, NextSequence(seq, 2));
      if (free_slot)
        return true;
    }
    return false;
  }

  /* Clears the first slot pred(a, b) accepts. */
  template<typename Pred>
  bool Remove(const Pred& pred)
  {
    for (Slot& s : m_slots)
    {
      /* Another writer holds the slot for a few stores at most. */
      int seq;
      while (!Claim(s, &seq))
        retro_cpu_relax();
      void* a = retro_atomic_load_relaxed_ptr(&s.a);
      const bool hit = (a != nullptr && pred(a, retro_atomic_load_relaxed_ptr(&s.b)));
      if (hit)
      {
        retro_atomic_store_relaxed_ptr(&s.a, nullptr);
        retro_atomic_store_relaxed_ptr(&s.b, nullptr);
      }
      retro_atomic_store_release_int(&s.seq, NextSequence(seq, 2));
      if (hit)
        return true;
    }
    return false;
  }

  /* Calls fn(a, b) for each live pair until it returns true. */
  template<typename Fn>
  bool ForEach(const Fn& fn) const
  {
    for (const Slot& cs : m_slots)
    {
      Slot& s = const_cast<Slot&>(cs);
      const int seq = retro_atomic_load_acquire_int(&s.seq);
      if (seq & 1)
        continue;
      void* a = retro_atomic_load_relaxed_ptr(&s.a);
      void* b = retro_atomic_load_relaxed_ptr(&s.b);
      retro_atomic_thread_fence_acquire();
      if (!a || retro_atomic_load_relaxed_int(&s.seq) != seq)
        continue;
      if (fn(a, b))
        return true;
    }
    return false;
  }

private:
  struct Slot
  {
    retro_atomic_int_t seq;
    retro_atomic_ptr_t a;
    retro_atomic_ptr_t b;
  };

  static bool Claim(Slot& s, int* seq)
  {
    const int cur = retro_atomic_load_acquire_int(&s.seq);
    if ((cur & 1) || !retro_atomic_cas_int(&s.seq, cur, NextSequence(cur)))
      return false;
    retro_atomic_thread_fence_release();
    *seq = cur;
    return true;
  }

  Slot m_slots[N];
};

/* COM object in an AtomicSlot; the slot owns one reference. Returns the
 * published object: fresh, or whichever another thread got in first
 * (fresh then keeps, and later drops, its own reference). */
template<typename T, typename ComPtrT>
T* PublishComSlot(AtomicSlot<T*>& slot, ComPtrT& fresh)
{
  T* const p = fresh.Get();
  if (slot.CompareExchange(nullptr, p))
  {
    fresh.Detach();
    return p;
  }
  return slot.Load();
}

template<typename T>
void ReleaseComSlot(AtomicSlot<T*>& slot)
{
  if (T* const p = slot.Exchange(nullptr))
    p->Release();
}
