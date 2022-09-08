#include "timers.h"
#include "common/state_wrapper.h"
#include "gpu.h"
#include "interrupt_controller.h"
#include "system.h"

Timers g_timers;

Timers::Timers() = default;

Timers::~Timers() = default;

void Timers::Initialize()
{
  m_sysclk_event = TimingEvents::CreateTimingEvent(
    "Timer SysClk Interrupt", 1, 1,
    [](void* param, TickCount ticks, TickCount ticks_late) { static_cast<Timers*>(param)->AddSysClkTicks(ticks); },
    this, false);
  Reset();
}

void Timers::Shutdown()
{
  m_sysclk_event.reset();
}

void Timers::Reset()
{
  for (CounterState& cs : m_states)
  {
    cs.mode.bits = 0;
    cs.mode.interrupt_request_n = true;
    cs.counter = 0;
    cs.target = 0;
    cs.gate = false;
    cs.external_counting_enabled = false;
    cs.counting_enabled = true;
    cs.irq_done = false;
  }

  m_syclk_ticks_carry = 0;
  m_sysclk_div_8_carry = 0;
  UpdateSysClkEvent();
}

bool Timers::DoState(StateWrapper& sw)
{
  for (CounterState& cs : m_states)
  {
    sw.Do(&cs.mode.bits);
    sw.Do(&cs.counter);
    sw.Do(&cs.target);
    sw.Do(&cs.gate);
    sw.Do(&cs.use_external_clock);
    sw.Do(&cs.external_counting_enabled);
    sw.Do(&cs.counting_enabled);
    sw.Do(&cs.irq_done);
  }

  sw.Do(&m_syclk_ticks_carry);
  sw.Do(&m_sysclk_div_8_carry);

  if (sw.IsReading())
    UpdateSysClkEvent();

  return !sw.HasError();
}

void Timers::CPUClocksChanged()
{
  m_syclk_ticks_carry = 0;
}

void Timers::SetGate(u32 timer, bool state)
{
  CounterState& cs = m_states[timer];
  if (cs.gate == state)
    return;

  cs.gate = state;

  if (!cs.mode.sync_enable)
    return;

  if (cs.counting_enabled && !cs.use_external_clock)
    m_sysclk_event->InvokeEarly();

  if (state)
  {
    switch (cs.mode.sync_mode)
    {
      case SyncMode::ResetOnGate:
      case SyncMode::ResetAndRunOnGate:
        cs.counter = 0;
        break;

      case SyncMode::FreeRunOnGate:
        cs.mode.sync_enable = false;
        break;
    }
  }

  UpdateCountingEnabled(cs);
  UpdateSysClkEvent();
}

TickCount Timers::GetTicksUntilIRQ(u32 timer) const
{
  const CounterState& cs = m_states[timer];
  if (!cs.counting_enabled)
    return std::numeric_limits<TickCount>::max();

  TickCount ticks_until_irq = std::numeric_limits<TickCount>::max();
  if (cs.mode.irq_at_target && cs.counter < cs.target)
    ticks_until_irq = static_cast<TickCount>(cs.target - cs.counter);
  if (cs.mode.irq_on_overflow)
    ticks_until_irq = std::min(ticks_until_irq, static_cast<TickCount>(0xFFFFu - cs.counter));

  return ticks_until_irq;
}

void Timers::AddTicks(u32 timer, TickCount count)
{
  CounterState& cs = m_states[timer];
  const u32 old_counter = cs.counter;
  cs.counter += static_cast<u32>(count);
  CheckForIRQ(timer, old_counter);
}

