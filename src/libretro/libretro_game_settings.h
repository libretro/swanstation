#pragma once
#include "core/types.h"
#include <bitset>
#include <optional>
#include <string>
#include <memory>

class ByteStream;

namespace GameSettings {
enum class Trait : u32
{
  ForceInterpreter,
  ForceSoftwareRenderer,
  ForceSoftwareRendererForReadbacks,
  ForceInterlacing,
  DisableTrueColor,
  DisableUpscaling,
  DisableAnalogModeForcing,
  DisableScaledDithering,
  DisableForceNTSCTimings,
  DisableWidescreen,
  DisablePGXP,
  DisablePGXPCulling,
  DisablePGXPTextureCorrection,
  DisablePGXPColorCorrection,
  DisablePGXPDepthBuffer,
  ForcePGXPVertexCache,
  ForcePGXPCPUMode,
  ForceRecompilerMemoryExceptions,
  ForceRecompilerICache,
  ForceRecompilerLUTFastmem,
  ForceOldAudioHook,

  Count
};

struct Entry
{
  std::bitset<static_cast<int>(Trait::Count)> traits{};
  std::optional<u32> dma_max_slice_ticks;
  std::optional<u32> dma_halt_ticks;
  std::optional<u32> gpu_fifo_size;
  std::optional<u32> gpu_max_run_ahead;
  std::optional<float> gpu_pgxp_tolerance;
  std::optional<float> gpu_pgxp_depth_threshold;

  ALWAYS_INLINE bool HasTrait(Trait trait) const { return traits[static_cast<int>(trait)]; }
  ALWAYS_INLINE void AddTrait(Trait trait) { traits[static_cast<int>(trait)] = true; }

  void ApplySettings(bool display_osd_messages) const;
};

}; // namespace GameSettings

std::unique_ptr<GameSettings::Entry> GetSettingsForGame(const std::string& game_code);
