#include "bus.h"
#include "cdrom.h"
#include "common/align.h"
#include "common/make_array.h"
#include "common/state_wrapper.h"
#include "cpu_code_cache.h"
#include "cpu_core.h"
#include "cpu_core_private.h"
#include "dma.h"
#include "gpu.h"
#include "host_interface.h"
#include "interrupt_controller.h"
#include "mdec.h"
#include "pad.h"
#include "sio.h"
#include "spu.h"
#include "timers.h"
#include <cstdio>
#include <cstring>
#include <tuple>
#include <utility>
// Disable MSVC unreachable code warnings for code that actually get reached
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4702) // warning C4702: unreachable code
#endif

namespace Bus {

union MEMDELAY
{
  uint32_t bits;

  BitField<uint32_t, uint8_t, 4, 4> access_time; // cycles
  BitField<uint32_t, bool, 8, 1> use_com0_time;
  BitField<uint32_t, bool, 9, 1> use_com1_time;
  BitField<uint32_t, bool, 10, 1> use_com2_time;
  BitField<uint32_t, bool, 11, 1> use_com3_time;
  BitField<uint32_t, bool, 12, 1> data_bus_16bit;
  BitField<uint32_t, uint8_t, 16, 5> memory_window_size;

  static constexpr uint32_t WRITE_MASK = 0b10101111'00011111'11111111'11111111;
};

union COMDELAY
{
  uint32_t bits;

  BitField<uint32_t, uint8_t, 0, 4> com0;
  BitField<uint32_t, uint8_t, 4, 4> com1;
  BitField<uint32_t, uint8_t, 8, 4> com2;
  BitField<uint32_t, uint8_t, 12, 4> com3;
  BitField<uint32_t, uint8_t, 16, 2> comunk;

  static constexpr uint32_t WRITE_MASK = 0b00000000'00000011'11111111'11111111;
};

union MEMCTRL
{
  uint32_t regs[MEMCTRL_REG_COUNT];

