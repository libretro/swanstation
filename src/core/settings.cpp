#include "settings.h"
#include "common/make_array.h"
#include "common/string_util.h"
#include "host_display.h"
#include "host_interface.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <numeric>

Settings g_settings;

SettingsInterface::~SettingsInterface() = default;

const char* SettingInfo::StringDefaultValue() const
{
  return default_value ? default_value : "";
}

bool SettingInfo::BooleanDefaultValue() const
{
  return default_value ? StringUtil::FromChars<bool>(default_value).value_or(false) : false;
}

s32 SettingInfo::IntegerDefaultValue() const
{
  return default_value ? StringUtil::FromChars<s32>(default_value).value_or(0) : 0;
}

s32 SettingInfo::IntegerMinValue() const
{
  static constexpr s32 fallback_value = std::numeric_limits<s32>::min();
  return min_value ? StringUtil::FromChars<s32>(min_value).value_or(fallback_value) : fallback_value;
}

s32 SettingInfo::IntegerMaxValue() const
{
  static constexpr s32 fallback_value = std::numeric_limits<s32>::max();
  return max_value ? StringUtil::FromChars<s32>(max_value).value_or(fallback_value) : fallback_value;
}

s32 SettingInfo::IntegerStepValue() const
{
  static constexpr s32 fallback_value = 1;
  return step_value ? StringUtil::FromChars<s32>(step_value).value_or(fallback_value) : fallback_value;
}

float SettingInfo::FloatDefaultValue() const
{
  return default_value ? StringUtil::FromChars<float>(default_value).value_or(0.0f) : 0.0f;
}

float SettingInfo::FloatMinValue() const
{
  static constexpr float fallback_value = std::numeric_limits<float>::min();
  return min_value ? StringUtil::FromChars<float>(min_value).value_or(fallback_value) : fallback_value;
}

float SettingInfo::FloatMaxValue() const
{
  static constexpr float fallback_value = std::numeric_limits<float>::max();
  return max_value ? StringUtil::FromChars<float>(max_value).value_or(fallback_value) : fallback_value;
}

float SettingInfo::FloatStepValue() const
{
  static constexpr float fallback_value = 0.1f;
  return step_value ? StringUtil::FromChars<float>(step_value).value_or(fallback_value) : fallback_value;
}

Settings::Settings() = default;

bool Settings::HasAnyPerGameMemoryCards() const
{
  return std::any_of(memory_card_types.begin(), memory_card_types.end(), [](MemoryCardType t) {
    return (t == MemoryCardType::PerGame || t == MemoryCardType::PerGameTitle);
  });
}

std::array<TinyString, NUM_CONTROLLER_AND_CARD_PORTS> Settings::GeneratePortLabels() const
{
  static constexpr std::array<std::array<bool, NUM_MULTITAPS>, static_cast<size_t>(MultitapMode::Count)>
    multitap_enabled_on_port = {{{false, false}, {true, false}, {false, true}, {true, true}}};

  std::array<TinyString, NUM_CONTROLLER_AND_CARD_PORTS> labels;

  u32 logical_port = 0;
  for (u32 physical_port = 0; physical_port < NUM_MULTITAPS; physical_port++)
  {
    if (multitap_enabled_on_port[static_cast<size_t>(multitap_mode)][physical_port])
    {
      for (u32 i = 0; i < 4; i++)
      {
        labels[logical_port] = TinyString::FromFormat("Port %u%c", physical_port + 1u, 'A' + i);
        logical_port++;
      }
    }
    else
    {
      labels[logical_port] = TinyString::FromFormat("Port %u", physical_port + 1u);
      logical_port++;
    }
  }

  return labels;
}

void Settings::CPUOverclockPercentToFraction(u32 percent, u32* numerator, u32* denominator)
{
  const u32 percent_gcd = std::gcd(percent, 100);
  *numerator = percent / percent_gcd;
  *denominator = 100u / percent_gcd;
}

u32 Settings::CPUOverclockFractionToPercent(u32 numerator, u32 denominator)
{
  return (numerator * 100u) / denominator;
}