void Timers::CheckForIRQ(u32 timer, u32 old_counter)
{
  CounterState& cs = m_states[timer];

  bool interrupt_request = false;
  if (cs.counter >= cs.target && (old_counter < cs.target || cs.target == 0))
  {
    interrupt_request |= cs.mode.irq_at_target;
    cs.mode.reached_target = true;

    if (cs.mode.reset_at_target && cs.target > 0)
      cs.counter %= cs.target;
  }
  if (cs.counter >= 0xFFFF)
  {
    interrupt_request |= cs.mode.irq_on_overflow;
    cs.mode.reached_overflow = true;
    cs.counter %= 0xFFFFu;
  }

  if (interrupt_request)
  {
    if (!cs.mode.irq_pulse_n)
    {
      // this is actually low for a few cycles
      cs.mode.interrupt_request_n = false;
      UpdateIRQ(timer);
      cs.mode.interrupt_request_n = true;
    }
    else
    {
      cs.mode.interrupt_request_n ^= true;
      UpdateIRQ(timer);
    }
  }
}

static ALWAYS_INLINE_RELEASE TickCount UnscaleTicksToOverclock(TickCount ticks, TickCount* remainder)
{
  const u64 num =
    (static_cast<u32>(ticks) * static_cast<u64>(g_settings.cpu_overclock_denominator)) + static_cast<u32>(*remainder);
  const TickCount t = static_cast<u32>(num / g_settings.cpu_overclock_numerator);
  *remainder = static_cast<u32>(num % g_settings.cpu_overclock_numerator);
  return t;
}

void Timers::AddSysClkTicks(TickCount sysclk_ticks)
{
  sysclk_ticks = g_settings.cpu_overclock_active ? UnscaleTicksToOverclock(sysclk_ticks, &m_syclk_ticks_carry) : sysclk_ticks;

  if (!m_states[0].external_counting_enabled && m_states[0].counting_enabled)
    AddTicks(0, sysclk_ticks);
  if (!m_states[1].external_counting_enabled && m_states[1].counting_enabled)
    AddTicks(1, sysclk_ticks);
  if (m_states[2].external_counting_enabled)
  {
    TickCount sysclk_div_8_ticks = (sysclk_ticks + m_sysclk_div_8_carry) / 8;
    m_sysclk_div_8_carry = (sysclk_ticks + m_sysclk_div_8_carry) % 8;
    AddTicks(2, sysclk_div_8_ticks);
  }
  else if (m_states[2].counting_enabled)
  {
    AddTicks(2, sysclk_ticks);
  }

  UpdateSysClkEvent();
}

u32 Timers::ReadRegister(u32 offset)
{
  const u32 timer_index = (offset >> 4) & u32(0x03);
  const u32 port_offset = offset & u32(0x0F);
  if (timer_index >= 3)
    return UINT32_C(0xFFFFFFFF);

  CounterState& cs = m_states[timer_index];

  switch (port_offset)
  {
    case 0x00:
    {
      if (timer_index < 2 && cs.external_counting_enabled)
      {
        // timers 0/1 depend on the GPU
        if (timer_index == 0 || g_gpu->IsCRTCScanlinePending())
          g_gpu->SynchronizeCRTC();
      }

      m_sysclk_event->InvokeEarly();

      return cs.counter;
    }

    case 0x04:
    {
      if (timer_index < 2 && cs.external_counting_enabled)
      {
        // timers 0/1 depend on the GPU
        if (timer_index == 0 || g_gpu->IsCRTCScanlinePending())
          g_gpu->SynchronizeCRTC();
      }

      m_sysclk_event->InvokeEarly();

      const u32 bits = cs.mode.bits;
      cs.mode.reached_overflow = false;
      cs.mode.reached_target = false;
      return bits;
    }

    case 0x08:
      return cs.target;

    default:
      break;
  }
  return UINT32_C(0xFFFFFFFF);
}