  struct
  {
    uint32_t exp1_base;
    uint32_t exp2_base;
    MEMDELAY exp1_delay_size;
    MEMDELAY exp3_delay_size;
    MEMDELAY bios_delay_size;
    MEMDELAY spu_delay_size;
    MEMDELAY cdrom_delay_size;
    MEMDELAY exp2_delay_size;
    COMDELAY common_delay;
  };
};

std::bitset<RAM_8MB_CODE_PAGE_COUNT> m_ram_code_bits{};
static uint32_t m_ram_code_page_count = 0;
uint8_t* g_ram = nullptr; // 2MB RAM
uint32_t g_ram_size = 0;
uint32_t g_ram_mask = 0;
uint8_t g_bios[BIOS_SIZE]{}; // 512K BIOS ROM

static std::array<TickCount, 3> m_exp1_access_time = {};
static std::array<TickCount, 3> m_exp2_access_time = {};
static std::array<TickCount, 3> m_bios_access_time = {};
static std::array<TickCount, 3> m_cdrom_access_time = {};
static std::array<TickCount, 3> m_spu_access_time = {};

static std::vector<uint8_t> m_exp1_rom;

static MEMCTRL m_MEMCTRL = {};
static uint32_t m_ram_size_reg = 0;

static std::string m_tty_line_buffer;

static Common::MemoryArena m_memory_arena;

static CPUFastmemMode m_fastmem_mode = CPUFastmemMode::Disabled;

#ifdef WITH_MMAP_FASTMEM
static uint8_t* m_fastmem_base = nullptr;
static std::vector<Common::MemoryArena::View> m_fastmem_ram_views;
static std::vector<Common::MemoryArena::View> m_fastmem_reserved_views;
#endif

static uint8_t** m_fastmem_lut = nullptr;
static constexpr auto m_fastmem_ram_mirrors =
  make_array(0x00000000u, 0x00200000u, 0x00400000u, 0x00600000u, 0x80000000u, 0x80200000u, 0x80400000u, 0x80600000u,
             0xA0000000u, 0xA0200000u, 0xA0400000u, 0xA0600000u);

static std::tuple<TickCount, TickCount, TickCount> CalculateMemoryTiming(MEMDELAY mem_delay, COMDELAY common_delay);
static void RecalculateMemoryTimings();

static bool AllocateMemory(bool enable_8mb_ram);
static void ReleaseMemory();

static void SetCodePageFastmemProtection(uint32_t page_index, bool writable);

#define FIXUP_HALFWORD_OFFSET(size, offset) ((size >= MemoryAccessSize::HalfWord) ? (offset) : ((offset) & ~1u))
#define FIXUP_HALFWORD_READ_VALUE(size, offset, value)                                                                 \
  ((size >= MemoryAccessSize::HalfWord) ? (value) : ((value) >> (((offset)&uint32_t(1)) * 8u)))
#define FIXUP_HALFWORD_WRITE_VALUE(size, offset, value)                                                                \
  ((size >= MemoryAccessSize::HalfWord) ? (value) : ((value) << (((offset)&uint32_t(1)) * 8u)))

#define FIXUP_WORD_OFFSET(size, offset) ((size == MemoryAccessSize::Word) ? (offset) : ((offset) & ~3u))
#define FIXUP_WORD_READ_VALUE(size, offset, value)                                                                     \
  ((size == MemoryAccessSize::Word) ? (value) : ((value) >> (((offset)&3u) * 8)))
#define FIXUP_WORD_WRITE_VALUE(size, offset, value)                                                                    \
  ((size == MemoryAccessSize::Word) ? (value) : ((value) << (((offset)&3u) * 8)))

bool Initialize()
{
  if (!AllocateMemory(g_settings.enable_8mb_ram))
  {
    g_host_interface->ReportError("Failed to allocate memory");
    return false;
  }

  Reset();
  return true;
}

void Shutdown()
{
  std::free(m_fastmem_lut);
  m_fastmem_lut = nullptr;

#ifdef WITH_MMAP_FASTMEM
  m_fastmem_base = nullptr;
  m_fastmem_ram_views.clear();
#endif

  CPU::g_state.fastmem_base = nullptr;
  m_fastmem_mode = CPUFastmemMode::Disabled;

  ReleaseMemory();
}

void Reset()
{
  std::memset(g_ram, 0, g_ram_size);
  m_MEMCTRL.exp1_base = 0x1F000000;
  m_MEMCTRL.exp2_base = 0x1F802000;
  m_MEMCTRL.exp1_delay_size.bits = 0x0013243F;
  m_MEMCTRL.exp3_delay_size.bits = 0x00003022;
  m_MEMCTRL.bios_delay_size.bits = 0x0013243F;
  m_MEMCTRL.spu_delay_size.bits = 0x200931E1;
  m_MEMCTRL.cdrom_delay_size.bits = 0x00020843;
  m_MEMCTRL.exp2_delay_size.bits = 0x00070777;
  m_MEMCTRL.common_delay.bits = 0x00031125;
  m_ram_size_reg = UINT32_C(0x00000B88);
  m_ram_code_bits = {};
  RecalculateMemoryTimings();
}

bool DoState(StateWrapper& sw)
{
  uint32_t ram_size = g_ram_size;
  sw.DoEx(&ram_size, 52, static_cast<uint32_t>(RAM_2MB_SIZE));
  if (ram_size != g_ram_size)
  {
    const bool using_8mb_ram = (ram_size == RAM_8MB_SIZE);
    ReleaseMemory();
    if (!AllocateMemory(using_8mb_ram))
      return false;

    UpdateFastmemViews(m_fastmem_mode);
    CPU::UpdateFastmemBase();
  }

  auto exp1_access_time = m_exp1_access_time;
  auto exp2_access_time = m_exp2_access_time;
  auto bios_access_time = m_bios_access_time;
  auto cdrom_access_time = m_cdrom_access_time;
  auto spu_access_time = m_spu_access_time;
  sw.Do(&exp1_access_time);
  sw.Do(&exp2_access_time);
  sw.Do(&bios_access_time);
  sw.Do(&cdrom_access_time);
  sw.Do(&spu_access_time);
  if (sw.IsReading())
  {
    // The memory controller fields can produce at most 183 ticks for a word access.
    static constexpr TickCount MAX_MEMORY_ACCESS_TICKS = 183;
    const auto valid_access_times = [](const std::array<TickCount, 3>& access_times) {
      for (const TickCount ticks : access_times)
      {
        if (ticks < 0 || ticks > MAX_MEMORY_ACCESS_TICKS)
          return false;
      }
      return true;
    };
    if (sw.HasError() || !valid_access_times(exp1_access_time) || !valid_access_times(exp2_access_time) ||
        !valid_access_times(bios_access_time) || !valid_access_times(cdrom_access_time) ||
        !valid_access_times(spu_access_time))
    {
      return false;
    }

    m_exp1_access_time = exp1_access_time;
    m_exp2_access_time = exp2_access_time;
    m_bios_access_time = bios_access_time;
    m_cdrom_access_time = cdrom_access_time;
    m_spu_access_time = spu_access_time;
  }
  sw.DoBytes(g_ram, g_ram_size);
  sw.DoBytes(g_bios, BIOS_SIZE);
  sw.DoArray(m_MEMCTRL.regs, countof(m_MEMCTRL.regs));
  sw.Do(&m_ram_size_reg);
  sw.Do(&m_tty_line_buffer);
  return !sw.HasError();
}

void SetExpansionROM(std::vector<uint8_t> data)
{
  m_exp1_rom = std::move(data);
}

void SetBIOS(const uint8_t *image, size_t image_size)
{
  if (image_size == BIOS_SIZE)
    std::memcpy(g_bios, image, BIOS_SIZE);
}

std::tuple<TickCount, TickCount, TickCount> CalculateMemoryTiming(MEMDELAY mem_delay, COMDELAY common_delay)
{
  // from nocash spec
  int32_t first = 0, seq = 0, min = 0;
  if (mem_delay.use_com0_time)
  {
    first += int32_t(common_delay.com0) - 1;
    seq += int32_t(common_delay.com0) - 1;
  }
  if (mem_delay.use_com2_time)
  {
    first += int32_t(common_delay.com2);
    seq += int32_t(common_delay.com2);
  }
  if (mem_delay.use_com3_time)
  {
    min = int32_t(common_delay.com3);
  }
  if (first < 6)
    first++;

  first = first + int32_t(mem_delay.access_time) + 2;
  seq = seq + int32_t(mem_delay.access_time) + 2;

  if (first < (min + 6))
    first = min + 6;
  if (seq < (min + 2))
    seq = min + 2;

  const TickCount byte_access_time = first;
  const TickCount halfword_access_time = mem_delay.data_bus_16bit ? first : (first + seq);
  const TickCount word_access_time = mem_delay.data_bus_16bit ? (first + seq) : (first + seq + seq + seq);
  return std::tie(std::max(byte_access_time - 1, 0), std::max(halfword_access_time - 1, 0),
                  std::max(word_access_time - 1, 0));
}

void RecalculateMemoryTimings()
{
  std::tie(m_bios_access_time[0], m_bios_access_time[1], m_bios_access_time[2]) =
    CalculateMemoryTiming(m_MEMCTRL.bios_delay_size, m_MEMCTRL.common_delay);
  std::tie(m_cdrom_access_time[0], m_cdrom_access_time[1], m_cdrom_access_time[2]) =
    CalculateMemoryTiming(m_MEMCTRL.cdrom_delay_size, m_MEMCTRL.common_delay);
  std::tie(m_spu_access_time[0], m_spu_access_time[1], m_spu_access_time[2]) =
    CalculateMemoryTiming(m_MEMCTRL.spu_delay_size, m_MEMCTRL.common_delay);
}

bool AllocateMemory(bool enable_8mb_ram)
{
  if (!m_memory_arena.Create(MEMORY_ARENA_SIZE, true, false))
    return false;

  // Create the base views.
  const uint32_t ram_size = enable_8mb_ram ? RAM_8MB_SIZE : RAM_2MB_SIZE;
  const uint32_t ram_mask = enable_8mb_ram ? RAM_8MB_MASK : RAM_2MB_MASK;
  g_ram = static_cast<uint8_t*>(m_memory_arena.CreateViewPtr(MEMORY_ARENA_RAM_OFFSET, ram_size, true, false));
  if (!g_ram)
    return false;

  g_ram_mask = ram_mask;
  g_ram_size = ram_size;
  m_ram_code_page_count = enable_8mb_ram ? RAM_8MB_CODE_PAGE_COUNT : RAM_2MB_CODE_PAGE_COUNT;
  return true;
}

void ReleaseMemory()
{
  if (g_ram)
  {
    m_memory_arena.ReleaseViewPtr(g_ram, g_ram_size);
    g_ram = nullptr;
    g_ram_mask = 0;
    g_ram_size = 0;
  }

  m_memory_arena.Destroy();
}

static ALWAYS_INLINE uint32_t FastmemAddressToLUTPageIndex(uint32_t address)
{
  return address >> 12;
}

static ALWAYS_INLINE_RELEASE void SetLUTFastmemPage(uint32_t address, uint8_t* ptr, bool writable)
{
  m_fastmem_lut[FastmemAddressToLUTPageIndex(address)] = ptr;
  m_fastmem_lut[FASTMEM_LUT_NUM_PAGES + FastmemAddressToLUTPageIndex(address)] = writable ? ptr : nullptr;
}

uint8_t* GetFastmemBase()
{
#ifdef WITH_MMAP_FASTMEM
  if (m_fastmem_mode == CPUFastmemMode::MMap)
    return m_fastmem_base;
#endif
  if (m_fastmem_mode == CPUFastmemMode::LUT)
    return reinterpret_cast<uint8_t*>(m_fastmem_lut);

  return nullptr;
}

void UpdateFastmemViews(CPUFastmemMode mode)
{
#ifdef WITH_MMAP_FASTMEM
  m_fastmem_ram_views.clear();
  m_fastmem_reserved_views.clear();
#endif

  m_fastmem_mode = mode;
  if (mode == CPUFastmemMode::Disabled)
  {
#ifdef WITH_MMAP_FASTMEM
    m_fastmem_base = nullptr;
#endif
    std::free(m_fastmem_lut);
    m_fastmem_lut = nullptr;
    return;
  }

#ifdef WITH_MMAP_FASTMEM
  if (mode == CPUFastmemMode::MMap)
  {
    std::free(m_fastmem_lut);
    m_fastmem_lut = nullptr;

    if (!m_fastmem_base)
    {
      m_fastmem_base = static_cast<uint8_t*>(m_memory_arena.FindBaseAddressForMapping(FASTMEM_REGION_SIZE));
      if (!m_fastmem_base)
        return;
    }

    auto MapRAM = [](uint32_t base_address) {
      uint8_t* map_address = m_fastmem_base + base_address;
      auto view = m_memory_arena.CreateView(MEMORY_ARENA_RAM_OFFSET, g_ram_size, true, false, map_address);
      if (!view)
        return;

      // mark all pages with code as non-writable
      for (uint32_t i = 0; i < m_ram_code_page_count; i++)
      {
        if (m_ram_code_bits[i])
        {
          uint8_t* page_address = map_address + (i * HOST_PAGE_SIZE);
          if (!m_memory_arena.SetPageProtection(page_address, HOST_PAGE_SIZE, true, false, false))
            return;
        }
      }

      m_fastmem_ram_views.push_back(std::move(view.value()));
    };

    auto ReserveRegion = [](uint32_t start_address, uint32_t end_address_inclusive) {
    // We don't reserve memory regions on Android because the app could be subject to address space size limitations.
#ifndef __ANDROID__
      uint8_t* map_address = m_fastmem_base + start_address;
      auto view = m_memory_arena.CreateReservedView(end_address_inclusive - start_address + 1, map_address);
      if (!view)
        return;

      m_fastmem_reserved_views.push_back(std::move(view.value()));
#endif
    };

    // KUSEG - cached
    MapRAM(0x00000000);
    ReserveRegion(0x00000000 + g_ram_size, 0x80000000 - 1);

    // KSEG0 - cached
    MapRAM(0x80000000);
    ReserveRegion(0x80000000 + g_ram_size, 0xA0000000 - 1);

    // KSEG1 - uncached
    MapRAM(0xA0000000);
    ReserveRegion(0xA0000000 + g_ram_size, 0xFFFFFFFF);

    return;
  }
#endif

#ifdef WITH_MMAP_FASTMEM
  m_fastmem_base = nullptr;
#endif

  if (!m_fastmem_lut)
  {
    m_fastmem_lut = static_cast<uint8_t**>(std::calloc(FASTMEM_LUT_NUM_SLOTS, sizeof(uint8_t*)));
    if (!m_fastmem_lut)
    {
      // fastmem cannot function without its lookup table; fall back to disabled
      m_fastmem_mode = CPUFastmemMode::Disabled;
      return;
    }
  }

  auto MapRAM = [](uint32_t base_address) {
    for (uint32_t address = 0; address < g_ram_size; address += HOST_PAGE_SIZE)
    {
      SetLUTFastmemPage(base_address + address, &g_ram[address],
                        !m_ram_code_bits[FastmemAddressToLUTPageIndex(address)]);
    }
  };

  // KUSEG - cached
  MapRAM(0x00000000);
  MapRAM(0x00200000);
  MapRAM(0x00400000);
  MapRAM(0x00600000);

  // KSEG0 - cached
  MapRAM(0x80000000);
  MapRAM(0x80200000);
  MapRAM(0x80400000);
  MapRAM(0x80600000);

  // KSEG1 - uncached
  MapRAM(0xA0000000);
  MapRAM(0xA0200000);
  MapRAM(0xA0400000);
  MapRAM(0xA0600000);
}

bool CanUseFastmemForAddress(VirtualMemoryAddress address)
{
  const PhysicalMemoryAddress paddr = address & CPU::PHYSICAL_MEMORY_ADDRESS_MASK;

  switch (m_fastmem_mode)
  {
#ifdef WITH_MMAP_FASTMEM
    // Currently since we don't map the mirrors, don't use fastmem for them.
    // This is because the swapping of page code bits for SMC is too expensive.
    case CPUFastmemMode::MMap:
      return (paddr < RAM_MIRROR_END);
#endif

    case CPUFastmemMode::LUT:
      return (paddr < g_ram_size);

    case CPUFastmemMode::Disabled:
    default:
      break;
  }

  return false;
}

bool IsRAMCodePage(uint32_t index)
{
  return m_ram_code_bits[index];
}

void SetRAMCodePage(uint32_t index)
{
  if (m_ram_code_bits[index])
    return;

  // protect fastmem pages
  m_ram_code_bits[index] = true;
  SetCodePageFastmemProtection(index, false);
}

void ClearRAMCodePage(uint32_t index)
{
  if (!m_ram_code_bits[index])
    return;

  // unprotect fastmem pages
  m_ram_code_bits[index] = false;
  SetCodePageFastmemProtection(index, true);
}

void SetCodePageFastmemProtection(uint32_t page_index, bool writable)
{
#ifdef WITH_MMAP_FASTMEM
  if (m_fastmem_mode == CPUFastmemMode::MMap)
  {
    // unprotect fastmem pages
    for (const auto& view : m_fastmem_ram_views)
    {
      uint8_t* page_address = static_cast<uint8_t*>(view.GetBasePointer()) + (page_index * HOST_PAGE_SIZE);
      m_memory_arena.SetPageProtection(page_address, HOST_PAGE_SIZE, true, writable, false);
    }

    return;
  }
#endif

  if (m_fastmem_mode == CPUFastmemMode::LUT)
  {
    // mirrors...
    const uint32_t ram_address = page_index * HOST_PAGE_SIZE;
    for (uint32_t mirror_start : m_fastmem_ram_mirrors)
      SetLUTFastmemPage(mirror_start + ram_address, &g_ram[ram_address], writable);
  }
}

void ClearRAMCodePageFlags()
{
  m_ram_code_bits.reset();

#ifdef WITH_MMAP_FASTMEM
  if (m_fastmem_mode == CPUFastmemMode::MMap)
  {
    // unprotect fastmem pages
    for (const auto& view : m_fastmem_ram_views)
      m_memory_arena.SetPageProtection(view.GetBasePointer(), view.GetMappingSize(), true, true, false);
  }
#endif

  if (m_fastmem_mode == CPUFastmemMode::LUT)
  {
    for (uint32_t i = 0; i < m_ram_code_page_count; i++)
    {
      const uint32_t addr = (i * HOST_PAGE_SIZE);
      for (uint32_t mirror_start : m_fastmem_ram_mirrors)
        SetLUTFastmemPage(mirror_start + addr, &g_ram[addr], true);
    }
  }
}

static TickCount DoInvalidAccess(MemoryAccessType type, MemoryAccessSize size, PhysicalMemoryAddress address,
                                 uint32_t& value)
{
  if (type == MemoryAccessType::Read)
    value = UINT32_C(0xFFFFFFFF);
  return (type == MemoryAccessType::Read) ? 1 : 0;
}

template<MemoryAccessType type, MemoryAccessSize size, bool skip_redundant_writes>
ALWAYS_INLINE static TickCount DoRAMAccess(uint32_t offset, uint32_t& value)
{
  offset &= g_ram_mask;
  if constexpr (type == MemoryAccessType::Read)
  {
    if constexpr (size == MemoryAccessSize::Byte)
    {
      value = static_cast<uint32_t>(g_ram[offset]);
    }
    else if constexpr (size == MemoryAccessSize::HalfWord)
    {
      uint16_t temp;
      std::memcpy(&temp, &g_ram[offset], sizeof(uint16_t));
      value = static_cast<uint32_t>(temp);
    }
    else if constexpr (size == MemoryAccessSize::Word)
    {
      std::memcpy(&value, &g_ram[offset], sizeof(uint32_t));
    }
  }
  else
  {
    const uint32_t page_index = offset / HOST_PAGE_SIZE;
    if constexpr (skip_redundant_writes)
    {
      if constexpr (size == MemoryAccessSize::Byte)
      {
        if (g_ram[offset] != static_cast<uint8_t>(value))
        {
          g_ram[offset] = static_cast<uint8_t>(value);
          if (m_ram_code_bits[page_index])
            CPU::CodeCache::InvalidateBlocksWithPageIndex(page_index);
        }
      }
      else if constexpr (size == MemoryAccessSize::HalfWord)
      {
        const uint16_t new_value = static_cast<uint16_t>(value);
        uint16_t old_value;
        std::memcpy(&old_value, &g_ram[offset], sizeof(old_value));
        if (old_value != new_value)
        {
          std::memcpy(&g_ram[offset], &new_value, sizeof(uint16_t));
          if (m_ram_code_bits[page_index])
            CPU::CodeCache::InvalidateBlocksWithPageIndex(page_index);
        }
      }
      else if constexpr (size == MemoryAccessSize::Word)
      {
        uint32_t old_value;
        std::memcpy(&old_value, &g_ram[offset], sizeof(uint32_t));
        if (old_value != value)
        {
          std::memcpy(&g_ram[offset], &value, sizeof(uint32_t));
          if (m_ram_code_bits[page_index])
            CPU::CodeCache::InvalidateBlocksWithPageIndex(page_index);
        }
      }
    }
    else
    {
      if (m_ram_code_bits[page_index])
        CPU::CodeCache::InvalidateBlocksWithPageIndex(page_index);

      if constexpr (size == MemoryAccessSize::Byte)
      {
        g_ram[offset] = static_cast<uint8_t>(value);
      }
      else if constexpr (size == MemoryAccessSize::HalfWord)
      {
        const uint16_t temp = static_cast<uint16_t>(value);
        std::memcpy(&g_ram[offset], &temp, sizeof(uint16_t));
      }
      else if constexpr (size == MemoryAccessSize::Word)
      {
        std::memcpy(&g_ram[offset], &value, sizeof(uint32_t));
      }
    }
  }

  return (type == MemoryAccessType::Read) ? RAM_READ_TICKS : 0;
}

template<MemoryAccessType type, MemoryAccessSize size>
ALWAYS_INLINE static TickCount DoBIOSAccess(uint32_t offset, uint32_t& value)
{
  // TODO: Configurable mirroring.
  if constexpr (type == MemoryAccessType::Read)
  {
    offset &= UINT32_C(0x7FFFF);
    if constexpr (size == MemoryAccessSize::Byte)
      value = static_cast<uint32_t>(g_bios[offset]);
    else if constexpr (size == MemoryAccessSize::HalfWord)
    {
      uint16_t temp;
      std::memcpy(&temp, &g_bios[offset], sizeof(uint16_t));
      value = static_cast<uint32_t>(temp);
    }
    else
      std::memcpy(&value, &g_bios[offset], sizeof(uint32_t));
  }

  return m_bios_access_time[static_cast<uint32_t>(size)];
}

template<MemoryAccessType type, MemoryAccessSize size>
static TickCount DoEXP1Access(uint32_t offset, uint32_t& value)
{
  if constexpr (type == MemoryAccessType::Read)
  {
    // EXP1 not present.
    if (m_exp1_rom.empty())
      value = UINT32_C(0xFFFFFFFF);
    // Bit 0 - Action Replay On/Off
    else if (offset == 0x20018)
      value = UINT32_C(1);
    else
    {
      const uint32_t transfer_size = uint32_t(1) << static_cast<uint32_t>(size);
      if ((offset + transfer_size) > m_exp1_rom.size())
        value = UINT32_C(0);
      else
      {
        if constexpr (size == MemoryAccessSize::Byte)
          value = static_cast<uint32_t>(m_exp1_rom[offset]);
        else if constexpr (size == MemoryAccessSize::HalfWord)
        {
          uint16_t halfword;
          std::memcpy(&halfword, &m_exp1_rom[offset], sizeof(halfword));
          value = static_cast<uint32_t>(halfword);
        }
        else
          std::memcpy(&value, &m_exp1_rom[offset], sizeof(value));
      }
    }

    return m_exp1_access_time[static_cast<uint32_t>(size)];
  }
  return 0;
}

template<MemoryAccessType type, MemoryAccessSize size>
static TickCount DoEXP2Access(uint32_t offset, uint32_t& value)
{
  if constexpr (type == MemoryAccessType::Read)
  {
    // rx/tx buffer empty
    if (offset == 0x21)
      value = 0x04 | 0x08;
    // nocash expansion area
    else if (offset >= 0x60 && offset <= 0x67)
      value = UINT32_C(0xFFFFFFFF);
    else
      value = UINT32_C(0xFFFFFFFF);

    return m_exp2_access_time[static_cast<uint32_t>(size)];
  }
  if (offset == 0x23 || offset == 0x80)
  {
    if (value == '\r') { }
    else if (value == '\n')
      m_tty_line_buffer.clear();
    else
      m_tty_line_buffer += static_cast<char>(static_cast<uint8_t>(value));
  }
  return 0;
}

template<MemoryAccessType type>
ALWAYS_INLINE static TickCount DoEXP3Access(uint32_t offset, uint32_t& value)
{
  if constexpr (type == MemoryAccessType::Read)
    value = UINT32_C(0xFFFFFFFF);
  return 0;
}

template<MemoryAccessType type>
ALWAYS_INLINE static TickCount DoUnknownEXPAccess(uint32_t address, uint32_t& value)
{
  if constexpr (type == MemoryAccessType::Read)
    return -1;
  return 0;
}

template<MemoryAccessType type, MemoryAccessSize size>
ALWAYS_INLINE static TickCount DoMemoryControlAccess(uint32_t offset, uint32_t& value)
{
  if constexpr (type == MemoryAccessType::Read)
  {
    value = m_MEMCTRL.regs[FIXUP_WORD_OFFSET(size, offset) / 4];
    value = FIXUP_WORD_READ_VALUE(size, offset, value);
    return 2;
  }
  else
  {
    const uint32_t index = FIXUP_WORD_OFFSET(size, offset) / 4;
    value = FIXUP_WORD_WRITE_VALUE(size, offset, value);

    const uint32_t write_mask = (index == 8) ? COMDELAY::WRITE_MASK : MEMDELAY::WRITE_MASK;
    const uint32_t new_value = (m_MEMCTRL.regs[index] & ~write_mask) | (value & write_mask);
    if (m_MEMCTRL.regs[index] != new_value)
    {
      m_MEMCTRL.regs[index] = new_value;
      RecalculateMemoryTimings();
    }
  }
  return 0;
}

template<MemoryAccessType type, MemoryAccessSize size>
ALWAYS_INLINE static TickCount DoMemoryControl2Access(uint32_t offset, uint32_t& value)
{
  if constexpr (type == MemoryAccessType::Read)
  {
    if (offset != 0x00)
      return DoInvalidAccess(type, size, MEMCTRL2_BASE | offset, value);
    value = m_ram_size_reg;
    return 2;
  }
  if (offset != 0x00)
    return DoInvalidAccess(type, size, MEMCTRL2_BASE | offset, value);
  m_ram_size_reg = value;
  return 0;
}

template<MemoryAccessType type, MemoryAccessSize size>
ALWAYS_INLINE static TickCount DoPadAccess(uint32_t offset, uint32_t& value)
{
  if constexpr (type == MemoryAccessType::Read)
  {
    value = g_pad.ReadRegister(FIXUP_HALFWORD_OFFSET(size, offset));
    value = FIXUP_HALFWORD_READ_VALUE(size, offset, value);
    return 2;
  }
  g_pad.WriteRegister(FIXUP_HALFWORD_OFFSET(size, offset), FIXUP_HALFWORD_WRITE_VALUE(size, offset, value));
  return 0;
}

template<MemoryAccessType type, MemoryAccessSize size>
ALWAYS_INLINE static TickCount DoSIOAccess(uint32_t offset, uint32_t& value)
{
  if constexpr (type == MemoryAccessType::Read)
  {
    value = g_sio.ReadRegister(FIXUP_HALFWORD_OFFSET(size, offset));
    value = FIXUP_HALFWORD_READ_VALUE(size, offset, value);
    return 2;
  }
  g_sio.WriteRegister(FIXUP_HALFWORD_OFFSET(size, offset), FIXUP_HALFWORD_WRITE_VALUE(size, offset, value));
  return 0;
}

template<MemoryAccessType type, MemoryAccessSize size>
ALWAYS_INLINE static TickCount DoCDROMAccess(uint32_t offset, uint32_t& value)
{
  if constexpr (type == MemoryAccessType::Read)
  {
    switch (size)
    {
      case MemoryAccessSize::Word:
      {
        const uint32_t b0 = static_cast<uint32_t>(g_cdrom.ReadRegister(offset));
        const uint32_t b1 = static_cast<uint32_t>(g_cdrom.ReadRegister(offset + 1u));
        const uint32_t b2 = static_cast<uint32_t>(g_cdrom.ReadRegister(offset + 2u));
        const uint32_t b3 = static_cast<uint32_t>(g_cdrom.ReadRegister(offset + 3u));
        value = b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
        break;
      }

      case MemoryAccessSize::HalfWord:
      {
        const uint32_t lsb = static_cast<uint32_t>(g_cdrom.ReadRegister(offset));
        const uint32_t msb = static_cast<uint32_t>(g_cdrom.ReadRegister(offset + 1u));
        value = lsb | (msb << 8);
        break;
      }

      case MemoryAccessSize::Byte:
      default:
        value = static_cast<uint32_t>(g_cdrom.ReadRegister(offset));
    }

    return m_cdrom_access_time[static_cast<uint32_t>(size)];
  }
  else
  {
    switch (size)
    {
      case MemoryAccessSize::Word:
        g_cdrom.WriteRegister(offset, static_cast<uint8_t>(value));
        g_cdrom.WriteRegister(offset + 1u, static_cast<uint8_t>((value >> 8)));
        g_cdrom.WriteRegister(offset + 2u, static_cast<uint8_t>((value >> 16)));
        g_cdrom.WriteRegister(offset + 3u, static_cast<uint8_t>((value >> 24)));
      break;

      case MemoryAccessSize::HalfWord:
        g_cdrom.WriteRegister(offset, static_cast<uint8_t>(value));
        g_cdrom.WriteRegister(offset + 1u, static_cast<uint8_t>((value >> 8)));
      break;

      case MemoryAccessSize::Byte:
      default:
      g_cdrom.WriteRegister(offset, static_cast<uint8_t>(value));
    }
  }
  return 0;
}

template<MemoryAccessType type, MemoryAccessSize size>
ALWAYS_INLINE static TickCount DoGPUAccess(uint32_t offset, uint32_t& value)
{
  if constexpr (type == MemoryAccessType::Read)
  {
    value = g_gpu->ReadRegister(FIXUP_WORD_OFFSET(size, offset));
    value = FIXUP_WORD_READ_VALUE(size, offset, value);
    return 2;
  }
  g_gpu->WriteRegister(FIXUP_WORD_OFFSET(size, offset), FIXUP_WORD_WRITE_VALUE(size, offset, value));
  return 0;
}

template<MemoryAccessType type, MemoryAccessSize size>
ALWAYS_INLINE static TickCount DoMDECAccess(uint32_t offset, uint32_t& value)
{
  if constexpr (type == MemoryAccessType::Read)
  {
    value = g_mdec.ReadRegister(FIXUP_WORD_OFFSET(size, offset));
    value = FIXUP_WORD_READ_VALUE(size, offset, value);
    return 2;
  }
  g_mdec.WriteRegister(FIXUP_WORD_OFFSET(size, offset), FIXUP_WORD_WRITE_VALUE(size, offset, value));
  return 0;
}

template<MemoryAccessType type, MemoryAccessSize size>
ALWAYS_INLINE static TickCount DoAccessInterruptController(uint32_t offset, uint32_t& value)
{
  if constexpr (type == MemoryAccessType::Read)
  {
    value = g_interrupt_controller.ReadRegister(FIXUP_WORD_OFFSET(size, offset));
    value = FIXUP_WORD_READ_VALUE(size, offset, value);
    return 2;
  }
  g_interrupt_controller.WriteRegister(FIXUP_WORD_OFFSET(size, offset), FIXUP_WORD_WRITE_VALUE(size, offset, value));
  return 0;
}

template<MemoryAccessType type, MemoryAccessSize size>
ALWAYS_INLINE static TickCount DoAccessTimers(uint32_t offset, uint32_t& value)
{
  if constexpr (type == MemoryAccessType::Read)
  {
    value = g_timers.ReadRegister(FIXUP_WORD_OFFSET(size, offset));
    value = FIXUP_WORD_READ_VALUE(size, offset, value);
    return 2;
  }
  g_timers.WriteRegister(FIXUP_WORD_OFFSET(size, offset), FIXUP_WORD_WRITE_VALUE(size, offset, value));
  return 0;
}

template<MemoryAccessType type, MemoryAccessSize size>
ALWAYS_INLINE static TickCount DoAccessSPU(uint32_t offset, uint32_t& value)
{
  if constexpr (type == MemoryAccessType::Read)
  {
    switch (size)
    {
      case MemoryAccessSize::Word:
      {
        // 32-bit reads are read as two 16-bit accesses.
        const uint16_t lsb = g_spu.ReadRegister(offset);
        const uint16_t msb = g_spu.ReadRegister(offset + 2);
        value = static_cast<uint32_t>(lsb) | (static_cast<uint32_t>(msb) << 16);
      }
      break;

      case MemoryAccessSize::HalfWord:
        value = static_cast<uint32_t>(g_spu.ReadRegister(offset));
      break;

      case MemoryAccessSize::Byte:
      default:
      {
        const uint16_t value16 = g_spu.ReadRegister(FIXUP_HALFWORD_OFFSET(size, offset));
        value = FIXUP_HALFWORD_READ_VALUE(size, offset, value16);
      }
      break;
    }

    return m_spu_access_time[static_cast<uint32_t>(size)];
  }
  {
    // 32-bit writes are written as two 16-bit writes.
    // TODO: Ignore if address is not aligned.
    switch (size)
    {
      case MemoryAccessSize::Word:
      {
        g_spu.WriteRegister(offset, static_cast<uint16_t>(value));
        g_spu.WriteRegister(offset + 2, static_cast<uint16_t>(value >> 16));
        break;
      }

      case MemoryAccessSize::HalfWord:
      {
        g_spu.WriteRegister(offset, static_cast<uint16_t>(value));
        break;
      }

      case MemoryAccessSize::Byte:
      {
        g_spu.WriteRegister(FIXUP_HALFWORD_OFFSET(size, offset),
                            static_cast<uint16_t>(FIXUP_HALFWORD_READ_VALUE(size, offset, value)));
        break;
      }
    }
  }
  return 0;
}

template<MemoryAccessType type, MemoryAccessSize size>
ALWAYS_INLINE static TickCount DoDMAAccess(uint32_t offset, uint32_t& value)
{
  if constexpr (type == MemoryAccessType::Read)
  {
    value = g_dma.ReadRegister(FIXUP_WORD_OFFSET(size, offset));
    value = FIXUP_WORD_READ_VALUE(size, offset, value);
    return 2;
  }
  g_dma.WriteRegister(FIXUP_WORD_OFFSET(size, offset), FIXUP_WORD_WRITE_VALUE(size, offset, value));
  return 0;
}

} // namespace Bus