void Settings::SetCPUOverclockPercent(u32 percent)
{
  CPUOverclockPercentToFraction(percent, &cpu_overclock_numerator, &cpu_overclock_denominator);
}

u32 Settings::GetCPUOverclockPercent() const
{
  return CPUOverclockFractionToPercent(cpu_overclock_numerator, cpu_overclock_denominator);
}

void Settings::UpdateOverclockActive()
{
  cpu_overclock_active = (cpu_overclock_enable && (cpu_overclock_numerator != 1 || cpu_overclock_denominator != 1));
}

void Settings::Load(SettingsInterface& si)
{
  region =
    ParseConsoleRegionName(
      si.GetStringValue("Console", "Region", Settings::GetConsoleRegionName(Settings::DEFAULT_CONSOLE_REGION)).c_str())
      .value_or(DEFAULT_CONSOLE_REGION);
  enable_8mb_ram = si.GetBoolValue("Console", "Enable8MBRAM", false);

  apply_game_settings = si.GetBoolValue("Main", "ApplyGameSettings", true);
  runahead_frames = static_cast<u32>(si.GetIntValue("Main", "RunaheadFrameCount", 0));

  audio_fast_hook = si.GetBoolValue("Audio", "FastHook", true);

  cpu_execution_mode =
    ParseCPUExecutionMode(
      si.GetStringValue("CPU", "ExecutionMode", GetCPUExecutionModeName(DEFAULT_CPU_EXECUTION_MODE)).c_str())
      .value_or(DEFAULT_CPU_EXECUTION_MODE);

  cpu_recompiler_block_linking = si.GetBoolValue("CPU", "RecompilerBlockLinking", true);
  cpu_recompiler_icache = si.GetBoolValue("CPU", "RecompilerICache", false);
  cpu_fastmem_mode = ParseCPUFastmemMode(
                       si.GetStringValue("CPU", "FastmemMode", GetCPUFastmemModeName(DEFAULT_CPU_FASTMEM_MODE)).c_str())
                       .value_or(DEFAULT_CPU_FASTMEM_MODE);
  cpu_fastmem_rewrite = si.GetBoolValue("CPU", "FastmemRewrite", false);

  gpu_renderer = ParseRendererName(si.GetStringValue("GPU", "Renderer", GetRendererName(DEFAULT_GPU_RENDERER)).c_str())
                   .value_or(DEFAULT_GPU_RENDERER);
  gpu_resolution_scale = static_cast<u32>(si.GetIntValue("GPU", "ResolutionScale", 1));
  gpu_use_thread = si.GetBoolValue("GPU", "UseThread", true);
  gpu_use_software_renderer_for_readbacks = si.GetBoolValue("GPU", "UseSoftwareRendererForReadbacks", false);
  gpu_true_color = si.GetBoolValue("GPU", "TrueColor", false);
  gpu_scaled_dithering = si.GetBoolValue("GPU", "ScaledDithering", false);
  gpu_texture_filter =
    ParseTextureFilterName(
      si.GetStringValue("GPU", "TextureFilter", GetTextureFilterName(DEFAULT_GPU_TEXTURE_FILTER)).c_str())
      .value_or(DEFAULT_GPU_TEXTURE_FILTER);
  gpu_downsample_mode =
    ParseDownsampleModeName(
      si.GetStringValue("GPU", "DownsampleMode", GetDownsampleModeName(DEFAULT_GPU_DOWNSAMPLE_MODE)).c_str())
      .value_or(DEFAULT_GPU_DOWNSAMPLE_MODE);
  gpu_disable_interlacing = si.GetBoolValue("GPU", "DisableInterlacing", true);
  gpu_force_ntsc_timings = si.GetBoolValue("GPU", "ForceNTSCTimings", false);
  gpu_widescreen_hack = si.GetBoolValue("GPU", "WidescreenHack", false);
  gpu_24bit_chroma_smoothing = si.GetBoolValue("GPU", "ChromaSmoothing24Bit", false);
  gpu_pgxp_enable = si.GetBoolValue("GPU", "PGXPEnable", false);
  gpu_pgxp_culling = si.GetBoolValue("GPU", "PGXPCulling", true);
  gpu_pgxp_texture_correction = si.GetBoolValue("GPU", "PGXPTextureCorrection", true);
  gpu_pgxp_color_correction = si.GetBoolValue("GPU", "PGXPColorCorrection", false);
  gpu_pgxp_vertex_cache = si.GetBoolValue("GPU", "PGXPVertexCache", false);
  gpu_pgxp_cpu = si.GetBoolValue("GPU", "PGXPCPU", false);
  gpu_pgxp_preserve_proj_fp = si.GetBoolValue("GPU", "PGXPPreserveProjFP", false);
  gpu_pgxp_tolerance = si.GetFloatValue("GPU", "PGXPTolerance", -1.0f);
  gpu_pgxp_depth_buffer = si.GetBoolValue("GPU", "PGXPDepthBuffer", false);
  SetPGXPDepthClearThreshold(si.GetFloatValue("GPU", "PGXPDepthClearThreshold", DEFAULT_GPU_PGXP_DEPTH_THRESHOLD));

  display_crop_mode =
    ParseDisplayCropMode(
      si.GetStringValue("Display", "CropMode", GetDisplayCropModeName(DEFAULT_DISPLAY_CROP_MODE)).c_str())
      .value_or(DEFAULT_DISPLAY_CROP_MODE);
  display_aspect_ratio =
    ParseDisplayAspectRatio(
      si.GetStringValue("Display", "AspectRatio", GetDisplayAspectRatioName(DEFAULT_DISPLAY_ASPECT_RATIO)).c_str())
      .value_or(DEFAULT_DISPLAY_ASPECT_RATIO);
  display_aspect_ratio_custom_numerator = static_cast<u16>(
    std::clamp<int>(si.GetIntValue("Display", "CustomAspectRatioNumerator", 4), 1, std::numeric_limits<u16>::max()));
  display_aspect_ratio_custom_denominator = static_cast<u16>(
    std::clamp<int>(si.GetIntValue("Display", "CustomAspectRatioDenominator", 3), 1, std::numeric_limits<u16>::max()));
  display_force_4_3_for_24bit = si.GetBoolValue("Display", "Force4_3For24Bit", false);
  display_active_start_offset = static_cast<s16>(si.GetIntValue("Display", "ActiveStartOffset", 0));
  display_active_end_offset = static_cast<s16>(si.GetIntValue("Display", "ActiveEndOffset", 0));
  display_line_start_offset = static_cast<s8>(si.GetIntValue("Display", "LineStartOffset", 0));
  display_line_end_offset = static_cast<s8>(si.GetIntValue("Display", "LineEndOffset", 0));
  display_show_osd_messages = si.GetBoolValue("Display", "ShowOSDMessages", true);

  cdrom_readahead_sectors = static_cast<u8>(si.GetIntValue("CDROM", "ReadaheadSectors", DEFAULT_CDROM_READAHEAD_SECTORS));
  cdrom_region_check = si.GetBoolValue("CDROM", "RegionCheck", false);
  cdrom_load_image_to_ram = si.GetBoolValue("CDROM", "LoadImageToRAM", false);
  cdrom_precache_chd = si.GetBoolValue("CDROM", "PreCacheCHD", false);
  cdrom_mute_cd_audio = si.GetBoolValue("CDROM", "MuteCDAudio", false);
  cdrom_read_speedup = si.GetIntValue("CDROM", "ReadSpeedup", 1);
  cdrom_seek_speedup = si.GetIntValue("CDROM", "SeekSpeedup", 1);

  use_old_mdec_routines = si.GetBoolValue("Hacks", "OldMDECRoutines", true);

  bios_patch_fast_boot = si.GetBoolValue("BIOS", "PatchFastBoot", DEFAULT_FAST_BOOT_VALUE);

  controller_analog_combo = si.GetIntValue("Controller", "AnalogCombo", 1);
  controller_enable_rumble = si.GetBoolValue("Controller", "EnableRumble", true);
  controller_show_crosshair = si.GetBoolValue("Controller", "ShowCrosshair", true);

  memory_card_types[0] =
    ParseMemoryCardTypeName(
      si.GetStringValue("MemoryCards", "Card1Type", GetMemoryCardTypeName(DEFAULT_MEMORY_CARD_1_TYPE)).c_str())
      .value_or(DEFAULT_MEMORY_CARD_1_TYPE);
  memory_card_types[1] =
    ParseMemoryCardTypeName(
      si.GetStringValue("MemoryCards", "Card2Type", GetMemoryCardTypeName(DEFAULT_MEMORY_CARD_2_TYPE)).c_str())
      .value_or(DEFAULT_MEMORY_CARD_2_TYPE);

  memory_card_use_playlist_title = si.GetBoolValue("MemoryCards", "UsePlaylistTitle", true);

  multitap_mode =
    ParseMultitapModeName(
      si.GetStringValue("ControllerPorts", "MultitapMode", GetMultitapModeName(DEFAULT_MULTITAP_MODE)).c_str())
      .value_or(DEFAULT_MULTITAP_MODE);

  log_level = ParseLogLevelName(si.GetStringValue("Logging", "LogLevel", GetLogLevelName(DEFAULT_LOG_LEVEL)).c_str())
                .value_or(DEFAULT_LOG_LEVEL);

  texture_replacements.enable_vram_write_replacements =
    si.GetBoolValue("TextureReplacements", "EnableVRAMWriteReplacements", false);
  texture_replacements.preload_textures = si.GetBoolValue("TextureReplacements", "PreloadTextures", false);
}

