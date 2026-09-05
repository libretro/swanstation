#include "system.h"
#include "bios.h"
#include "bus.h"
#include "cdrom.h"
#include "cheats.h"
#include "libretro/libretro_audio_stream.h"
#include "common/error.h"
#include "common/file_system.h"
#include "common/iso_reader.h"
#include "common/log.h"
#include "common/make_array.h"
#include "common/state_wrapper.h"
#include "common/string_util.h"
#include "controller.h"
#include "cpu_code_cache.h"
#include "cpu_core.h"
#include "dma.h"
#include "gpu.h"
#include "gte.h"
#include "host_display.h"
#include "host_interface.h"
#include "host_interface_progress_callback.h"
#include "interrupt_controller.h"
#include "libcrypt_game_codes.h"
#include "mdec.h"
#include "memory_card.h"
#include "multitap.h"
#include "openbios.bin.h"
#include "pad.h"
#include "pgxp.h"
#include "psf_loader.h"
#include "save_state_version.h"
#include "sio.h"
#include "spu.h"
#include "texture_replacements.h"
#include "timers.h"
#include "xxhash.h"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <limits>
#include <thread>

#include <compat/strl.h>
#include <file/file_path.h>

Log_SetChannel(System);

SystemBootParameters::SystemBootParameters() = default;

SystemBootParameters::SystemBootParameters(SystemBootParameters&& other) = default;

SystemBootParameters::SystemBootParameters(std::string filename_) : filename(std::move(filename_)) {}

SystemBootParameters::~SystemBootParameters() = default;

