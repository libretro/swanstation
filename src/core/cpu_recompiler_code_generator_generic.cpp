#include "common/log.h"
#include "cpu_core.h"
#include "cpu_core_private.h"
#include "cpu_recompiler_code_generator.h"
#include "settings.h"
Log_SetChannel(Recompiler::CodeGenerator);

namespace CPU::Recompiler {

void CodeGenerator::EmitLoadGuestRegister(HostReg host_reg, Reg guest_reg)
{
  EmitLoadCPUStructField(host_reg, RegSize_32, State::GPRRegisterOffset(static_cast<u32>(guest_reg)));
}

void CodeGenerator::EmitStoreGuestRegister(Reg guest_reg, const Value& value)
{
  EmitStoreCPUStructField(State::GPRRegisterOffset(static_cast<u32>(guest_reg)), value);
}

void CodeGenerator::EmitStoreInterpreterLoadDelay(Reg reg, const Value& value)
{
  EmitStoreCPUStructField(offsetof(State, load_delay_reg), Value::FromConstantU8(static_cast<u8>(reg)));
  EmitStoreCPUStructField(offsetof(State, load_delay_value), value);
  m_load_delay_dirty = true;
}

Value CodeGenerator::EmitLoadGuestMemory(const CodeBlockInstruction& cbi, const Value& address,
                                         const SpeculativeValue& address_spec, RegSize size)
{
  if (address.IsConstant() && !SpeculativeIsCacheIsolated())
  {
    TickCount read_ticks;
    void* ptr = GetDirectReadMemoryPointer(
      static_cast<u32>(address.constant_value),
      (size == RegSize_8) ? MemoryAccessSize::Byte :
                            ((size == RegSize_16) ? MemoryAccessSize::HalfWord : MemoryAccessSize::Word),
      &read_ticks);
    if (ptr)
    {
      Value result = m_register_cache.AllocateScratch(size);

      if (g_settings.IsUsingFastmem() && Bus::IsRAMAddress(static_cast<u32>(address.constant_value)))
      {
        // have to mask away the high bits for mirrors, since we don't map them in fastmem
        EmitLoadGuestRAMFastmem(Value::FromConstantU32(static_cast<u32>(address.constant_value) & Bus::g_ram_mask),
                                size, result);
      }
      else
      {
        EmitLoadGlobal(result.GetHostRegister(), size, ptr);
      }

      m_delayed_cycles_add += read_ticks;
      return result;
    }
  }

  Value result = m_register_cache.AllocateScratch(HostPointerSize);

  const bool use_fastmem =
    (address_spec ? Bus::CanUseFastmemForAddress(*address_spec) : true) && !SpeculativeIsCacheIsolated();

  if (g_settings.IsUsingFastmem() && use_fastmem && g_settings.cpu_fastmem_rewrite)
  {
    EmitLoadGuestMemoryFastmem(cbi, address, size, result);
  }
  else
  {
    AddPendingCycles(true);
    m_register_cache.FlushCallerSavedGuestRegisters(true, true);
    EmitLoadGuestMemorySlowmem(cbi, address, size, result, false);
  }

  // Downcast to ignore upper 56/48/32 bits. This should be a noop.
  if (result.size != size)
  {
    switch (size)
    {
      case RegSize_8:
        ConvertValueSizeInPlace(&result, RegSize_8, false);
        break;

      case RegSize_16:
        ConvertValueSizeInPlace(&result, RegSize_16, false);
        break;

      case RegSize_32:
        ConvertValueSizeInPlace(&result, RegSize_32, false);
        break;

      default:
        break;
    }
  }

  return result;
}

void CodeGenerator::EmitStoreGuestMemory(const CodeBlockInstruction& cbi, const Value& address,
                                         const SpeculativeValue& address_spec, RegSize size, const Value& value)
{
  if (address.IsConstant() && !SpeculativeIsCacheIsolated())
  {
    void* ptr = GetDirectWriteMemoryPointer(
      static_cast<u32>(address.constant_value),
      (size == RegSize_8) ? MemoryAccessSize::Byte :
                            ((size == RegSize_16) ? MemoryAccessSize::HalfWord : MemoryAccessSize::Word));
    if (ptr)
    {
      if (value.size != size)
        EmitStoreGlobal(ptr, value.ViewAsSize(size));
      else
        EmitStoreGlobal(ptr, value);

      return;
    }
  }

  const bool use_fastmem =
    (address_spec ? Bus::CanUseFastmemForAddress(*address_spec) : true) && !SpeculativeIsCacheIsolated();

  if (g_settings.IsUsingFastmem() && use_fastmem && g_settings.cpu_fastmem_rewrite)
  {
    EmitStoreGuestMemoryFastmem(cbi, address, size, value);
  }
  else
  {
    AddPendingCycles(true);
    m_register_cache.FlushCallerSavedGuestRegisters(true, true);
    EmitStoreGuestMemorySlowmem(cbi, address, size, value, false);
  }
}

} // namespace CPU::Recompiler