static std::array<const char*, static_cast<std::size_t>(LogLevel::Count)> s_log_level_names = {
  {"None", "Error", "Warning", "Perf", "Info", "Verbose", "Dev", "Profile", "Debug", "Trace"}};

std::optional<LogLevel> Settings::ParseLogLevelName(const char* str)
{
  int index = 0;
  for (const char* name : s_log_level_names)
  {
    if (StringUtil::Strcasecmp(name, str) == 0)
      return static_cast<LogLevel>(index);

    index++;
  }

  return std::nullopt;
}

const char* Settings::GetLogLevelName(LogLevel level)
{
  return s_log_level_names[static_cast<int>(level)];
}

static std::array<const char*, 4> s_console_region_names = {{"Auto", "NTSC-J", "NTSC-U", "PAL"}};
static std::array<const char*, 4> s_console_region_display_names = {
  {TRANSLATABLE("ConsoleRegion", "Auto-Detect"), TRANSLATABLE("ConsoleRegion", "NTSC-J (Japan)"),
   TRANSLATABLE("ConsoleRegion", "NTSC-U/C (US, Canada)"), TRANSLATABLE("ConsoleRegion", "PAL (Europe, Australia)")}};

std::optional<ConsoleRegion> Settings::ParseConsoleRegionName(const char* str)
{
  int index = 0;
  for (const char* name : s_console_region_names)
  {
    if (StringUtil::Strcasecmp(name, str) == 0)
      return static_cast<ConsoleRegion>(index);

    index++;
  }

  return std::nullopt;
}

