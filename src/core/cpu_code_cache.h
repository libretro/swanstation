#pragma once
#include "bus.h"
#include "common/bitfield.h"
#include "common/jit_code_buffer.h"
#include "common/page_fault_handler.h"
#include "cpu_types.h"
#include <array>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

#ifdef WITH_RECOMPILER
#include "cpu_recompiler_types.h"
#endif

namespace CPU {

union CodeBlockKey
{
  uint32_t bits;

  BitField<uint32_t, bool, 0, 1> user_mode;
  BitField<uint32_t, uint32_t, 2, 30> aligned_pc;

  ALWAYS_INLINE uint32_t GetPC() const { return aligned_pc << 2; }
  ALWAYS_INLINE void SetPC(uint32_t pc) { aligned_pc = pc >> 2; }

  ALWAYS_INLINE uint32_t GetPCPhysicalAddress() const { return (aligned_pc << 2) & PHYSICAL_MEMORY_ADDRESS_MASK; }

  ALWAYS_INLINE CodeBlockKey() = default;
  ALWAYS_INLINE CodeBlockKey(const CodeBlockKey&) = default;

  ALWAYS_INLINE CodeBlockKey& operator=(const CodeBlockKey& rhs)
  {
    bits = rhs.bits;
    return *this;
  }
};

struct CodeBlockInstruction
{
  Instruction instruction;
  uint32_t pc;

  bool is_branch_instruction : 1;
  bool is_direct_branch_instruction : 1;
  bool is_unconditional_branch_instruction : 1;
  bool is_branch_delay_slot : 1;
  bool is_load_instruction : 1;
  bool is_store_instruction : 1;
  bool has_load_delay : 1;
};

struct CodeBlock
{
  using HostCodePointer = void (*)();

  struct LinkInfo
  {
    CodeBlock* block;
    void* host_pc;
    void* host_resolve_pc;
    uint32_t host_pc_size;
  };

  CodeBlock(const CodeBlockKey key_) : key(key_) {}

  CodeBlockKey key;
  uint32_t host_code_size = 0;
  HostCodePointer host_code = nullptr;

  std::vector<CodeBlockInstruction> instructions;
  std::vector<LinkInfo> link_predecessors;
  std::vector<LinkInfo> link_successors;

  TickCount uncached_fetch_ticks = 0;
  uint32_t icache_line_count = 0;

#ifdef WITH_RECOMPILER
  std::vector<Recompiler::LoadStoreBackpatchInfo> loadstore_backpatch_info;
#endif

  bool contains_loadstore_instructions = false;
  bool contains_double_branches = false;
  bool invalidated = false;
  bool can_link = true;

  uint32_t recompile_frame_number = 0;
  uint32_t recompile_count = 0;
  uint32_t invalidate_frame_number = 0;

  uint32_t GetPC() const { return key.GetPC(); }
  uint32_t GetSizeInBytes() const { return static_cast<uint32_t>(instructions.size()) * sizeof(Instruction); }
  uint32_t GetStartPageIndex() const { return (key.GetPCPhysicalAddress() / HOST_PAGE_SIZE); }
  uint32_t GetEndPageIndex() const { return ((key.GetPCPhysicalAddress() + GetSizeInBytes()) / HOST_PAGE_SIZE); }
  bool IsInRAM() const
  {
    // TODO: Constant
    return key.GetPCPhysicalAddress() < 0x200000;
  }
};

namespace CodeCache {

inline constexpr uint32_t FAST_MAP_TABLE_COUNT = 0x10000,
                     FAST_MAP_TABLE_SIZE = 0x10000 / 4, // 16384
  FAST_MAP_TABLE_SHIFT = 16;

using FastMapTable = CodeBlock::HostCodePointer*;

void Initialize();
void Shutdown();
void Execute();

#ifdef WITH_RECOMPILER
using DispatcherFunction = void (*)();
using SingleBlockDispatcherFunction = void(*)(const CodeBlock::HostCodePointer);

FastMapTable* GetFastMapPointer();
void ExecuteRecompiler();
#endif

/// Flushes the code cache, forcing all blocks to be recompiled.
void Flush();

/// Changes whether the recompiler is enabled.
void Reinitialize();

/// Invalidates all blocks which are in the range of the specified code page.
void InvalidateBlocksWithPageIndex(uint32_t page_index);

/// Invalidates all blocks in the cache.
void InvalidateAll();

template<PGXPMode pgxp_mode>
void InterpretCachedBlock(const CodeBlock& block);

template<PGXPMode pgxp_mode>
void InterpretUncachedBlock();

/// Invalidates any code pages which overlap the specified range.
ALWAYS_INLINE void InvalidateCodePages(PhysicalMemoryAddress address, uint32_t word_count)
{
  /* address is the lowest word written; the range may wrap at the end of RAM. */
  const uint32_t page_mask = Bus::g_ram_mask / HOST_PAGE_SIZE;
  const uint32_t start_page = (address & Bus::g_ram_mask) / HOST_PAGE_SIZE;
  uint32_t num_pages = ((address & HOST_PAGE_OFFSET_MASK) + word_count * sizeof(uint32_t) + HOST_PAGE_OFFSET_MASK) / HOST_PAGE_SIZE;
  if (num_pages > page_mask + 1)
    num_pages = page_mask + 1;
  for (uint32_t i = 0; i < num_pages; i++)
  {
    const uint32_t page = (start_page + i) & page_mask;
    if (Bus::m_ram_code_bits[page])
      CPU::CodeCache::InvalidateBlocksWithPageIndex(page);
  }
}

}; // namespace CodeCache

} // namespace CPU
