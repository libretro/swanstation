#pragma once
#include "common/timer.h"
#include "host_interface.h"
#include "settings.h"
#include "timing_event.h"
#include "types.h"
#include <memory>
#include <optional>
#include <string>

class ByteStream;
class CDImage;
class StateWrapper;

class Controller;

struct CheatCode;
class CheatList;

struct SystemBootParameters
{
  SystemBootParameters();
  SystemBootParameters(SystemBootParameters&& other);
  SystemBootParameters(std::string filename_);
  ~SystemBootParameters();

  std::string filename;
  std::optional<bool> override_fast_boot;
  std::unique_ptr<ByteStream> state_stream;
  uint32_t media_playlist_index = 0;
  bool load_image_to_ram = false;
  bool force_software_renderer = false;
};

namespace System {

// 5 megabytes is sufficient for now, at the moment they're around 4.3MB, or 10.3MB with 8MB RAM enabled.
inline constexpr uint32_t MAX_SAVE_STATE_SIZE = 11 * 1024 * 1024;

inline constexpr TickCount MASTER_CLOCK = 44100 * 0x300; // 33868800Hz or 33.8688MHz, also used as CPU clock

enum class State
{
  Shutdown,
  Starting,
  Running
};

extern TickCount g_ticks_per_second;

/// Returns the preferred console type for a disc.
ConsoleRegion GetConsoleRegionForDiscRegion(DiscRegion region);

std::string GetGameCodeForImage(CDImage* cdi, bool fallback_to_hash);
DiscRegion GetRegionForCode(std::string_view code);
DiscRegion GetRegionFromSystemArea(CDImage* cdi);
DiscRegion GetRegionForImage(CDImage* cdi);
DiscRegion GetRegionForExe(const char* path);
DiscRegion GetRegionForPsf(const char* path);

State GetState();
bool IsShutdown();
bool IsValid();

ConsoleRegion GetRegion();
bool IsPALRegion();

ALWAYS_INLINE_RELEASE TickCount ScaleTicksToOverclock(TickCount ticks)
{
  if (!g_settings.cpu_overclock_active)
    return ticks;

  return static_cast<TickCount>((static_cast<uint64_t>(static_cast<uint32_t>(ticks)) * g_settings.cpu_overclock_numerator) /
                                g_settings.cpu_overclock_denominator);
}

TickCount GetMaxSliceTicks();
void UpdateOverclock();

/// Injects a PS-EXE into memory at its specified load location. If patch_loader is set, the BIOS will be patched to
/// direct execution to this executable.
bool InjectEXEFromBuffer(const void* buffer, uint32_t buffer_size, bool patch_loader = true);

uint32_t GetFrameNumber();
void FrameDone();

const std::string& GetRunningCode();
float GetVerticalFrequency();

bool Boot(const SystemBootParameters& params);
void Reset();
void Shutdown();

bool LoadState(ByteStream* state, bool is_memory_state = false);
bool SaveState(ByteStream* state);

/// Recreates the GPU component, saving/loading the state so it is preserved. Call when the GPU renderer changes.
bool RecreateGPU(GPURenderer renderer, bool update_display = true);

void RunFrame();

/// Sets the emulated vertical refresh rate (Hz). This is reported to the
/// libretro frontend via retro_system_av_info.timing.fps so it can pace and
/// resample audio correctly; the core itself does no frame throttling - the
/// frontend drives pacing by calling retro_run().
void SetVerticalFrequency(float frequency);

// Access controllers for simulating input.
Controller* GetController(uint32_t slot);
void UpdateControllers();
void UpdateControllerSettings();
void ResetControllers();
void UpdateMemoryCardTypes();
void UpdateMultitaps(void);

bool HasMedia();
std::string GetMediaFileName();
bool InsertMedia(const char* path);
void RemoveMedia();
void SetMediaTrayOpen(bool opened);

/// Returns true if this is a multi-subimage image (e.g. m3u).
bool HasMediaSubImages();

/// Returns the number of entries in the media/disc playlist.
uint32_t GetMediaSubImageCount();

/// Returns the current image from the media/disc playlist.
uint32_t GetMediaSubImageIndex();

/// Returns the path to the specified playlist index.
std::string GetMediaSubImageTitle(uint32_t index);

/// Returns the sub-image path corresponding to the specified playlist index.
std::string GetMediaSubImagePath(uint32_t index);

/// Switches to the specified media/disc playlist index.
bool SwitchMediaSubImage(uint32_t index);

/// Accesses the current cheat list.
CheatList* GetCheatList();

/// Sets or clears the provided cheat list, applying every frame.
void SetCheatList(std::unique_ptr<CheatList> cheats);

//////////////////////////////////////////////////////////////////////////
// Memory Save States (Runahead)
//////////////////////////////////////////////////////////////////////////
void ClearMemorySaveStates();
void UpdateMemorySaveStateSettings();
void SetRunaheadReplayFlag();

} // namespace System