const char* Settings::GetConsoleRegionName(ConsoleRegion region)
{
  return s_console_region_names[static_cast<int>(region)];
}

const char* Settings::GetConsoleRegionDisplayName(ConsoleRegion region)
{
  return s_console_region_display_names[static_cast<int>(region)];
}

static std::array<const char*, 4> s_disc_region_names = {{"NTSC-J", "NTSC-U", "PAL", "Other"}};
static std::array<const char*, 4> s_disc_region_display_names = {
  {TRANSLATABLE("DiscRegion", "NTSC-J (Japan)"), TRANSLATABLE("DiscRegion", "NTSC-U/C (US, Canada)"),
   TRANSLATABLE("DiscRegion", "PAL (Europe, Australia)"), TRANSLATABLE("DiscRegion", "Other")}};

std::optional<DiscRegion> Settings::ParseDiscRegionName(const char* str)
{
  int index = 0;
  for (const char* name : s_disc_region_names)
  {
    if (StringUtil::Strcasecmp(name, str) == 0)
      return static_cast<DiscRegion>(index);

    index++;
  }

  return std::nullopt;
}

const char* Settings::GetDiscRegionName(DiscRegion region)
{
  return s_disc_region_names[static_cast<int>(region)];
}

const char* Settings::GetDiscRegionDisplayName(DiscRegion region)
{
  return s_disc_region_display_names[static_cast<int>(region)];
}