namespace System {

struct MemorySaveState
{
  std::unique_ptr<HostDisplayTexture> vram_texture;
  std::unique_ptr<GrowableMemoryByteStream> state_stream;
};

static bool SaveMemoryState(MemorySaveState* mss);
static bool LoadMemoryState(const MemorySaveState& mss);

static bool LoadEXE(const char* filename);

/// Opens CD image, preloading if needed.
static std::unique_ptr<CDImage> OpenCDImage(const char* path, Common::Error* error, bool force_preload,
                                            bool check_for_patches);
static bool ReadExecutableFromImage(ISOReader& iso, std::string* out_executable_name, std::vector<uint8_t>* out_executable_data);
static bool ReadExecutableFromImage(CDImage* cdi, std::string* out_executable_name, std::vector<uint8_t>* out_executable_data);
static bool ShouldCheckForImagePatches();
static std::string GetGameHashCodeForImage(CDImage* cdi);
static std::string GetExecutableNameForImage(CDImage* cdi);
static void UpdatePerGameMemoryCards();

static bool DoLoadState(ByteStream* stream, bool force_software_renderer, bool update_display, bool is_memory_state);
static bool DoState(StateWrapper& sw, HostDisplayTexture** host_texture, bool update_display, bool is_memory_state);
static void DoRunFrame();
static bool CreateGPU(GPURenderer renderer);

static void SaveRunaheadState();
static void DoRunahead();

static bool Initialize(bool force_software_renderer);

static void UpdateRunningGame(const char* path, CDImage* image);
static bool CheckForSBIFile(CDImage* image);

static State s_state = State::Shutdown;

static ConsoleRegion s_region = ConsoleRegion::NTSC_U;
TickCount g_ticks_per_second = MASTER_CLOCK;
static TickCount s_max_slice_ticks = MASTER_CLOCK / 10;
static uint32_t s_frame_number = 1;
static uint32_t s_internal_frame_number = 0;

static std::string s_running_game_path;
static std::string s_running_game_code;
static std::string s_running_game_title;

static float s_vertical_frequency = 60.0f;

static std::unique_ptr<CheatList> s_cheat_list;

static std::deque<MemorySaveState> s_runahead_states;
static bool s_runahead_replay_pending = false;
static uint32_t s_runahead_frames = 0;

State GetState()
{
  return s_state;
}

bool IsShutdown()
{
  return s_state == State::Shutdown;
}

bool IsValid()
{
  return s_state != State::Shutdown && s_state != State::Starting;
}

ConsoleRegion GetRegion()
{
  return s_region;
}

bool IsPALRegion()
{
  return s_region == ConsoleRegion::PAL;
}

TickCount GetMaxSliceTicks(void)
{
  return s_max_slice_ticks;
}

void UpdateOverclock(void)
{
  g_ticks_per_second = ScaleTicksToOverclock(MASTER_CLOCK);
  s_max_slice_ticks = ScaleTicksToOverclock(MASTER_CLOCK / 10);
  g_spu.CPUClockChanged();
  g_cdrom.CPUClockChanged();
  g_gpu->CPUClockChanged();
  g_timers.CPUClocksChanged();
}

uint32_t GetFrameNumber(void)
{
  return s_frame_number;
}

void FrameDone()
{
  s_frame_number++;
  CPU::g_state.frame_done = true;
  CPU::g_state.downcount = 0;
}

const std::string& GetRunningCode()
{
  return s_running_game_code;
}

float GetVerticalFrequency()
{
  return s_vertical_frequency;
}

/// Returns true if the filename is a PlayStation executable we can inject.
static bool IsExeFileName(const char* path)
{
  const char* extension = std::strrchr(path, '.');
  return (extension &&
          (StringUtil::Strcasecmp(extension, ".exe") == 0 || StringUtil::Strcasecmp(extension, ".psexe") == 0));
}

/// Returns true if the filename is a Portable Sound Format file we can uncompress/load.
static bool IsPsfFileName(const char* path)
{
  const char* extension = std::strrchr(path, '.');
  return (extension &&
          (StringUtil::Strcasecmp(extension, ".psf") == 0 || StringUtil::Strcasecmp(extension, ".minipsf") == 0));
}

ConsoleRegion GetConsoleRegionForDiscRegion(DiscRegion region)
{
  switch (region)
  {
    case DiscRegion::NTSC_J:
      return ConsoleRegion::NTSC_J;
    case DiscRegion::PAL:
      return ConsoleRegion::PAL;
    case DiscRegion::NTSC_U:
    case DiscRegion::Other:
    default:
      break;

  }

  return ConsoleRegion::NTSC_U;
}

std::string GetGameCodeForImage(CDImage* cdi, bool fallback_to_hash)
{
  std::string code(GetExecutableNameForImage(cdi));
  if (!code.empty())
  {
    // SCES_123.45 -> SCES-12345
    for (std::string::size_type pos = 0; pos < code.size();)
    {
      if (code[pos] == '.')
      {
        code.erase(pos, 1);
        continue;
      }

      if (code[pos] == '_')
        code[pos] = '-';
      else
        code[pos] = static_cast<char>(std::toupper(code[pos]));

      pos++;
    }

    return code;
  }

  if (!fallback_to_hash)
    return {};

  return GetGameHashCodeForImage(cdi);
}

static std::string GetGameHashCodeForImage(CDImage* cdi)
{
  ISOReader iso;
  if (!iso.Open(cdi, 1))
    return {};

  std::string exe_name;
  std::vector<uint8_t> exe_buffer;
  if (!ReadExecutableFromImage(cdi, &exe_name, &exe_buffer))
    return {};

  const uint32_t track_1_length = cdi->GetTrackLength(1);

  XXH64_state_t* state = XXH64_createState();
  XXH64_reset(state, 0x4242D00C);
  XXH64_update(state, exe_name.c_str(), exe_name.size());
  XXH64_update(state, exe_buffer.data(), exe_buffer.size());
  XXH64_update(state, &iso.GetPVD(), sizeof(ISOReader::ISOPrimaryVolumeDescriptor));
  XXH64_update(state, &track_1_length, sizeof(track_1_length));
  const uint64_t hash = XXH64_digest(state);
  XXH64_freeState(state);
  return StringUtil::StdStringFromFormat("HASH-%" PRIX64, hash);
}

static std::string GetExecutableNameForImage(ISOReader& iso, bool strip_subdirectories)
{
  // Read SYSTEM.CNF
  std::vector<uint8_t> system_cnf_data;
  if (!iso.ReadFile("SYSTEM.CNF", &system_cnf_data))
    return {};

  // Parse lines
  std::vector<std::pair<std::string, std::string>> lines;
  std::pair<std::string, std::string> current_line;
  bool reading_value = false;
  for (size_t pos = 0; pos < system_cnf_data.size(); pos++)
  {
    const char ch = static_cast<char>(system_cnf_data[pos]);
    if (ch == '\r' || ch == '\n')
    {
      if (!current_line.first.empty())
      {
        lines.push_back(std::move(current_line));
        current_line = {};
        reading_value = false;
      }
    }
    else if (ch == ' ' || (ch >= 0x09 && ch <= 0x0D))
    {
      continue;
    }
    else if (ch == '=' && !reading_value)
    {
      reading_value = true;
    }
    else
    {
      if (reading_value)
        current_line.second.push_back(ch);
      else
        current_line.first.push_back(ch);
    }
  }

  if (!current_line.first.empty())
    lines.push_back(std::move(current_line));

  // Find the BOOT line
  auto iter = std::find_if(lines.begin(), lines.end(),
                           [](const auto& it) { return StringUtil::Strcasecmp(it.first.c_str(), "boot") == 0; });
  if (iter == lines.end())
    return {};

  std::string code = iter->second;
  std::string::size_type pos;
  if (strip_subdirectories)
  {
    // cdrom:\SCES_123.45;1
    pos = code.rfind('\\');
    if (pos != std::string::npos)
    {
      code.erase(0, pos + 1);
    }
    else
    {
      // cdrom:SCES_123.45;1
      pos = code.rfind(':');
      if (pos != std::string::npos)
        code.erase(0, pos + 1);
    }
  }
  else
  {
    if (code.compare(0, 6, "cdrom:") == 0)
      code.erase(0, 6);
    else
      Log_WarningPrintf("Unknown prefix in executable path: '%s'", code.c_str());

    // remove leading slashes
    while (code[0] == '/' || code[0] == '\\')
      code.erase(0, 1);
  }

  // strip off ; or version number
  pos = code.rfind(';');
  if (pos != std::string::npos)
    code.erase(pos);

  return code;
}

static std::string GetExecutableNameForImage(CDImage* cdi)
{
  ISOReader iso;
  if (!iso.Open(cdi, 1))
    return {};

  return GetExecutableNameForImage(iso, true);
}

static bool ReadExecutableFromImage(ISOReader& iso, std::string* out_executable_name, std::vector<uint8_t>* out_executable_data)
{
  bool result = false;

  std::string executable_path(GetExecutableNameForImage(iso, false));
  if (!executable_path.empty())
  {
    result = iso.ReadFile(executable_path.c_str(), out_executable_data);
    if (!result)
      Log_ErrorPrintf("Failed to read executable '%s' from disc", executable_path.c_str());
  }

  if (!result)
  {
    // fallback to PSX.EXE
    executable_path = "PSX.EXE";
    result = iso.ReadFile(executable_path.c_str(), out_executable_data);
    if (!result)
      Log_ErrorPrint("Failed to read fallback PSX.EXE from disc");
  }

  if (!result)
    return false;

  if (out_executable_name)
    *out_executable_name = std::move(executable_path);

  return true;
}

static bool ReadExecutableFromImage(CDImage* cdi, std::string* out_executable_name, std::vector<uint8_t>* out_executable_data)
{
  ISOReader iso;
  if (!iso.Open(cdi, 1))
    return false;

  return ReadExecutableFromImage(iso, out_executable_name, out_executable_data);
}

DiscRegion GetRegionForCode(std::string_view code)
{
  std::string prefix;
  for (size_t pos = 0; pos < code.length(); pos++)
  {
    const int ch = std::tolower(code[pos]);
    if (ch < 'a' || ch > 'z')
      break;

    prefix.push_back(static_cast<char>(ch));
  }

  if (prefix == "sces" || prefix == "sced" || prefix == "sles" || prefix == "sled")
    return DiscRegion::PAL;
  else if (prefix == "scps" || prefix == "slps" || prefix == "slpm" || prefix == "sczs" || prefix == "papx")
    return DiscRegion::NTSC_J;
  else if (prefix == "scus" || prefix == "slus")
    return DiscRegion::NTSC_U;
  else
    return DiscRegion::Other;
}

DiscRegion GetRegionFromSystemArea(CDImage* cdi)
{
  // The license code is on sector 4 of the disc.
  uint8_t sector[CDImage::DATA_SECTOR_SIZE];
  if (!cdi->Seek(1, 4) || cdi->Read(CDImage::ReadMode::DataOnly, 1, sector) != 1)
    return DiscRegion::Other;

  static constexpr char ntsc_u_string[] = "          Licensed  by          Sony Computer Entertainment Amer  ica ";
  static constexpr char ntsc_j_string[] = "          Licensed  by          Sony Computer Entertainment Inc.";
  static constexpr char pal_string[] = "          Licensed  by          Sony Computer Entertainment Euro pe";

  // subtract one for the terminating null
  if (std::equal(ntsc_u_string, ntsc_u_string + countof(ntsc_u_string) - 1, sector))
    return DiscRegion::NTSC_U;
  else if (std::equal(ntsc_j_string, ntsc_j_string + countof(ntsc_j_string) - 1, sector))
    return DiscRegion::NTSC_J;
  else if (std::equal(pal_string, pal_string + countof(pal_string) - 1, sector))
    return DiscRegion::PAL;
  else
    return DiscRegion::Other;
}

DiscRegion GetRegionForImage(CDImage* cdi)
{
  DiscRegion system_area_region = GetRegionFromSystemArea(cdi);
  if (system_area_region != DiscRegion::Other)
    return system_area_region;

  std::string code = GetGameCodeForImage(cdi, false);
  if (code.empty())
    return DiscRegion::Other;

  return GetRegionForCode(code);
}

DiscRegion GetRegionForExe(const char* path)
{
  BIOS::PSEXEHeader header;
  RFILE *fp = FileSystem::OpenRFile(path, "rb");
  if (!fp)
    return DiscRegion::Other;
  if (rfread(&header, sizeof(header), 1, fp) != 1)
  {
    filestream_close(fp);
    return DiscRegion::Other;
  }

  filestream_close(fp);
  return BIOS::GetPSExeDiscRegion(header);
}

DiscRegion GetRegionForPsf(const char* path)
{
  PSFLoader::File psf;
  if (!psf.Load(path))
    return DiscRegion::Other;

  return psf.GetRegion();
}

bool RecreateGPU(GPURenderer renderer, bool update_display /* = true*/)
{
  ClearMemorySaveStates();
  g_gpu->RestoreGraphicsAPIState();

  // save current state
  std::unique_ptr<ByteStream> state_stream = ByteStream_CreateGrowableMemoryStream();
  StateWrapper sw(state_stream.get(), StateWrapper::Mode::Write, SAVE_STATE_VERSION);
  const bool state_valid = g_gpu->DoState(sw, nullptr, false) && TimingEvents::DoState(sw);
  if (!state_valid)
    Log_ErrorPrintf("Failed to save old GPU state when switching renderers");

  g_gpu->ResetGraphicsAPIState();

  // create new renderer
  g_gpu.reset();
  if (!CreateGPU(renderer))
  {
    g_host_interface->ReportError("Failed to recreate GPU.");
    System::Shutdown();
    return false;
  }

  if (state_valid)
  {
    state_stream->SeekAbsolute(0);
    sw.SetMode(StateWrapper::Mode::Read);
    g_gpu->RestoreGraphicsAPIState();
    g_gpu->DoState(sw, nullptr, update_display);
    TimingEvents::DoState(sw);
    g_gpu->ResetGraphicsAPIState();
  }

  return true;
}

std::unique_ptr<CDImage> OpenCDImage(const char* path, Common::Error* error, bool force_preload, bool check_for_patches)
{
  CDImage::OpenFlags open_flags = CDImage::OpenFlags::None;
  if (g_settings.cdrom_precache_chd && !g_settings.cdrom_load_image_to_ram)
    open_flags |= CDImage::OpenFlags::PreCache;

  std::unique_ptr<CDImage> media = CDImage::Open(path, open_flags, error);
  if (!media)
    return {};

  if (force_preload || g_settings.cdrom_load_image_to_ram)
  {
    if (media->HasSubImages() && media->GetSubImageCount() > 1)
    {
      Log_WarningPrintf("CD image preloading not available for multi-disc image '%s'",
                        FileSystem::GetDisplayNameFromPath(media->GetFileName()).c_str());
    }
    else
    {
      HostInterfaceProgressCallback callback;
      std::unique_ptr<CDImage> memory_image = CDImage::CreateMemoryImage(media.get(), &callback);
      if (memory_image)
        media = std::move(memory_image);
      else
        Log_WarningPrintf("Failed to preload image '%s' to RAM", path);
    }
  }

  if (check_for_patches)
  {
    const std::string ppf_filename(FileSystem::BuildRelativePath(
      path, FileSystem::ReplaceExtension(FileSystem::GetDisplayNameFromPath(path), "ppf")));
    if (!ppf_filename.empty() && path_is_valid(ppf_filename.c_str()))
    {
      media = CDImage::OverlayPPFPatch(ppf_filename.c_str(), CDImage::OpenFlags::None, std::move(media));
      if (!media)
      {
        Log_WarningPrintf("Failed to apply ppf patch from '%s', using unpatched image.",
                          ppf_filename.c_str());
        return OpenCDImage(path, error, force_preload, false);
      }
    }
  }

  return media;
}

bool ShouldCheckForImagePatches()
{
  return g_host_interface->GetBoolSettingValue("CDROM", "LoadImagePatches", false);
}

bool Boot(const SystemBootParameters& params)
{
  s_state = State::Starting;
  s_region = g_settings.region;

  if (params.state_stream)
  {
    if (!DoLoadState(params.state_stream.get(), params.force_software_renderer, true, false))
    {
      Shutdown();
      return false;
    }

    return true;
  }

  // Load CD image up and detect region.
  Common::Error error;
  std::unique_ptr<CDImage> media;
  bool exe_boot = false;
  bool psf_boot = false;
  if (!params.filename.empty())
  {
    exe_boot = IsExeFileName(params.filename.c_str());
    psf_boot = (!exe_boot && IsPsfFileName(params.filename.c_str()));
    if (exe_boot || psf_boot)
    {
      if (s_region == ConsoleRegion::Auto)
      {
        const DiscRegion file_region =
          (exe_boot ? GetRegionForExe(params.filename.c_str()) : GetRegionForPsf(params.filename.c_str()));
        Log_InfoPrintf("EXE/PSF Region: %s", Settings::GetDiscRegionDisplayName(file_region));
        s_region = GetConsoleRegionForDiscRegion(file_region);
      }
    }
    else
    {
      Log_InfoPrintf("Loading CD image '%s'...", params.filename.c_str());
      media = OpenCDImage(params.filename.c_str(), &error, params.load_image_to_ram, ShouldCheckForImagePatches());
      if (!media)
      {
        // Couldn't open the image (corrupt, unsupported container, non-PSX
        // disc, etc). Rather than refusing to start, fall back to booting the
        // BIOS with no media inserted - the same as starting with no content.
        // The failure is still surfaced as a warning so it isn't silent.
        Log_WarningPrintf("Failed to load CD image '%s': %s. Booting BIOS with no disc.",
                          params.filename.c_str(), error.GetCodeAndMessage().GetCharArray());
        if (s_region == ConsoleRegion::Auto)
          s_region = ConsoleRegion::NTSC_U;
      }
      else if (s_region == ConsoleRegion::Auto)
      {
        const DiscRegion disc_region = GetRegionForImage(media.get());
        if (disc_region != DiscRegion::Other)
        {
          s_region = GetConsoleRegionForDiscRegion(disc_region);
          Log_InfoPrintf("Auto-detected console %s region for '%s' (region %s)",
                         Settings::GetConsoleRegionName(s_region), params.filename.c_str(),
                         Settings::GetDiscRegionName(disc_region));
        }
        else
        {
          s_region = ConsoleRegion::NTSC_U;
          Log_WarningPrintf("Could not determine console region for disc region %s. Defaulting to %s.",
                            Settings::GetDiscRegionName(disc_region), Settings::GetConsoleRegionName(s_region));
        }
      }
    }
  }
  else
  {
    // Default to NTSC for BIOS boot.
    if (s_region == ConsoleRegion::Auto)
      s_region = ConsoleRegion::NTSC_U;
  }

  Log_InfoPrintf("Console Region: %s", Settings::GetConsoleRegionDisplayName(s_region));

  // Load BIOS image.
  std::optional<BIOS::Image> bios_image = g_host_interface->GetBIOSImage(s_region);

  // Notify change of disc.
  UpdateRunningGame(media ? media->GetFileName().c_str() : params.filename.c_str(), media.get());

  // Check for SBI.
  if (!CheckForSBIFile(media.get()))
  {
    Shutdown();
    return false;
  }

  // Switch subimage.
  if (media && params.media_playlist_index != 0 && !media->SwitchSubImage(params.media_playlist_index, &error))
  {
    g_host_interface->ReportFormattedError("Failed to switch to subimage %u in '%s': %s", params.media_playlist_index,
                                           params.filename.c_str(), error.GetCodeAndMessage().GetCharArray());
    Shutdown();
    return false;
  }

  // Component setup.
  if (!Initialize(params.force_software_renderer))
  {
    Shutdown();
    return false;
  }

  // Load built-in copy of OpenBIOS if no BIOS could be found.
  if (bios_image)
    Bus::SetBIOS(bios_image->data(), bios_image->size());
  else
    Bus::SetBIOS(openbios, sizeof(openbios));

  UpdateControllers();
  UpdateMemoryCardTypes();
  UpdateMultitaps();
  Reset();

  const BIOS::Hash bios_hash = BIOS::GetHash(Bus::g_bios, Bus::BIOS_SIZE);

  // Load EXE late after BIOS.
  if (exe_boot && !LoadEXE(params.filename.c_str()))
  {
    g_host_interface->ReportFormattedError("Failed to load EXE file '%s'", params.filename.c_str());
    Shutdown();
    return false;
  }
  else if (psf_boot && !PSFLoader::Load(params.filename.c_str()))
  {
    g_host_interface->ReportFormattedError("Failed to load PSF file '%s'", params.filename.c_str());
    Shutdown();
    return false;
  }

  // Insert CD, and apply fastboot patch if enabled.
  if (media)
    g_cdrom.InsertMedia(std::move(media));
  if (g_cdrom.HasMedia() &&
      (params.override_fast_boot.has_value() ? params.override_fast_boot.value() : g_settings.bios_patch_fast_boot))
  {
    BIOS::PatchBIOSFastBoot(Bus::g_bios, Bus::BIOS_SIZE, bios_hash);
  }

  // Good to go.
  s_state = State::Running;
  return true;
}

bool Initialize(bool force_software_renderer)
{
  g_ticks_per_second = ScaleTicksToOverclock(MASTER_CLOCK);
  s_max_slice_ticks = ScaleTicksToOverclock(MASTER_CLOCK / 10);
  s_frame_number = 1;

  s_vertical_frequency = 60.0f;

  TimingEvents::Initialize();

  CPU::Initialize();

  if (!Bus::Initialize())
  {
    CPU::Shutdown();
    return false;
  }

  if (!CreateGPU(force_software_renderer ? GPURenderer::Software : g_settings.gpu_renderer))
  {
    Bus::Shutdown();
    CPU::Shutdown();
    return false;
  }

  if (g_settings.gpu_pgxp_enable)
    PGXP::Initialize();

  // CPU code cache must happen after GPU, because it might steal our address space.
  CPU::CodeCache::Initialize();

  g_dma.Initialize();
  g_interrupt_controller.Initialize();

  g_cdrom.Initialize();
  g_pad.Initialize();
  g_timers.Initialize();
  g_spu.Initialize();
  g_mdec.Initialize();
  g_sio.Initialize();

  UpdateMemorySaveStateSettings();
  return true;
}

void Shutdown()
{
  if (s_state == State::Shutdown)
    return;

  ClearMemorySaveStates();

  g_texture_replacements.Shutdown();

  g_sio.Shutdown();
  g_mdec.Shutdown();
  g_spu.Shutdown();
  g_timers.Shutdown();
  g_pad.Shutdown();
  g_cdrom.Shutdown();
  g_gpu.reset();
  g_interrupt_controller.Shutdown();
  g_dma.Shutdown();
  PGXP::Shutdown();
  CPU::CodeCache::Shutdown();
  Bus::Shutdown();
  CPU::Shutdown();
  s_running_game_code.clear();
  s_running_game_path.clear();
  s_running_game_title.clear();
  s_cheat_list.reset();
  // Wipe the cheat scratch register file so leftover state from this
  // session can't leak into the next game's cheats. This complements the
  // reset in SetCheatList, which catches the path where a frontend
  // explicitly resets cheats (e.g. libretro's retro_cheat_reset) without
  // tearing the system down first.
  CheatList::ResetSharedScratchRegisters();
  s_state = State::Shutdown;

  g_host_interface->OnRunningGameChanged(s_running_game_path, nullptr, s_running_game_code, s_running_game_title);
}

bool CreateGPU(GPURenderer renderer)
{
  switch (renderer)
  {
    case GPURenderer::HardwareOpenGL:
      g_gpu = GPU::CreateHardwareOpenGLRenderer();
      break;

    case GPURenderer::HardwareVulkan:
      g_gpu = GPU::CreateHardwareVulkanRenderer();
      break;

#ifdef _WIN32
    case GPURenderer::HardwareD3D11:
      g_gpu = GPU::CreateHardwareD3D11Renderer();
      break;
#ifdef USE_D3D12
    case GPURenderer::HardwareD3D12:
      g_gpu = GPU::CreateHardwareD3D12Renderer();
      break;
#endif
#endif

    case GPURenderer::Software:
    default:
      g_gpu = GPU::CreateSoftwareRenderer();
      break;
  }

  if (!g_gpu || !g_gpu->Initialize(g_host_interface->GetDisplay()))
  {
    Log_ErrorPrintf("Failed to initialize %s renderer, falling back to software renderer",
                    Settings::GetRendererName(renderer));
    g_host_interface->AddFormattedOSDMessage(
      30.0f,
      g_host_interface->TranslateString("OSDMessage",
                                        "Failed to initialize %s renderer, falling back to software renderer."),
      Settings::GetRendererName(renderer));
    g_gpu.reset();
    g_gpu = GPU::CreateSoftwareRenderer();
    if (!g_gpu->Initialize(g_host_interface->GetDisplay()))
      return false;
  }

  return true;
}

bool DoState(StateWrapper& sw, HostDisplayTexture** host_texture, bool update_display, bool is_memory_state)
{
  if (!sw.DoMarker("System"))
    return false;

  sw.Do(&s_region);
  sw.Do(&s_frame_number);
  sw.Do(&s_internal_frame_number);

  if (!sw.DoMarker("CPU") || !CPU::DoState(sw))
    return false;

  if (sw.IsReading())
  {
    if (is_memory_state)
      CPU::CodeCache::InvalidateAll();
    else
      CPU::CodeCache::Flush();
  }

  // only reset pgxp if we're not runahead-rollbacking. the value checks will save us from broken rendering, and it
  // saves using imprecise values for a frame in 30fps games.
  if (sw.IsReading() && g_settings.gpu_pgxp_enable && !is_memory_state)
    PGXP::Reset();

  if (!sw.DoMarker("Bus") || !Bus::DoState(sw))
    return false;

  if (!sw.DoMarker("DMA") || !g_dma.DoState(sw))
    return false;

  if (!sw.DoMarker("InterruptController") || !g_interrupt_controller.DoState(sw))
    return false;

  g_gpu->RestoreGraphicsAPIState();
  const bool gpu_result = sw.DoMarker("GPU") && g_gpu->DoState(sw, host_texture, update_display);
  g_gpu->ResetGraphicsAPIState();
  if (!gpu_result)
    return false;

  if (!sw.DoMarker("CDROM") || !g_cdrom.DoState(sw))
    return false;

  if (!sw.DoMarker("Pad") || !g_pad.DoState(sw))
    return false;

  if (!sw.DoMarker("Timers") || !g_timers.DoState(sw))
    return false;

  if (!sw.DoMarker("SPU") || !g_spu.DoState(sw))
    return false;

  if (!sw.DoMarker("MDEC") || !g_mdec.DoState(sw))
    return false;

  if (!sw.DoMarker("SIO") || !g_sio.DoState(sw))
    return false;

  if (!sw.DoMarker("Events") || !TimingEvents::DoState(sw))
    return false;

  if (!sw.DoMarker("Overclock"))
    return false;

  bool cpu_overclock_active = g_settings.cpu_overclock_active;
  uint32_t cpu_overclock_numerator = g_settings.cpu_overclock_numerator;
  uint32_t cpu_overclock_denominator = g_settings.cpu_overclock_denominator;
  sw.Do(&cpu_overclock_active);
  sw.Do(&cpu_overclock_numerator);
  sw.Do(&cpu_overclock_denominator);

  if (sw.IsReading() && (cpu_overclock_active != g_settings.cpu_overclock_active ||
                         (cpu_overclock_active && (g_settings.cpu_overclock_numerator != cpu_overclock_numerator ||
                                                   g_settings.cpu_overclock_denominator != cpu_overclock_denominator))))
  {
    UpdateOverclock();
  }

  return !sw.HasError();
}

void Reset()
{
  if (IsShutdown())
    return;

  g_gpu->RestoreGraphicsAPIState();

  CPU::Reset();
  CPU::CodeCache::Flush();
  if (g_settings.gpu_pgxp_enable)
    PGXP::Initialize();

  Bus::Reset();
  g_dma.Reset();
  g_interrupt_controller.Reset();
  g_gpu->Reset(true);
  g_cdrom.Reset();
  g_pad.Reset();
  g_timers.Reset();
  g_spu.Reset();
  g_mdec.Reset();
  g_sio.Reset();
  s_frame_number = 1;
  TimingEvents::Reset();

  g_gpu->ResetGraphicsAPIState();
}

bool LoadState(ByteStream* state, bool is_memory_state)
{
  if (IsShutdown())
    return false;

  return DoLoadState(state, false, false, is_memory_state);
}

bool DoLoadState(ByteStream* state, bool force_software_renderer, bool update_display, bool is_memory_state)
{
  SAVE_STATE_HEADER header;
  if (!state->Read2(&header, sizeof(header)))
    return false;

  if (header.magic != SAVE_STATE_MAGIC)
    return false;

  if (header.version < SAVE_STATE_MINIMUM_VERSION)
  {
    g_host_interface->ReportFormattedError(
      g_host_interface->TranslateString("System",
                                        "Save state is incompatible: minimum version is %u but state is version %u."),
      SAVE_STATE_MINIMUM_VERSION, header.version);
    return false;
  }

  if (header.version > SAVE_STATE_VERSION)
  {
    g_host_interface->ReportFormattedError(
      g_host_interface->TranslateString("System",
                                        "Save state is incompatible: maximum version is %u but state is version %u."),
      SAVE_STATE_VERSION, header.version);
    return false;
  }

  Common::Error error;
  std::string media_filename;
  std::unique_ptr<CDImage> media;
  if (header.media_filename_length > 0)
  {
    media_filename.resize(header.media_filename_length);
    if (!state->SeekAbsolute(header.offset_to_media_filename) ||
        !state->Read2(media_filename.data(), header.media_filename_length))
    {
      return false;
    }

    std::unique_ptr<CDImage> old_media = g_cdrom.RemoveMedia(false);
    if (old_media && old_media->GetFileName() == media_filename)
    {
      Log_InfoPrintf("Re-using same media '%s'", media_filename.c_str());
      media = std::move(old_media);
    }
    else
    {
      media = OpenCDImage(media_filename.c_str(), &error, false, ShouldCheckForImagePatches());
      if (!media)
      {
        if (old_media)
        {
          Log_InfoPrintf("Failed to open CD image from save state '%s': %s. Using existing image '%s', this may result in instability.",
                         media_filename.c_str(), error.GetCodeAndMessage().GetCharArray(), old_media->GetFileName().c_str());
          media = std::move(old_media);
        }
        else
        {
          g_host_interface->ReportFormattedError(
            g_host_interface->TranslateString("System", "Failed to open CD image '%s' used by save state: %s."),
            media_filename.c_str(), error.GetCodeAndMessage().GetCharArray());
          return false;
        }
      }
    }
  }

  UpdateRunningGame(media_filename.c_str(), media.get());

  if (media && header.version >= 51)
  {
    const uint32_t num_subimages = media->HasSubImages() ? media->GetSubImageCount() : 1;
    if (header.media_subimage_index >= num_subimages ||
        (media->HasSubImages() && media->GetCurrentSubImage() != header.media_subimage_index &&
         !media->SwitchSubImage(header.media_subimage_index, &error)))
    {
      g_host_interface->ReportFormattedError(
        g_host_interface->TranslateString("System",
                                          "Failed to switch to subimage %u in CD image '%s' used by save state: %s."),
        header.media_subimage_index + 1u, media_filename.c_str(), error.GetCodeAndMessage().GetCharArray());
      return false;
    }
    else
    {
      Log_InfoPrintf("Switched to subimage %u in '%s'", header.media_subimage_index, media_filename.c_str());
    }
  }

  ClearMemorySaveStates();

  if (s_state == State::Starting)
  {
    if (!Initialize(force_software_renderer))
      return false;

    if (media)
      g_cdrom.InsertMedia(std::move(media));

    UpdateControllers();
    UpdateMemoryCardTypes();
    UpdateMultitaps();
    Reset();
  }
  else
  {
    g_cdrom.Reset();
    if (media)
      g_cdrom.InsertMedia(std::move(media));
    else
      g_cdrom.RemoveMedia(false);

    // ensure the correct card is loaded
    if (g_settings.HasAnyPerGameMemoryCards())
      UpdatePerGameMemoryCards();
  }

  if (header.data_compression_type != 0)
  {
    g_host_interface->ReportFormattedError("Unknown save state compression type %u", header.data_compression_type);
    return false;
  }

  if (!state->SeekAbsolute(header.offset_to_data))
    return false;

  StateWrapper sw(state, StateWrapper::Mode::Read, header.version);
  if (!DoState(sw, nullptr, update_display, is_memory_state))
    return false;

  if (s_state == State::Starting)
    s_state = State::Running;

  return true;
}

bool SaveState(ByteStream* state)
{
  if (IsShutdown())
    return false;

  SAVE_STATE_HEADER header = {};

  const uint64_t header_position = state->GetPosition();
  if (!state->Write2(&header, sizeof(header)))
    return false;

  // fill in header
  header.magic = SAVE_STATE_MAGIC;
  header.version = SAVE_STATE_VERSION;
  strlcpy(header.title, s_running_game_title.c_str(), sizeof(header.title));
  strlcpy(header.game_code, s_running_game_code.c_str(), sizeof(header.game_code));

  if (g_cdrom.HasMedia())
  {
    const std::string& media_filename = g_cdrom.GetMediaFileName();
    header.offset_to_media_filename = static_cast<uint32_t>(state->GetPosition());
    header.media_filename_length = static_cast<uint32_t>(media_filename.length());
    header.media_subimage_index = g_cdrom.GetMedia()->HasSubImages() ? g_cdrom.GetMedia()->GetCurrentSubImage() : 0;
    if (!media_filename.empty() && !state->Write2(media_filename.data(), header.media_filename_length))
      return false;
  }

  // write data
  {
    header.offset_to_data = static_cast<uint32_t>(state->GetPosition());

    g_gpu->RestoreGraphicsAPIState();

    StateWrapper sw(state, StateWrapper::Mode::Write, SAVE_STATE_VERSION);
    const bool result = DoState(sw, nullptr, false, false);

    g_gpu->ResetGraphicsAPIState();

    if (!result)
      return false;

    header.data_compression_type = 0;
    header.data_uncompressed_size = static_cast<uint32_t>(state->GetPosition() - header.offset_to_data);
  }

  // re-write header
  const uint64_t end_position = state->GetPosition();
  if (!state->SeekAbsolute(header_position) || !state->Write2(&header, sizeof(header)) ||
      !state->SeekAbsolute(end_position))
  {
    return false;
  }

  return true;
}

void DoRunFrame()
{
  g_gpu->RestoreGraphicsAPIState();

  if (CPU::g_state.use_debug_dispatcher)
  {
    CPU::ExecuteDebug();
  }
  else
  {
    switch (g_settings.cpu_execution_mode)
    {
      case CPUExecutionMode::Recompiler:
#ifdef WITH_RECOMPILER
        CPU::CodeCache::ExecuteRecompiler();
#else
        CPU::CodeCache::Execute();
#endif
        break;

      case CPUExecutionMode::CachedInterpreter:
        CPU::CodeCache::Execute();
        break;

      case CPUExecutionMode::Interpreter:
      default:
        CPU::Execute();
        break;
    }
  }

  // Generate any pending samples from the SPU before sleeping, this way we reduce the chances of underruns.
  g_spu.GeneratePendingSamples();

  if (s_cheat_list)
    s_cheat_list->Apply();

  g_gpu->ResetGraphicsAPIState();
}

void RunFrame()
{
  if (s_runahead_frames > 0)
    DoRunahead();

  DoRunFrame();
}

void SetVerticalFrequency(float frequency)
{
  s_vertical_frequency = frequency;
}

static bool LoadEXEToRAM(const char* filename, bool patch_bios)
{
  RFILE* fp = FileSystem::OpenRFile(filename, "rb");
  if (!fp)
  {
    Log_ErrorPrintf("Failed to open exe file '%s'", filename);
    return false;
  }

  rfseek(fp, 0, SEEK_END);
  const uint32_t file_size = static_cast<uint32_t>(rftell(fp));
  rfseek(fp, 0, SEEK_SET);

  BIOS::PSEXEHeader header;
  if (rfread(&header, sizeof(header), 1, fp) != 1 || !BIOS::IsValidPSExeHeader(header, file_size))
  {
    Log_ErrorPrintf("'%s' is not a valid PS-EXE", filename);
    rfclose(fp);
    return false;
  }

  if (header.memfill_size > 0)
  {
    const uint32_t words_to_write = header.memfill_size / 4;
    uint32_t address = header.memfill_start & ~UINT32_C(3);
    for (uint32_t i = 0; i < words_to_write; i++)
    {
      CPU::SafeWriteMemoryWord(address, 0);
      address += sizeof(uint32_t);
    }
  }

  const uint32_t file_data_size = std::min<uint32_t>(file_size - sizeof(BIOS::PSEXEHeader), header.file_size);
  if (file_data_size >= 4)
  {
    std::vector<uint32_t> data_words((file_data_size + 3) / 4);
    if (rfread(data_words.data(), file_data_size, 1, fp) != 1)
    {
      rfclose(fp);
      return false;
    }

    const uint32_t num_words = file_data_size / 4;
    uint32_t address = header.load_address;
    for (uint32_t i = 0; i < num_words; i++)
    {
      CPU::SafeWriteMemoryWord(address, data_words[i]);
      address += sizeof(uint32_t);
    }
  }

  rfclose(fp);

  // patch the BIOS to jump to the executable directly
  const uint32_t r_pc = header.initial_pc;
  const uint32_t r_gp = header.initial_gp;
  const uint32_t r_sp = header.initial_sp_base + header.initial_sp_offset;
  const uint32_t r_fp = header.initial_sp_base + header.initial_sp_offset;
  return BIOS::PatchBIOSForEXE(Bus::g_bios, Bus::BIOS_SIZE, r_pc, r_gp, r_sp, r_fp);
}

bool LoadEXE(const char* filename)
{
  const std::string libps_path(FileSystem::BuildRelativePath(filename, "libps.exe"));
  if (!libps_path.empty() && path_is_valid(libps_path.c_str()) && !LoadEXEToRAM(libps_path.c_str(), false))
  {
    Log_ErrorPrintf("Failed to load libps.exe from '%s'", libps_path.c_str());
    return false;
  }

  return LoadEXEToRAM(filename, true);
}

bool InjectEXEFromBuffer(const void* buffer, uint32_t buffer_size, bool patch_bios)
{
  const uint8_t* buffer_ptr = static_cast<const uint8_t*>(buffer);
  const uint8_t* buffer_end = static_cast<const uint8_t*>(buffer) + buffer_size;

  BIOS::PSEXEHeader header;
  if (buffer_size < sizeof(header))
    return false;

  std::memcpy(&header, buffer_ptr, sizeof(header));
  buffer_ptr += sizeof(header);

  const uint32_t file_size = static_cast<uint32_t>(buffer_end - buffer_ptr);
  if (!BIOS::IsValidPSExeHeader(header, file_size))
    return false;

  if (header.memfill_size > 0)
  {
    const uint32_t words_to_write = header.memfill_size / 4;
    uint32_t address = header.memfill_start & ~UINT32_C(3);
    for (uint32_t i = 0; i < words_to_write; i++)
    {
      CPU::SafeWriteMemoryWord(address, 0);
      address += sizeof(uint32_t);
    }
  }

  const uint32_t file_data_size = std::min<uint32_t>(file_size - sizeof(BIOS::PSEXEHeader), header.file_size);
  if (file_data_size >= 4)
  {
    std::vector<uint32_t> data_words((file_data_size + 3) / 4);
    if (file_size < file_data_size)
      return false;

    std::memcpy(data_words.data(), buffer_ptr, file_data_size);

    const uint32_t num_words = file_data_size / 4;
    uint32_t address = header.load_address;
    for (uint32_t i = 0; i < num_words; i++)
    {
      CPU::SafeWriteMemoryWord(address, data_words[i]);
      address += sizeof(uint32_t);
    }
  }

  // patch the BIOS to jump to the executable directly
  if (patch_bios)
  {
    const uint32_t r_pc = header.initial_pc;
    const uint32_t r_gp = header.initial_gp;
    const uint32_t r_sp = header.initial_sp_base + header.initial_sp_offset;
    const uint32_t r_fp = header.initial_sp_base + header.initial_sp_offset;
    if (!BIOS::PatchBIOSForEXE(Bus::g_bios, Bus::BIOS_SIZE, r_pc, r_gp, r_sp, r_fp))
      return false;
  }

  return true;
}

#if 0
// currently not used until EXP1 is implemented

bool SetExpansionROM(const char* filename)
{
  RFILE* fp = FileSystem::OpenRFile(filename, "rb");
  if (!fp)
  {
    Log_ErrorPrintf("Failed to open '%s'", filename);
    return false;
  }

  rfseek(fp, 0, SEEK_END);
  const uint32_t size = static_cast<uint32_t>(rftell(fp));
  rfseek(fp, 0, SEEK_SET);

  std::vector<uint8_t> data(size);
  if (rfread(data.data(), size, 1, fp) != 1)
  {
    Log_ErrorPrintf("Failed to read ROM data from '%s'", filename);
    rfclose(fp);
    return false;
  }

  rfclose(fp);

  Log_InfoPrintf("Loaded expansion ROM from '%s': %u bytes", filename, size);
  Bus::SetExpansionROM(std::move(data));
  return true;
}
#endif

Controller* GetController(uint32_t slot)
{
  return g_pad.GetController(slot);
}

void UpdateControllers(void)
{
  for (uint32_t i = 0; i < NUM_CONTROLLER_AND_CARD_PORTS; i++)
  {
    g_pad.SetController(i, nullptr);

    const ControllerType type = g_settings.controller_types[i];
    if (type != ControllerType::None)
    {
      std::unique_ptr<Controller> controller = Controller::Create(type, i);
      if (controller)
      {
        controller->LoadSettings(TinyString::FromFormat("Controller%u", i + 1u));
        g_pad.SetController(i, std::move(controller));
      }
    }
  }
}

void UpdateControllerSettings(void)
{
  for (uint32_t i = 0; i < NUM_CONTROLLER_AND_CARD_PORTS; i++)
  {
    Controller* controller = g_pad.GetController(i);
    if (controller)
      controller->LoadSettings(TinyString::FromFormat("Controller%u", i + 1u));
  }
}

void ResetControllers()
{
  for (uint32_t i = 0; i < NUM_CONTROLLER_AND_CARD_PORTS; i++)
  {
    Controller* controller = g_pad.GetController(i);
    if (controller)
      controller->Reset();
  }
}

static std::unique_ptr<MemoryCard> GetMemoryCardForSlot(uint32_t slot, MemoryCardType type)
{
  // Disable memory cards when running PSFs.
  const bool is_running_psf = !s_running_game_path.empty() && IsPsfFileName(s_running_game_path.c_str());
  if (is_running_psf)
    return nullptr;

  switch (type)
  {
    case MemoryCardType::PerGame:
    {
      if (s_running_game_code.empty())
      {
        Log_WarningPrintf("Per-game memory card cannot be used for slot %u as the running "
                          "game has no code. Using shared card instead.", slot + 1u);
        return MemoryCard::Open(g_host_interface->GetSharedMemoryCardPath(slot));
      }
      else
      {
        return MemoryCard::Open(g_host_interface->GetGameMemoryCardPath(s_running_game_code.c_str(), slot));
      }
    }

    case MemoryCardType::PerGameTitle:
    {
      if (s_running_game_title.empty())
      {
        Log_WarningPrintf("Per-game memory card cannot be used for slot %u as the running "
                          "game has no title. Using shared card instead.", slot + 1u);
        return MemoryCard::Open(g_host_interface->GetSharedMemoryCardPath(slot));
      }
      else
      {
        return MemoryCard::Open(g_host_interface->GetGameMemoryCardPath(
          MemoryCard::SanitizeGameTitleForFileName(s_running_game_title).c_str(), slot));
      }
    }

    case MemoryCardType::PerGameFileTitle:
    {
      const std::string display_name(FileSystem::GetDisplayNameFromPath(s_running_game_path));
      const std::string_view file_title(FileSystem::GetFileTitleFromPath(display_name));
      if (file_title.empty())
      {
        Log_WarningPrintf("Per-game memory card cannot be used for slot %u as the running "
                          "game has no path. Using shared card instead.", slot + 1u);
        return MemoryCard::Open(g_host_interface->GetSharedMemoryCardPath(slot));
      }
      else
      {
        return MemoryCard::Open(
          g_host_interface->GetGameMemoryCardPath(MemoryCard::SanitizeGameTitleForFileName(file_title).c_str(), slot));
      }
    }

    case MemoryCardType::Shared:
    {
      if (g_settings.memory_card_paths[slot].empty())
        return MemoryCard::Open(g_host_interface->GetSharedMemoryCardPath(slot));
      else
        return MemoryCard::Open(g_settings.memory_card_paths[slot]);
    }

    case MemoryCardType::NonPersistent:
      return MemoryCard::Create();

    case MemoryCardType::Libretro:
      return MemoryCard::Create();

    case MemoryCardType::None:
    default:
      return nullptr;
  }
}

void UpdateMemoryCardTypes()
{
  for (uint32_t i = 0; i < NUM_CONTROLLER_AND_CARD_PORTS; i++)
  {
    g_pad.SetMemoryCard(i, nullptr);

    const MemoryCardType type = g_settings.memory_card_types[i];
    std::unique_ptr<MemoryCard> card = GetMemoryCardForSlot(i, type);
    if (card)
      g_pad.SetMemoryCard(i, std::move(card));
  }
}

static void UpdatePerGameMemoryCards()
{
  for (uint32_t i = 0; i < NUM_CONTROLLER_AND_CARD_PORTS; i++)
  {
    const MemoryCardType type = g_settings.memory_card_types[i];
    if (!Settings::IsPerGameMemoryCardType(type))
      continue;

    g_pad.SetMemoryCard(i, nullptr);

    std::unique_ptr<MemoryCard> card = GetMemoryCardForSlot(i, type);
    if (card)
      g_pad.SetMemoryCard(i, std::move(card));
  }
}

void UpdateMultitaps(void)
{
  switch (g_settings.multitap_mode)
  {
    case MultitapMode::Disabled:
    {
      g_pad.GetMultitap(0)->SetEnable(false, 0);
      g_pad.GetMultitap(1)->SetEnable(false, 0);
    }
    break;

    case MultitapMode::Port1Only:
    {
      g_pad.GetMultitap(0)->SetEnable(true, 0);
      g_pad.GetMultitap(1)->SetEnable(false, 0);
    }
    break;

    case MultitapMode::Port2Only:
    {
      g_pad.GetMultitap(0)->SetEnable(false, 0);
      g_pad.GetMultitap(1)->SetEnable(true, 1);
    }
    break;

    case MultitapMode::BothPorts:
    {
      g_pad.GetMultitap(0)->SetEnable(true, 0);
      g_pad.GetMultitap(1)->SetEnable(true, 4);
    }
    break;
  }
}

bool HasMedia()
{
  return g_cdrom.HasMedia();
}

std::string GetMediaFileName()
{
  if (!g_cdrom.HasMedia())
    return {};

  return g_cdrom.GetMediaFileName();
}

bool InsertMedia(const char* path)
{
  Common::Error error;
  std::unique_ptr<CDImage> image = OpenCDImage(path, &error, false, ShouldCheckForImagePatches());
  if (!image)
  {
    g_host_interface->AddFormattedOSDMessage(
      10.0f, g_host_interface->TranslateString("OSDMessage", "Failed to open disc image '%s': %s."), path,
      error.GetCodeAndMessage().GetCharArray());
    return false;
  }

  UpdateRunningGame(path, image.get());
  g_cdrom.InsertMedia(std::move(image));
  Log_InfoPrintf("Inserted media from %s (%s, %s)", s_running_game_path.c_str(), s_running_game_code.c_str(),
                 s_running_game_title.c_str());
  g_host_interface->AddFormattedOSDMessage(10.0f,
                                           g_host_interface->TranslateString("OSDMessage", "Inserted disc '%s' (%s)."),
                                           s_running_game_title.c_str(), s_running_game_code.c_str());

  if (g_settings.HasAnyPerGameMemoryCards())
  {
    UpdatePerGameMemoryCards();
  }

  ClearMemorySaveStates();
  return true;
}

void RemoveMedia()
{
  g_cdrom.RemoveMedia(false);
  ClearMemorySaveStates();
}

void SetMediaTrayOpen(bool opened)
{
  g_cdrom.SetShellOpen(opened);
  ClearMemorySaveStates();
}

void UpdateRunningGame(const char* path, CDImage* image)
{
  if (s_running_game_path == path)
    return;

  s_running_game_path.clear();
  s_running_game_code.clear();
  s_running_game_title.clear();

  if (path && path[0] != '\0')
  {
    s_running_game_path = path;
    g_host_interface->GetGameInfo(path, image, &s_running_game_code, &s_running_game_title);

    if (image && image->HasSubImages() && g_settings.memory_card_use_playlist_title)
    {
      std::string image_title(image->GetMetadata("title"));
      if (!image_title.empty())
        s_running_game_title = std::move(image_title);
    }
  }

  g_texture_replacements.SetGameID(s_running_game_code);

  g_host_interface->OnRunningGameChanged(s_running_game_path, image, s_running_game_code, s_running_game_title);
}

bool CheckForSBIFile(CDImage* image)
{
  if (s_running_game_code.empty() || !LibcryptGameList::IsLibcryptGameCode(s_running_game_code) || !image ||
      image->HasNonStandardSubchannel())
  {
    return true;
  }

  Log_WarningPrintf("SBI file missing but required for %s (%s)", s_running_game_code.c_str(),
                    s_running_game_title.c_str());

  // The libretro frontend has no synchronous "are you sure?" UI hook,
  // so this used to call ConfirmMessage which is a stub returning
  // false unconditionally - meaning users who explicitly opted in via
  // AllowBootingWithoutSBIFile=true were still blocked from booting.
  // Honour the opt-in instead: emit a long-duration OSD warning and
  // proceed. The off-by-default branch keeps the hard refusal so
  // ordinary users still get the error message about needing the SBI.
  if (g_host_interface->GetBoolSettingValue("CDROM", "AllowBootingWithoutSBIFile", false))
  {
    g_host_interface->AddFormattedOSDMessage(
      30.0f,
      g_host_interface->TranslateString(
        "System",
        "WARNING: Running libcrypt-protected game %s (%s) without an SBI file. The game will likely not "
        "run correctly. See the README for how to add an SBI file."),
      s_running_game_code.c_str(), s_running_game_title.c_str());
    return true;
  }

  g_host_interface->ReportError(SmallString::FromFormat(
    g_host_interface->TranslateString(
      "System", "You are attempting to run a libcrypt protected game without an SBI file:\n\n%s: %s\n\nYour dump is "
                "incomplete, you must add the SBI file to run this game. \n\n"
                "The name of the SBI file must match the name of the disc image."),
    s_running_game_code.c_str(), s_running_game_title.c_str()));
  return false;
}

bool HasMediaSubImages()
{
  const CDImage* cdi = g_cdrom.GetMedia();
  return cdi ? cdi->HasSubImages() : false;
}

uint32_t GetMediaSubImageCount()
{
  const CDImage* cdi = g_cdrom.GetMedia();
  return cdi ? cdi->GetSubImageCount() : 0;
}

uint32_t GetMediaSubImageIndex()
{
  const CDImage* cdi = g_cdrom.GetMedia();
  return cdi ? cdi->GetCurrentSubImage() : 0;
}

std::string GetMediaSubImageTitle(uint32_t index)
{
  const CDImage* cdi = g_cdrom.GetMedia();
  if (!cdi)
    return {};

  return cdi->GetSubImageMetadata(index, "title");
}

std::string GetMediaSubImagePath(uint32_t index)
{
  const CDImage* cdi = g_cdrom.GetMedia();
  if (!cdi)
    return {};

  return cdi->GetSubImageMetadata(index, "file_path");
}

bool SwitchMediaSubImage(uint32_t index)
{
  if (!g_cdrom.HasMedia())
    return false;

  std::unique_ptr<CDImage> image = g_cdrom.RemoveMedia(true);

  Common::Error error;
  if (!image->SwitchSubImage(index, &error))
  {
    g_host_interface->AddFormattedOSDMessage(
      10.0f, g_host_interface->TranslateString("OSDMessage", "Failed to switch to subimage %u in '%s': %s."),
      index + 1u, image->GetFileName().c_str(), error.GetCodeAndMessage().GetCharArray());
    g_cdrom.InsertMedia(std::move(image));
    return false;
  }

  g_host_interface->AddFormattedOSDMessage(
    20.0f, g_host_interface->TranslateString("OSDMessage", "Switched to sub-image %s (%u) in '%s'."),
    image->GetSubImageMetadata(index, "title").c_str(), index + 1u, image->GetMetadata("title").c_str());
  g_cdrom.InsertMedia(std::move(image));

  ClearMemorySaveStates();
  return true;
}

CheatList* GetCheatList()
{
  return s_cheat_list.get();
}

void SetCheatList(std::unique_ptr<CheatList> cheats)
{
  // Reset the shared cheat scratch registers used by D7/51/52 instruction
  // families - the new cheat list (or null, for retro_cheat_reset) gets
  // its own clean register state rather than inheriting whatever the
  // previous list left behind.
  CheatList::ResetSharedScratchRegisters();
  s_cheat_list = std::move(cheats);
}

void ClearMemorySaveStates()
{
  s_runahead_states.clear();
}

void UpdateMemorySaveStateSettings()
{
  ClearMemorySaveStates();

  s_runahead_frames = g_settings.runahead_frames;
  s_runahead_replay_pending = false;
  if (s_runahead_frames > 0)
  {
    Log_InfoPrintf("Runahead is active with %u frames", s_runahead_frames);
    // The replay path used to allocate a separate NullAudioStream and
    // pointer-swap it onto the SPU; that has been replaced with a
    // SetSilentMode toggle on the host's existing audio stream
    // (see DoRunahead) so the indirection and the second instance go
    // away.
  }
}

bool LoadMemoryState(const MemorySaveState& mss)
{
  mss.state_stream->SeekAbsolute(0);

  StateWrapper sw(mss.state_stream.get(), StateWrapper::Mode::Read, SAVE_STATE_VERSION);
  HostDisplayTexture* host_texture = mss.vram_texture.get();
  if (!DoState(sw, &host_texture, true, true))
  {
    g_host_interface->ReportError("Failed to load memory save state, resetting.");
    Reset();
    return false;
  }

  return true;
}

bool SaveMemoryState(MemorySaveState* mss)
{
  if (!mss->state_stream)
    mss->state_stream = std::make_unique<GrowableMemoryByteStream>(nullptr, MAX_SAVE_STATE_SIZE);
  else
    mss->state_stream->SeekAbsolute(0);

  HostDisplayTexture* host_texture = mss->vram_texture.release();
  StateWrapper sw(mss->state_stream.get(), StateWrapper::Mode::Write, SAVE_STATE_VERSION);
  if (!DoState(sw, &host_texture, false, true))
  {
    Log_ErrorPrint("Failed to create runahead memory state.");
    delete host_texture;
    return false;
  }

  mss->vram_texture.reset(host_texture);
  return true;
}

void SaveRunaheadState()
{
  // try to reuse the frontmost slot
  MemorySaveState mss;
  while (s_runahead_states.size() >= s_runahead_frames)
  {
    mss = std::move(s_runahead_states.front());
    s_runahead_states.pop_front();
  }

  if (!SaveMemoryState(&mss))
  {
    Log_ErrorPrint("Failed to save runahead state.");
    return;
  }

  s_runahead_states.push_back(std::move(mss));
}

void DoRunahead()
{
  if (s_runahead_replay_pending)
  {
    // we need to replay and catch up - load the state,
    s_runahead_replay_pending = false;
    if (s_runahead_states.empty() || !LoadMemoryState(s_runahead_states.front()))
    {
      s_runahead_states.clear();
      return;
    }

    // and throw away all the states, forcing us to catch up below
    // TODO: can we leave one frame here and run, avoiding the extra save?
    s_runahead_states.clear();

  }

  // run the frames with no audio
  int32_t frames_to_run = static_cast<int32_t>(s_runahead_frames) - static_cast<int32_t>(s_runahead_states.size());
  if (frames_to_run > 0)
  {
    // Switch the host audio stream into silent mode for the replay
    // window: the SPU keeps producing samples for state correctness,
    // but the FIFO is drained inside EndWrite so nothing reaches the
    // libretro frontend and back-pressure does not build up. This
    // replaces the previous pointer-swap-to-NullAudioStream pattern.
    LibretroAudioStream* const audio_stream = g_host_interface->GetAudioStream();
    audio_stream->SetSilentMode(true);

    while (frames_to_run > 0)
    {
      DoRunFrame();
      SaveRunaheadState();
      frames_to_run--;
    }

    audio_stream->SetSilentMode(false);
  }
  else
  {
    // save this frame
    SaveRunaheadState();
  }
}

void SetRunaheadReplayFlag()
{
  if (s_runahead_frames == 0 || s_runahead_states.empty())
    return;

  s_runahead_replay_pending = true;
}

} // namespace System