void Timers::WriteRegister(u32 offset, u32 value)
{
  const u32 timer_index = (offset >> 4) & u32(0x03);
  const u32 port_offset = offset & u32(0x0F);
  if (timer_index >= 3)
    return;

  CounterState& cs = m_states[timer_index];

  if (timer_index < 2 && cs.external_counting_enabled)
  {
    // timers 0/1 depend on the GPU
    if (timer_index == 0 || g_gpu->IsCRTCScanlinePending())
      g_gpu->SynchronizeCRTC();
  }

  m_sysclk_event->InvokeEarly();

  // Strictly speaking these IRQ checks should probably happen on the next tick.
  switch (port_offset)
  {
    case 0x00:
    {
      const u32 old_counter = cs.counter;
      cs.counter = value & u32(0xFFFF);
      CheckForIRQ(timer_index, old_counter);
      if (timer_index == 2 || !cs.external_counting_enabled)
        UpdateSysClkEvent();
    }
    break;

    case 0x04:
    {
      static constexpr u32 WRITE_MASK = 0b1110001111111111;

      cs.mode.bits = (value & WRITE_MASK) | (cs.mode.bits & ~WRITE_MASK);
      cs.use_external_clock = (cs.mode.clock_source & (timer_index == 2 ? 2 : 1)) != 0;
      cs.counter = 0;
      cs.irq_done = false;

      UpdateCountingEnabled(cs);
      CheckForIRQ(timer_index, cs.counter);
      UpdateIRQ(timer_index);
      UpdateSysClkEvent();
    }
    break;

    case 0x08:
    {
      cs.target = value & u32(0xFFFF);
      CheckForIRQ(timer_index, cs.counter);
      if (timer_index == 2 || !cs.external_counting_enabled)
        UpdateSysClkEvent();
    }
    break;

    default:
      break;
  }
}

void Timers::UpdateCountingEnabled(CounterState& cs)
{
  if (cs.mode.sync_enable)
  {
    switch (cs.mode.sync_mode)
    {
      case SyncMode::PauseOnGate:
        cs.counting_enabled = !cs.gate;
        break;

      case SyncMode::ResetOnGate:
        cs.counting_enabled = true;
        break;

      case SyncMode::ResetAndRunOnGate:
      case SyncMode::FreeRunOnGate:
        cs.counting_enabled = cs.gate;
        break;
    }
  }
  else
    cs.counting_enabled = true;

  cs.external_counting_enabled = cs.use_external_clock && cs.counting_enabled;
}

void Timers::UpdateIRQ(u32 index)
{
  CounterState& cs = m_states[index];
  if (cs.mode.interrupt_request_n || (!cs.mode.irq_repeat && cs.irq_done))
    return;

  cs.irq_done = true;
  g_interrupt_controller.InterruptRequest(
    static_cast<InterruptController::IRQ>(static_cast<u32>(InterruptController::IRQ::TMR0) + index));
}

TickCount Timers::GetTicksUntilNextInterrupt() const
{
  TickCount min_ticks = System::GetMaxSliceTicks();
  for (u32 i = 0; i < NUM_TIMERS; i++)
  {
    const CounterState& cs = m_states[i];
    if (!cs.counting_enabled || (i < 2 && cs.external_counting_enabled) ||
        (!cs.mode.irq_at_target && !cs.mode.irq_on_overflow && (cs.mode.irq_repeat || !cs.irq_done)))
    {
      continue;
    }

    if (cs.mode.irq_at_target)
    {
      TickCount ticks = (cs.counter <= cs.target) ? static_cast<TickCount>(cs.target - cs.counter) :
                                                    static_cast<TickCount>((0xFFFFu - cs.counter) + cs.target);
      if (cs.external_counting_enabled) // sysclk/8 for timer 2
        ticks *= 8;

      min_ticks = std::min(min_ticks, ticks);
    }
    if (cs.mode.irq_on_overflow)
    {
      TickCount ticks = static_cast<TickCount>(0xFFFFu - cs.counter);
      if (cs.external_counting_enabled) // sysclk/8 for timer 2
        ticks *= 8;

      min_ticks = std::min(min_ticks, ticks);
    }
  }

  return System::ScaleTicksToOverclock(std::max<TickCount>(1, min_ticks));
}

void Timers::UpdateSysClkEvent()
{
  m_sysclk_event->Schedule(GetTicksUntilNextInterrupt());
}