static std::array<const char*, 3> s_cpu_execution_mode_names = {{"Interpreter", "CachedInterpreter", "Recompiler"}};
static std::array<const char*, 3> s_cpu_execution_mode_display_names = {
  {TRANSLATABLE("CPUExecutionMode", "Interpreter (Slowest)"),
   TRANSLATABLE("CPUExecutionMode", "Cached Interpreter (Faster)"),
   TRANSLATABLE("CPUExecutionMode", "Recompiler (Fastest)")}};

std::optional<CPUExecutionMode> Settings::ParseCPUExecutionMode(const char* str)
{
  u8 index = 0;
  for (const char* name : s_cpu_execution_mode_names)
  {
    if (StringUtil::Strcasecmp(name, str) == 0)
      return static_cast<CPUExecutionMode>(index);

    index++;
  }

  return std::nullopt;
}

const char* Settings::GetCPUExecutionModeName(CPUExecutionMode mode)
{
  return s_cpu_execution_mode_names[static_cast<u8>(mode)];
}

const char* Settings::GetCPUExecutionModeDisplayName(CPUExecutionMode mode)
{
  return s_cpu_execution_mode_display_names[static_cast<u8>(mode)];
}

static std::array<const char*, static_cast<u32>(CPUFastmemMode::Count)> s_cpu_fastmem_mode_names = {
  {"Disabled", "MMap", "LUT"}};
static std::array<const char*, static_cast<u32>(CPUFastmemMode::Count)> s_cpu_fastmem_mode_display_names = {
  {TRANSLATABLE("CPUFastmemMode", "Disabled (Slowest)"),
   TRANSLATABLE("CPUFastmemMode", "MMap (Hardware, Fastest, 64-Bit Only)"),
   TRANSLATABLE("CPUFastmemMode", "LUT (Faster)")}};

std::optional<CPUFastmemMode> Settings::ParseCPUFastmemMode(const char* str)
{
  u8 index = 0;
  for (const char* name : s_cpu_fastmem_mode_names)
  {
    if (StringUtil::Strcasecmp(name, str) == 0)
      return static_cast<CPUFastmemMode>(index);

    index++;
  }

  return std::nullopt;
}

const char* Settings::GetCPUFastmemModeName(CPUFastmemMode mode)
{
  return s_cpu_fastmem_mode_names[static_cast<u8>(mode)];
}

const char* Settings::GetCPUFastmemModeDisplayName(CPUFastmemMode mode)
{
  return s_cpu_fastmem_mode_display_names[static_cast<u8>(mode)];
}

static constexpr auto s_gpu_renderer_names = make_array(
#ifdef _WIN32
  "D3D11",
  "D3D12",
#endif
  "Vulkan", "OpenGL", "Software");
static constexpr auto s_gpu_renderer_display_names = make_array(
#ifdef _WIN32
  TRANSLATABLE("GPURenderer", "Hardware (D3D11)"),
  TRANSLATABLE("GPURenderer", "Hardware (D3D12)"),
#endif
  TRANSLATABLE("GPURenderer", "Hardware (Vulkan)"), TRANSLATABLE("GPURenderer", "Hardware (OpenGL)"),
  TRANSLATABLE("GPURenderer", "Software"));