namespace CPU {

template<bool add_ticks, bool icache_read = false, uint32_t word_count = 1, bool raise_exceptions>
ALWAYS_INLINE_RELEASE bool DoInstructionRead(PhysicalMemoryAddress address, void* data)
{
  using namespace Bus;

  address &= PHYSICAL_MEMORY_ADDRESS_MASK;

  if (address < RAM_MIRROR_END)
  {
    std::memcpy(data, &g_ram[address & g_ram_mask], sizeof(uint32_t) * word_count);
    if constexpr (add_ticks)
      g_state.pending_ticks += (icache_read ? 1 : RAM_READ_TICKS) * word_count;

    return true;
  }
  else if (address >= BIOS_BASE && address < (BIOS_BASE + BIOS_SIZE))
  {
    std::memcpy(data, &g_bios[(address - BIOS_BASE) & BIOS_MASK], sizeof(uint32_t) * word_count);
    if constexpr (add_ticks)
      g_state.pending_ticks += m_bios_access_time[static_cast<uint32_t>(MemoryAccessSize::Word)] * word_count;

    return true;
  }
  if (raise_exceptions)
    CPU::RaiseException(address, Cop0Registers::CAUSE::MakeValueForException(Exception::IBE, false, false, 0));

  std::memset(data, 0, sizeof(uint32_t) * word_count);
  return false;
}

TickCount GetInstructionReadTicks(VirtualMemoryAddress address)
{
  using namespace Bus;

  address &= PHYSICAL_MEMORY_ADDRESS_MASK;

  if (address < RAM_MIRROR_END)
    return RAM_READ_TICKS;
  else if (address >= BIOS_BASE && address < (BIOS_BASE + BIOS_SIZE))
    return m_bios_access_time[static_cast<uint32_t>(MemoryAccessSize::Word)];
  return 0;
}

TickCount GetICacheFillTicks(VirtualMemoryAddress address)
{
  using namespace Bus;

  address &= PHYSICAL_MEMORY_ADDRESS_MASK;

  if (address < RAM_MIRROR_END)
    return 1 * ((ICACHE_LINE_SIZE - (address & (ICACHE_LINE_SIZE - 1))) / sizeof(uint32_t));
  else if (address >= BIOS_BASE && address < (BIOS_BASE + BIOS_SIZE))
    return m_bios_access_time[static_cast<uint32_t>(MemoryAccessSize::Word)] *
           ((ICACHE_LINE_SIZE - (address & (ICACHE_LINE_SIZE - 1))) / sizeof(uint32_t));
  return 0;
}

void CheckAndUpdateICacheTags(uint32_t line_count, TickCount uncached_ticks)
{
  VirtualMemoryAddress current_pc = g_state.regs.pc & ICACHE_TAG_ADDRESS_MASK;
  if (IsCachedAddress(current_pc))
  {
    TickCount ticks = 0;
    TickCount cached_ticks_per_line = GetICacheFillTicks(current_pc);
    for (uint32_t i = 0; i < line_count; i++, current_pc += ICACHE_LINE_SIZE)
    {
      const uint32_t line = GetICacheLine(current_pc);
      if (g_state.icache_tags[line] != current_pc)
      {
        g_state.icache_tags[line] = current_pc;
        ticks += cached_ticks_per_line;
      }
    }

    g_state.pending_ticks += ticks;
  }
  else
    g_state.pending_ticks += uncached_ticks;
}

uint32_t FillICache(VirtualMemoryAddress address)
{
  const uint32_t line = GetICacheLine(address);
  uint8_t* line_data = &g_state.icache_data[line * ICACHE_LINE_SIZE];
  uint32_t line_tag;
  switch ((address >> 2) & 0x03u)
  {
    case 0:
      DoInstructionRead<true, true, 4, false>(address & ~(ICACHE_LINE_SIZE - 1u), line_data);
      line_tag = GetICacheTagForAddress(address);
      break;
    case 1:
      DoInstructionRead<true, true, 3, false>(address & (~(ICACHE_LINE_SIZE - 1u) | 0x4), line_data + 0x4);
      line_tag = GetICacheTagForAddress(address) | 0x1;
      break;
    case 2:
      DoInstructionRead<true, true, 2, false>(address & (~(ICACHE_LINE_SIZE - 1u) | 0x8), line_data + 0x8);
      line_tag = GetICacheTagForAddress(address) | 0x3;
      break;
    case 3:
    default:
      DoInstructionRead<true, true, 1, false>(address & (~(ICACHE_LINE_SIZE - 1u) | 0xC), line_data + 0xC);
      line_tag = GetICacheTagForAddress(address) | 0x7;
      break;
  }
  g_state.icache_tags[line] = line_tag;

  const uint32_t offset = GetICacheLineOffset(address);
  uint32_t result;
  std::memcpy(&result, &line_data[offset], sizeof(result));
  return result;
}

void ClearICache()
{
  std::memset(g_state.icache_data.data(), 0, ICACHE_SIZE);
  g_state.icache_tags.fill(ICACHE_INVALID_BITS);
}

ALWAYS_INLINE_RELEASE static uint32_t ReadICache(VirtualMemoryAddress address)
{
  const uint32_t line = GetICacheLine(address);
  const uint8_t* line_data = &g_state.icache_data[line * ICACHE_LINE_SIZE];
  const uint32_t offset = GetICacheLineOffset(address);
  uint32_t result;
  std::memcpy(&result, &line_data[offset], sizeof(result));
  return result;
}

ALWAYS_INLINE_RELEASE static void WriteICache(VirtualMemoryAddress address, uint32_t value)
{
  const uint32_t line = GetICacheLine(address);
  const uint32_t offset = GetICacheLineOffset(address);
  g_state.icache_tags[line] = GetICacheTagForAddress(address) | ICACHE_INVALID_BITS;
  std::memcpy(&g_state.icache_data[line * ICACHE_LINE_SIZE + offset], &value, sizeof(value));
}

static void WriteCacheControl(uint32_t value)
{
  g_state.cache_control.bits = value;
}

template<MemoryAccessType type, MemoryAccessSize size>
ALWAYS_INLINE static TickCount DoScratchpadAccess(PhysicalMemoryAddress address, uint32_t& value)
{
  const PhysicalMemoryAddress cache_offset = address & DCACHE_OFFSET_MASK;
  if constexpr (size == MemoryAccessSize::Byte)
  {
    if constexpr (type == MemoryAccessType::Read)
      value = static_cast<uint32_t>(g_state.dcache[cache_offset]);
    else
      g_state.dcache[cache_offset] = static_cast<uint8_t>(value);
  }
  else if constexpr (size == MemoryAccessSize::HalfWord)
  {
    if constexpr (type == MemoryAccessType::Read)
    {
      uint16_t temp;
      std::memcpy(&temp, &g_state.dcache[cache_offset], sizeof(temp));
      value = static_cast<uint32_t>(temp);
    }
    else
    {
      uint16_t temp = static_cast<uint16_t>(value);
      std::memcpy(&g_state.dcache[cache_offset], &temp, sizeof(temp));
    }
  }
  else if constexpr (size == MemoryAccessSize::Word)
  {
    if constexpr (type == MemoryAccessType::Read)
      std::memcpy(&value, &g_state.dcache[cache_offset], sizeof(value));
    else
      std::memcpy(&g_state.dcache[cache_offset], &value, sizeof(value));
  }

  return 0;
}

template<MemoryAccessType type, MemoryAccessSize size>
static ALWAYS_INLINE TickCount DoMemoryAccess(VirtualMemoryAddress address, uint32_t& value)
{
  using namespace Bus;

  switch (address >> 29)
  {
    case 0x00: // KUSEG 0M-512M
    case 0x04: // KSEG0 - physical memory cached
    {
      if constexpr (type == MemoryAccessType::Write)
      {
        if (g_state.cop0_regs.sr.Isc)
        {
          WriteICache(address, value);
          return 0;
        }
      }

      address &= PHYSICAL_MEMORY_ADDRESS_MASK;
      if ((address & DCACHE_LOCATION_MASK) == DCACHE_LOCATION)
        return DoScratchpadAccess<type, size>(address, value);
    }
    break;

    case 0x01: // KUSEG 512M-1024M
    case 0x02: // KUSEG 1024M-1536M
    case 0x03: // KUSEG 1536M-2048M
    {
      // Above 512mb raises an exception.
      if constexpr (type == MemoryAccessType::Read)
        value = UINT32_C(0xFFFFFFFF);

      return -1;
    }

    case 0x05: // KSEG1 - physical memory uncached
    {
      address &= PHYSICAL_MEMORY_ADDRESS_MASK;
    }
    break;

    case 0x06: // KSEG2
    case 0x07: // KSEG2
      if (address == 0xFFFE0130)
      {
        if constexpr (type == MemoryAccessType::Read)
          value = g_state.cache_control.bits;
        else
          WriteCacheControl(value);

        return 0;
      }
      if constexpr (type == MemoryAccessType::Read)
        value = UINT32_C(0xFFFFFFFF);
      return -1;
  }

  if (address < RAM_MIRROR_END)
    return DoRAMAccess<type, size, false>(address, value);
  else if (address >= BIOS_BASE && address < (BIOS_BASE + BIOS_SIZE))
    return DoBIOSAccess<type, size>(static_cast<uint32_t>(address - BIOS_BASE), value);
  else if (address < EXP1_BASE)
    return DoInvalidAccess(type, size, address, value);
  else if (address < (EXP1_BASE + EXP1_SIZE))
    return DoEXP1Access<type, size>(address & EXP1_MASK, value);
  else if (address < MEMCTRL_BASE)
    return DoInvalidAccess(type, size, address, value);
  else if (address < (MEMCTRL_BASE + MEMCTRL_SIZE))
    return DoMemoryControlAccess<type, size>(address & MEMCTRL_MASK, value);
  else if (address < (PAD_BASE + PAD_SIZE))
    return DoPadAccess<type, size>(address & PAD_MASK, value);
  else if (address < (SIO_BASE + SIO_SIZE))
    return DoSIOAccess<type, size>(address & SIO_MASK, value);
  else if (address < (MEMCTRL2_BASE + MEMCTRL2_SIZE))
    return DoMemoryControl2Access<type, size>(address & MEMCTRL2_MASK, value);
  else if (address < (INTERRUPT_CONTROLLER_BASE + INTERRUPT_CONTROLLER_SIZE))
    return DoAccessInterruptController<type, size>(address & INTERRUPT_CONTROLLER_MASK, value);
  else if (address < (DMA_BASE + DMA_SIZE))
    return DoDMAAccess<type, size>(address & DMA_MASK, value);
  else if (address < (TIMERS_BASE + TIMERS_SIZE))
    return DoAccessTimers<type, size>(address & TIMERS_MASK, value);
  else if (address < CDROM_BASE)
    return DoInvalidAccess(type, size, address, value);
  else if (address < (CDROM_BASE + GPU_SIZE))
    return DoCDROMAccess<type, size>(address & CDROM_MASK, value);
  else if (address < (GPU_BASE + GPU_SIZE))
    return DoGPUAccess<type, size>(address & GPU_MASK, value);
  else if (address < (MDEC_BASE + MDEC_SIZE))
    return DoMDECAccess<type, size>(address & MDEC_MASK, value);
  else if (address < SPU_BASE)
    return DoInvalidAccess(type, size, address, value);
  else if (address < (SPU_BASE + SPU_SIZE))
    return DoAccessSPU<type, size>(address & SPU_MASK, value);
  else if (address < EXP2_BASE)
    return DoInvalidAccess(type, size, address, value);
  else if (address < (EXP2_BASE + EXP2_SIZE))
    return DoEXP2Access<type, size>(address & EXP2_MASK, value);
  else if (address < EXP3_BASE)
    return DoUnknownEXPAccess<type>(address, value);
  else if (address < (EXP3_BASE + EXP3_SIZE))
    return DoEXP3Access<type>(address & EXP3_MASK, value);
  return DoInvalidAccess(type, size, address, value);
}

template<MemoryAccessType type, MemoryAccessSize size>
static bool DoAlignmentCheck(VirtualMemoryAddress address)
{
  if constexpr (size == MemoryAccessSize::HalfWord)
  {
    if (Common::IsAlignedPow2(address, 2))
      return true;
  }
  else if constexpr (size == MemoryAccessSize::Word)
  {
    if (Common::IsAlignedPow2(address, 4))
      return true;
  }
  else
    return true;

  g_state.cop0_regs.BadVaddr = address;
  RaiseException(type == MemoryAccessType::Read ? Exception::AdEL : Exception::AdES);
  return false;
}

bool FetchInstruction()
{
  const PhysicalMemoryAddress address = g_state.regs.npc;
  switch (address >> 29)
  {
    case 0x00: // KUSEG 0M-512M
    case 0x04: // KSEG0 - physical memory cached
      if (CompareICacheTag(address))
        g_state.next_instruction.bits = ReadICache(address);
      else
        g_state.next_instruction.bits = FillICache(address);
      break;

    case 0x05: // KSEG1 - physical memory uncached
    {
      if (!DoInstructionRead<true, false, 1, true>(address, &g_state.next_instruction.bits))
        return false;
    }
    break;

    case 0x01: // KUSEG 512M-1024M
    case 0x02: // KUSEG 1024M-1536M
    case 0x03: // KUSEG 1536M-2048M
    case 0x06: // KSEG2
    case 0x07: // KSEG2
    default:
    {
      CPU::RaiseException(Cop0Registers::CAUSE::MakeValueForException(Exception::IBE,
                                                                      g_state.current_instruction_in_branch_delay_slot,
                                                                      g_state.current_instruction_was_branch_taken, 0),
                          address);
      return false;
    }
  }

  g_state.regs.pc = g_state.regs.npc;
  g_state.regs.npc += sizeof(g_state.next_instruction.bits);
  return true;
}

bool FetchInstructionForInterpreterFallback()
{
  const PhysicalMemoryAddress address = g_state.regs.npc;
  switch (address >> 29)
  {
    case 0x00: // KUSEG 0M-512M
    case 0x04: // KSEG0 - physical memory cached
    case 0x05: // KSEG1 - physical memory uncached
    {
      // We don't use the icache when doing interpreter fallbacks, because it's probably stale.
      if (!DoInstructionRead<false, false, 1, true>(address, &g_state.next_instruction.bits))
        return false;
    }
    break;

    case 0x01: // KUSEG 512M-1024M
    case 0x02: // KUSEG 1024M-1536M
    case 0x03: // KUSEG 1536M-2048M
    case 0x06: // KSEG2
    case 0x07: // KSEG2
    default:
    {
      CPU::RaiseException(Cop0Registers::CAUSE::MakeValueForException(Exception::IBE,
                                                                      g_state.current_instruction_in_branch_delay_slot,
                                                                      g_state.current_instruction_was_branch_taken, 0),
                          address);
      return false;
    }
  }

  g_state.regs.pc = g_state.regs.npc;
  g_state.regs.npc += sizeof(g_state.next_instruction.bits);
  return true;
}

bool SafeReadInstruction(VirtualMemoryAddress addr, uint32_t* value)
{
  switch (addr >> 29)
  {
    case 0x00: // KUSEG 0M-512M
    case 0x04: // KSEG0 - physical memory cached
    case 0x05: // KSEG1 - physical memory uncached
      // TODO: Check icache.
      return DoInstructionRead<false, false, 1, false>(addr, value);
    case 0x01: // KUSEG 512M-1024M
    case 0x02: // KUSEG 1024M-1536M
    case 0x03: // KUSEG 1536M-2048M
    case 0x06: // KSEG2
    case 0x07: // KSEG2
    default:
      break;
  }
  return false;
}

bool ReadMemoryByte(VirtualMemoryAddress addr, uint8_t* value)
{
  uint32_t temp = 0;
  const TickCount cycles = DoMemoryAccess<MemoryAccessType::Read, MemoryAccessSize::Byte>(addr, temp);
  *value = static_cast<uint8_t>(temp);
  if (cycles < 0)
  {
    RaiseException(Exception::DBE);
    return false;
  }

  g_state.pending_ticks += cycles;
  return true;
}

bool ReadMemoryHalfWord(VirtualMemoryAddress addr, uint16_t* value)
{
  if (!DoAlignmentCheck<MemoryAccessType::Read, MemoryAccessSize::HalfWord>(addr))
    return false;

  uint32_t temp = 0;
  const TickCount cycles = DoMemoryAccess<MemoryAccessType::Read, MemoryAccessSize::HalfWord>(addr, temp);
  *value = static_cast<uint16_t>(temp);
  if (cycles < 0)
  {
    RaiseException(Exception::DBE);
    return false;
  }

  g_state.pending_ticks += cycles;
  return true;
}

bool ReadMemoryWord(VirtualMemoryAddress addr, uint32_t* value)
{
  if (!DoAlignmentCheck<MemoryAccessType::Read, MemoryAccessSize::Word>(addr))
    return false;

  const TickCount cycles = DoMemoryAccess<MemoryAccessType::Read, MemoryAccessSize::Word>(addr, *value);
  if (cycles < 0)
  {
    RaiseException(Exception::DBE);
    return false;
  }

  g_state.pending_ticks += cycles;
  return true;
}

bool WriteMemoryByte(VirtualMemoryAddress addr, uint32_t value)
{
  const TickCount cycles = DoMemoryAccess<MemoryAccessType::Write, MemoryAccessSize::Byte>(addr, value);
  if (cycles < 0)
  {
    RaiseException(Exception::DBE);
    return false;
  }

  return true;
}

bool WriteMemoryHalfWord(VirtualMemoryAddress addr, uint32_t value)
{
  if (!DoAlignmentCheck<MemoryAccessType::Write, MemoryAccessSize::HalfWord>(addr))
    return false;

  const TickCount cycles = DoMemoryAccess<MemoryAccessType::Write, MemoryAccessSize::HalfWord>(addr, value);
  if (cycles < 0)
  {
    RaiseException(Exception::DBE);
    return false;
  }

  return true;
}

bool WriteMemoryWord(VirtualMemoryAddress addr, uint32_t value)
{
  if (!DoAlignmentCheck<MemoryAccessType::Write, MemoryAccessSize::Word>(addr))
    return false;

  const TickCount cycles = DoMemoryAccess<MemoryAccessType::Write, MemoryAccessSize::Word>(addr, value);
  if (cycles < 0)
  {
    RaiseException(Exception::DBE);
    return false;
  }

  return true;
}

template<MemoryAccessType type, MemoryAccessSize size>
static ALWAYS_INLINE bool DoSafeMemoryAccess(VirtualMemoryAddress address, uint32_t& value)
{
  using namespace Bus;

  switch (address >> 29)
  {
    case 0x00: // KUSEG 0M-512M
    case 0x04: // KSEG0 - physical memory cached
    {
      address &= PHYSICAL_MEMORY_ADDRESS_MASK;
      if ((address & DCACHE_LOCATION_MASK) == DCACHE_LOCATION)
      {
        DoScratchpadAccess<type, size>(address, value);
        return true;
      }
    }
    break;

    case 0x01: // KUSEG 512M-1024M
    case 0x02: // KUSEG 1024M-1536M
    case 0x03: // KUSEG 1536M-2048M
    case 0x06: // KSEG2
    case 0x07: // KSEG2
    {
      // Above 512mb raises an exception.
      return false;
    }

    case 0x05: // KSEG1 - physical memory uncached
    {
      address &= PHYSICAL_MEMORY_ADDRESS_MASK;
    }
    break;
  }

  if (address < RAM_MIRROR_END)
  {
    DoRAMAccess<type, size, true>(address, value);
    return true;
  }
  if constexpr (type == MemoryAccessType::Read)
  {
    if (address >= BIOS_BASE && address < (BIOS_BASE + BIOS_SIZE))
    {
      DoBIOSAccess<type, size>(static_cast<uint32_t>(address - BIOS_BASE), value);
      return true;
    }
  }
  return false;
}

bool SafeReadMemoryByte(VirtualMemoryAddress addr, uint8_t* value)
{
  uint32_t temp = 0;
  if (!DoSafeMemoryAccess<MemoryAccessType::Read, MemoryAccessSize::Byte>(addr, temp))
    return false;

  *value = static_cast<uint8_t>(temp);
  return true;
}

bool SafeReadMemoryHalfWord(VirtualMemoryAddress addr, uint16_t* value)
{
  if ((addr & 1) == 0)
  {
    uint32_t temp = 0;
    if (!DoSafeMemoryAccess<MemoryAccessType::Read, MemoryAccessSize::HalfWord>(addr, temp))
      return false;

    *value = static_cast<uint16_t>(temp);
    return true;
  }

  uint8_t low, high;
  if (!SafeReadMemoryByte(addr, &low) || !SafeReadMemoryByte(addr + 1, &high))
    return false;

  *value = (static_cast<uint16_t>(high) << 8) | static_cast<uint16_t>(low);
  return true;
}

bool SafeReadMemoryWord(VirtualMemoryAddress addr, uint32_t* value)
{
  if ((addr & 3) == 0)
    return DoSafeMemoryAccess<MemoryAccessType::Read, MemoryAccessSize::Word>(addr, *value);

  uint16_t low, high;
  if (!SafeReadMemoryHalfWord(addr, &low) || !SafeReadMemoryHalfWord(addr + 2, &high))
    return false;

  *value = (static_cast<uint32_t>(high) << 16) | static_cast<uint32_t>(low);
  return true;
}

bool SafeWriteMemoryByte(VirtualMemoryAddress addr, uint8_t value)
{
  uint32_t temp = static_cast<uint32_t>(value);
  return DoSafeMemoryAccess<MemoryAccessType::Write, MemoryAccessSize::Byte>(addr, temp);
}

bool SafeWriteMemoryHalfWord(VirtualMemoryAddress addr, uint16_t value)
{
  if ((addr & 1) == 0)
  {
    uint32_t temp = static_cast<uint32_t>(value);
    return DoSafeMemoryAccess<MemoryAccessType::Write, MemoryAccessSize::HalfWord>(addr, temp);
  }

  return SafeWriteMemoryByte(addr, static_cast<uint8_t>(value)) && SafeWriteMemoryByte(addr + 1, static_cast<uint8_t>(value >> 8));
}

bool SafeWriteMemoryWord(VirtualMemoryAddress addr, uint32_t value)
{
  if ((addr & 3) == 0)
    return DoSafeMemoryAccess<MemoryAccessType::Write, MemoryAccessSize::Word>(addr, value);

  return SafeWriteMemoryHalfWord(addr, static_cast<uint16_t>(value >> 16)) &&
         SafeWriteMemoryHalfWord(addr + 2, static_cast<uint16_t>(value >> 16));
}

void* GetDirectReadMemoryPointer(VirtualMemoryAddress address, MemoryAccessSize size, TickCount* read_ticks)
{
  using namespace Bus;

  const uint32_t seg = (address >> 29);
  if (seg != 0 && seg != 4 && seg != 5)
    return nullptr;

  const PhysicalMemoryAddress paddr = address & PHYSICAL_MEMORY_ADDRESS_MASK;
  if (paddr < RAM_MIRROR_END)
  {
    if (read_ticks)
      *read_ticks = RAM_READ_TICKS;

    return &g_ram[paddr & g_ram_mask];
  }

  if ((paddr & DCACHE_LOCATION_MASK) == DCACHE_LOCATION)
  {
    if (read_ticks)
      *read_ticks = 0;

    return &g_state.dcache[paddr & DCACHE_OFFSET_MASK];
  }

  if (paddr >= BIOS_BASE && paddr < (BIOS_BASE + BIOS_SIZE))
  {
    if (read_ticks)
      *read_ticks = m_bios_access_time[static_cast<uint32_t>(size)];

    return &g_bios[paddr & BIOS_MASK];
  }

  return nullptr;
}

void* GetDirectWriteMemoryPointer(VirtualMemoryAddress address, MemoryAccessSize size)
{
  using namespace Bus;

  const uint32_t seg = (address >> 29);
  if (seg != 0 && seg != 4 && seg != 5)
    return nullptr;

  const PhysicalMemoryAddress paddr = address & PHYSICAL_MEMORY_ADDRESS_MASK;
  if ((paddr & DCACHE_LOCATION_MASK) == DCACHE_LOCATION)
    return &g_state.dcache[paddr & DCACHE_OFFSET_MASK];

  return nullptr;
}

namespace Recompiler::Thunks {

uint64_t ReadMemoryByte(uint32_t address)
{
  uint32_t temp;
  const TickCount cycles = DoMemoryAccess<MemoryAccessType::Read, MemoryAccessSize::Byte>(address, temp);
  if (cycles < 0)
    return static_cast<uint64_t>(-static_cast<int64_t>(Exception::DBE));

  g_state.pending_ticks += cycles;
  return static_cast<uint64_t>(temp);
}

uint64_t ReadMemoryHalfWord(uint32_t address)
{
  if (!Common::IsAlignedPow2(address, 2))
  {
    g_state.cop0_regs.BadVaddr = address;
    return static_cast<uint64_t>(-static_cast<int64_t>(Exception::AdEL));
  }

  uint32_t temp;
  const TickCount cycles = DoMemoryAccess<MemoryAccessType::Read, MemoryAccessSize::HalfWord>(address, temp);
  if (cycles < 0)
    return static_cast<uint64_t>(-static_cast<int64_t>(Exception::DBE));

  g_state.pending_ticks += cycles;
  return static_cast<uint64_t>(temp);
}

uint64_t ReadMemoryWord(uint32_t address)
{
  if (!Common::IsAlignedPow2(address, 4))
  {
    g_state.cop0_regs.BadVaddr = address;
    return static_cast<uint64_t>(-static_cast<int64_t>(Exception::AdEL));
  }

  uint32_t temp;
  const TickCount cycles = DoMemoryAccess<MemoryAccessType::Read, MemoryAccessSize::Word>(address, temp);
  if (cycles < 0)
    return static_cast<uint64_t>(-static_cast<int64_t>(Exception::DBE));

  g_state.pending_ticks += cycles;
  return static_cast<uint64_t>(temp);
}

uint32_t WriteMemoryByte(uint32_t address, uint32_t value)
{
  const TickCount cycles = DoMemoryAccess<MemoryAccessType::Write, MemoryAccessSize::Byte>(address, value);
  if (cycles < 0)
    return static_cast<uint32_t>(Exception::DBE);

  return 0;
}

uint32_t WriteMemoryHalfWord(uint32_t address, uint32_t value)
{
  if (!Common::IsAlignedPow2(address, 2))
  {
    g_state.cop0_regs.BadVaddr = address;
    return static_cast<uint32_t>(Exception::AdES);
  }

  const TickCount cycles = DoMemoryAccess<MemoryAccessType::Write, MemoryAccessSize::HalfWord>(address, value);
  if (cycles < 0)
    return static_cast<uint32_t>(Exception::DBE);

  return 0;
}

uint32_t WriteMemoryWord(uint32_t address, uint32_t value)
{
  if (!Common::IsAlignedPow2(address, 4))
  {
    g_state.cop0_regs.BadVaddr = address;
    return static_cast<uint32_t>(Exception::AdES);
  }

  const TickCount cycles = DoMemoryAccess<MemoryAccessType::Write, MemoryAccessSize::Word>(address, value);
  if (cycles < 0)
    return static_cast<uint32_t>(Exception::DBE);

  return 0;
}

uint32_t UncheckedReadMemoryByte(uint32_t address)
{
  uint32_t temp = 0;
  g_state.pending_ticks += DoMemoryAccess<MemoryAccessType::Read, MemoryAccessSize::Byte>(address, temp);
  return temp;
}

uint32_t UncheckedReadMemoryHalfWord(uint32_t address)
{
  uint32_t temp = 0;
  g_state.pending_ticks += DoMemoryAccess<MemoryAccessType::Read, MemoryAccessSize::HalfWord>(address, temp);
  return temp;
}

uint32_t UncheckedReadMemoryWord(uint32_t address)
{
  uint32_t temp = 0;
  g_state.pending_ticks += DoMemoryAccess<MemoryAccessType::Read, MemoryAccessSize::Word>(address, temp);
  return temp;
}

void UncheckedWriteMemoryByte(uint32_t address, uint32_t value)
{
  g_state.pending_ticks += DoMemoryAccess<MemoryAccessType::Write, MemoryAccessSize::Byte>(address, value);
}

void UncheckedWriteMemoryHalfWord(uint32_t address, uint32_t value)
{
  g_state.pending_ticks += DoMemoryAccess<MemoryAccessType::Write, MemoryAccessSize::HalfWord>(address, value);
}

void UncheckedWriteMemoryWord(uint32_t address, uint32_t value)
{
  g_state.pending_ticks += DoMemoryAccess<MemoryAccessType::Write, MemoryAccessSize::Word>(address, value);
}

} // namespace Recompiler::Thunks

} // namespace CPU

#ifdef _MSC_VER
#pragma warning(pop)
#endif