std::optional<GPURenderer> Settings::ParseRendererName(const char* str)
{
  int index = 0;
  for (const char* name : s_gpu_renderer_names)
  {
    if (StringUtil::Strcasecmp(name, str) == 0)
      return static_cast<GPURenderer>(index);

    index++;
  }

  return std::nullopt;
}

const char* Settings::GetRendererName(GPURenderer renderer)
{
  return s_gpu_renderer_names[static_cast<int>(renderer)];
}

const char* Settings::GetRendererDisplayName(GPURenderer renderer)
{
  return s_gpu_renderer_display_names[static_cast<int>(renderer)];
}

static constexpr auto s_texture_filter_names =
  make_array("Nearest", "Bilinear", "BilinearBinAlpha", "JINC2", "JINC2BinAlpha", "xBR", "xBRBinAlpha");
static constexpr auto s_texture_filter_display_names =
  make_array(TRANSLATABLE("GPUTextureFilter", "Nearest-Neighbor"), TRANSLATABLE("GPUTextureFilter", "Bilinear"),
             TRANSLATABLE("GPUTextureFilter", "Bilinear (No Edge Blending)"), TRANSLATABLE("GPUTextureFilter", "JINC2"),
             TRANSLATABLE("GPUTextureFilter", "JINC2 (No Edge Blending)"), TRANSLATABLE("GPUTextureFilter", "xBR"),
             TRANSLATABLE("GPUTextureFilter", "xBR (No Edge Blending)"));

std::optional<GPUTextureFilter> Settings::ParseTextureFilterName(const char* str)
{
  int index = 0;
  for (const char* name : s_texture_filter_names)
  {
    if (StringUtil::Strcasecmp(name, str) == 0)
      return static_cast<GPUTextureFilter>(index);

    index++;
  }

  return std::nullopt;
}

const char* Settings::GetTextureFilterName(GPUTextureFilter filter)
{
  return s_texture_filter_names[static_cast<int>(filter)];
}

const char* Settings::GetTextureFilterDisplayName(GPUTextureFilter filter)
{
  return s_texture_filter_display_names[static_cast<int>(filter)];
}

static constexpr auto s_downsample_mode_names = make_array("Disabled", "Box", "Adaptive");

std::optional<GPUDownsampleMode> Settings::ParseDownsampleModeName(const char* str)
{
  int index = 0;
  for (const char* name : s_downsample_mode_names)
  {
    if (StringUtil::Strcasecmp(name, str) == 0)
      return static_cast<GPUDownsampleMode>(index);

    index++;
  }

  return std::nullopt;
}

const char* Settings::GetDownsampleModeName(GPUDownsampleMode mode)
{
  return s_downsample_mode_names[static_cast<int>(mode)];
}

static std::array<const char*, 3> s_display_crop_mode_names = {{"None", "Overscan", "Borders"}};
static std::array<const char*, 3> s_display_crop_mode_display_names = {
  {TRANSLATABLE("DisplayCropMode", "None"), TRANSLATABLE("DisplayCropMode", "Only Overscan Area"),
   TRANSLATABLE("DisplayCropMode", "All Borders")}};

std::optional<DisplayCropMode> Settings::ParseDisplayCropMode(const char* str)
{
  int index = 0;
  for (const char* name : s_display_crop_mode_names)
  {
    if (StringUtil::Strcasecmp(name, str) == 0)
      return static_cast<DisplayCropMode>(index);

    index++;
  }

  return std::nullopt;
}

const char* Settings::GetDisplayCropModeName(DisplayCropMode crop_mode)
{
  return s_display_crop_mode_names[static_cast<int>(crop_mode)];
}

const char* Settings::GetDisplayCropModeDisplayName(DisplayCropMode crop_mode)
{
  return s_display_crop_mode_display_names[static_cast<int>(crop_mode)];
}

static std::array<const char*, static_cast<size_t>(DisplayAspectRatio::Count)> s_display_aspect_ratio_names = {
  {TRANSLATABLE("DisplayAspectRatio", "Auto (Game Native)"), TRANSLATABLE("DisplayAspectRatio", "Auto (Match Window)"),
   TRANSLATABLE("DisplayAspectRatio", "Custom"), "4:3", "16:9", "19:9", "20:9", "PAR 1:1", "Native"}};
static constexpr std::array<float, static_cast<size_t>(DisplayAspectRatio::Count)> s_display_aspect_ratio_values = {
  {-1.0f, -1.0f, -1.0f, 4.0f / 3.0f, 16.0f / 9.0f, 19.0f / 9.0f, 20.0f / 9.0f, -1.0f, -1.0f}};

std::optional<DisplayAspectRatio> Settings::ParseDisplayAspectRatio(const char* str)
{
  int index = 0;
  for (const char* name : s_display_aspect_ratio_names)
  {
    if (StringUtil::Strcasecmp(name, str) == 0)
      return static_cast<DisplayAspectRatio>(index);

    index++;
  }

  return std::nullopt;
}

const char* Settings::GetDisplayAspectRatioName(DisplayAspectRatio ar)
{
  return s_display_aspect_ratio_names[static_cast<int>(ar)];
}

float Settings::GetDisplayAspectRatioValue() const
{
  switch (display_aspect_ratio)
  {
    case DisplayAspectRatio::MatchWindow:
    {
      const HostDisplay* display = g_host_interface->GetDisplay();
      if (!display)
        return s_display_aspect_ratio_values[static_cast<int>(DEFAULT_DISPLAY_ASPECT_RATIO)];

      const u32 width = display->GetWindowWidth();
      const u32 height = display->GetWindowHeight();
      return static_cast<float>(width) / static_cast<float>(height);
    }

    case DisplayAspectRatio::Custom:
    {
      return static_cast<float>(display_aspect_ratio_custom_numerator) /
             static_cast<float>(display_aspect_ratio_custom_denominator);
    }

    default:
    {
      return s_display_aspect_ratio_values[static_cast<int>(display_aspect_ratio)];
    }
  }
}

static std::array<const char*, 8> s_controller_type_names = {
  {"None", "DigitalController", "AnalogController", "AnalogJoystick", "NamcoGunCon", "PlayStationMouse", "NeGcon", "NeGconRumble"}};

std::optional<ControllerType> Settings::ParseControllerTypeName(const char* str)
{
  int index = 0;
  for (const char* name : s_controller_type_names)
  {
    if (StringUtil::Strcasecmp(name, str) == 0)
      return static_cast<ControllerType>(index);

    index++;
  }

  return std::nullopt;
}

const char* Settings::GetControllerTypeName(ControllerType type)
{
  return s_controller_type_names[static_cast<int>(type)];
}

static std::array<const char*, 7> s_memory_card_type_names = {
  {"None", "Shared", "PerGame", "PerGameTitle", "PerGameFileTitle", "NonPersistent", "Libretro"}};

std::optional<MemoryCardType> Settings::ParseMemoryCardTypeName(const char* str)
{
  int index = 0;
  for (const char* name : s_memory_card_type_names)
  {
    if (StringUtil::Strcasecmp(name, str) == 0)
      return static_cast<MemoryCardType>(index);

    index++;
  }

  return std::nullopt;
}

const char* Settings::GetMemoryCardTypeName(MemoryCardType type)
{
  return s_memory_card_type_names[static_cast<int>(type)];
}

static std::array<const char*, 4> s_multitap_enable_mode_names = {{"Disabled", "Port1Only", "Port2Only", "BothPorts"}};

std::optional<MultitapMode> Settings::ParseMultitapModeName(const char* str)
{
  u32 index = 0;
  for (const char* name : s_multitap_enable_mode_names)
  {
    if (StringUtil::Strcasecmp(name, str) == 0)
      return static_cast<MultitapMode>(index);

    index++;
  }

  return std::nullopt;
}

const char* Settings::GetMultitapModeName(MultitapMode mode)
{
  return s_multitap_enable_mode_names[static_cast<size_t>(mode)];
}
