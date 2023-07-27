#include "host_interface.h"
#include "bios.h"
#include "cdrom.h"
#include "common/audio_stream.h"
#include "common/byte_stream.h"
#include "common/file_system.h"
#include "common/image.h"
#include "common/log.h"
#include "common/string_util.h"
#include "controller.h"
#include "cpu_code_cache.h"
#include "cpu_core.h"
#include "dma.h"
#include "gpu.h"
#include "gte.h"
#include "host_display.h"
#include "pgxp.h"
#include "save_state_version.h"
#include "spu.h"
#include "system.h"
#include "texture_replacements.h"
#include <cmath>
#include <cstring>
#include <cwchar>
#include <stdlib.h>
Log_SetChannel(HostInterface);

HostInterface* g_host_interface;

HostInterface::HostInterface()
{
  g_host_interface = this;

  // we can get the program directory at construction time
  m_program_directory = FileSystem::GetPathDirectory(FileSystem::GetProgramPath());
}

HostInterface::~HostInterface()
{
  // system should be shut down prior to the destructor
  g_host_interface = nullptr;
}

bool HostInterface::Initialize()
{
  return true;
}

void HostInterface::Shutdown()
{
  if (!System::IsShutdown())
    System::Shutdown();
}

bool HostInterface::BootSystem(std::shared_ptr<SystemBootParameters> parameters)
{
  AcquireHostDisplay();

  // create the audio stream. this will never fail, since we'll just fall back to null
  m_audio_stream = CreateAudioStream();
  m_audio_stream->Reconfigure(AUDIO_SAMPLE_RATE, AUDIO_SAMPLE_RATE, AUDIO_CHANNELS, g_settings.audio_buffer_size);

  if (System::IsValid())
    g_spu.SetAudioStream(m_audio_stream.get());

  if (!System::Boot(*parameters))
  {
    if (!System::IsStartupCancelled())
    {
      ReportError(
        g_host_interface->TranslateString("System", "System failed to boot. The log may contain more information."));
    }

    m_audio_stream.reset();
    ReleaseHostDisplay();
    return false;
  }

  UpdateSoftwareCursor();
  return true;
}

void HostInterface::ResetSystem()
{
  System::Reset();
}

void HostInterface::DestroySystem()
{
  if (System::IsShutdown())
    return;

  System::Shutdown();
  m_audio_stream.reset();
  UpdateSoftwareCursor();
  ReleaseHostDisplay();
}

void HostInterface::ReportError(const char* message)
{
  Log_ErrorPrint(message);
}

void HostInterface::ReportMessage(const char* message)
{
  Log_InfoPrint(message);
}

bool HostInterface::ConfirmMessage(const char* message)
{
  Log_WarningPrintf("ConfirmMessage(\"%s\") -> Yes", message);
  return true;
}

void HostInterface::ReportFormattedError(const char* format, ...)
{
  std::va_list ap;
  va_start(ap, format);
  std::string message = StringUtil::StdStringFromFormatV(format, ap);
  va_end(ap);

  ReportError(message.c_str());
}

void HostInterface::ReportFormattedMessage(const char* format, ...)
{
  std::va_list ap;
  va_start(ap, format);
  std::string message = StringUtil::StdStringFromFormatV(format, ap);
  va_end(ap);

  ReportMessage(message.c_str());
}

void HostInterface::AddFormattedOSDMessage(float duration, const char* format, ...)
{
  std::va_list ap;
  va_start(ap, format);
  std::string message = StringUtil::StdStringFromFormatV(format, ap);
  va_end(ap);

  AddOSDMessage(std::move(message), duration);
}

std::string HostInterface::GetBIOSDirectory()
{
  std::string dir = GetStringSettingValue("BIOS", "SearchDirectory", "");
  if (!dir.empty())
    return dir;

  return GetUserDirectoryRelativePath("bios");
}

std::optional<std::vector<u8>> HostInterface::GetBIOSImage(ConsoleRegion region)
{
  std::string bios_dir = GetBIOSDirectory();
  std::string bios_name;
  switch (region)
  {
    case ConsoleRegion::NTSC_J:
      bios_name = GetStringSettingValue("BIOS", "PathNTSCJ", "");
      break;

    case ConsoleRegion::PAL:
      bios_name = GetStringSettingValue("BIOS", "PathPAL", "");
      break;

    case ConsoleRegion::NTSC_U:
    default:
      bios_name = GetStringSettingValue("BIOS", "PathNTSCU", "");
      break;
  }

  //auto-detect
  if (bios_name.empty())
    return FindBIOSImageInDirectory(region, bios_dir.c_str());

  // try the configured path
  std::optional<BIOS::Image> image = BIOS::LoadImageFromFile(
    StringUtil::StdStringFromFormat("%s" FS_OSPATH_SEPARATOR_STR "%s", bios_dir.c_str(), bios_name.c_str()).c_str());
  if (!image.has_value())
    return FindBIOSImageInDirectory(region, bios_dir.c_str());
  return image;
}

std::optional<std::vector<u8>> HostInterface::FindBIOSImageInDirectory(ConsoleRegion region, const char* directory)
{
  Log_InfoPrintf("Searching for a %s BIOS in '%s'...", Settings::GetConsoleRegionDisplayName(region), directory);

  FileSystem::FindResultsArray results;
  FileSystem::FindFiles(
    directory, "*", FILESYSTEM_FIND_FILES | FILESYSTEM_FIND_HIDDEN_FILES | FILESYSTEM_FIND_RELATIVE_PATHS, &results);

  std::string fallback_path;
  std::optional<BIOS::Image> fallback_image;
  const BIOS::ImageInfo* fallback_info = nullptr;

  for (const FILESYSTEM_FIND_DATA& fd : results)
  {
    if (fd.Size != BIOS::BIOS_SIZE && fd.Size != BIOS::BIOS_SIZE_PS2 && fd.Size != BIOS::BIOS_SIZE_PS3)
    {
      Log_WarningPrintf("Skipping '%s': incorrect size", fd.FileName.c_str());
      continue;
    }

    std::string full_path(
      StringUtil::StdStringFromFormat("%s" FS_OSPATH_SEPARATOR_STR "%s", directory, fd.FileName.c_str()));

    std::optional<BIOS::Image> found_image = BIOS::LoadImageFromFile(full_path.c_str());
    if (!found_image)
      continue;

    BIOS::Hash found_hash = BIOS::GetHash(*found_image);

    const BIOS::ImageInfo* ii = BIOS::GetImageInfoForHash(found_hash);

    if (BIOS::IsValidHashForRegion(region, found_hash))
    {
      Log_InfoPrintf("Using BIOS '%s': %s", fd.FileName.c_str(), ii ? ii->description : "");
      return found_image;
    }

    // don't let an unknown bios take precedence over a known one
    if (!fallback_path.empty() && (fallback_info || !ii))
      continue;

    fallback_path = std::move(full_path);
    fallback_image = std::move(found_image);
    fallback_info = ii;
  }

  if (!fallback_image.has_value())
  {
    g_host_interface->ReportFormattedError(
      g_host_interface->TranslateString("HostInterface", "No BIOS image found for %s region"),
      Settings::GetConsoleRegionDisplayName(region));
    return std::nullopt;
  }

  if (!fallback_info)
    return std::nullopt;

  Log_WarningPrintf("Falling back to possibly-incompatible image '%s': %s", fallback_path.c_str(),
		  fallback_info->description);

  return fallback_image;
}

std::string HostInterface::GetShaderCacheBasePath() const
{
  return GetUserDirectoryRelativePath("cache/");
}

void HostInterface::FixIncompatibleSettings(bool display_osd_messages)
{
  if (g_settings.disable_all_enhancements)
  {
    Log_WarningPrintf("All enhancements disabled by config setting.");
    g_settings.cpu_overclock_enable = false;
    g_settings.cpu_overclock_active = false;
    g_settings.enable_8mb_ram = false;
    g_settings.gpu_resolution_scale = 1;
    g_settings.gpu_multisamples = 1;
    g_settings.gpu_per_sample_shading = false;
    g_settings.gpu_true_color = false;
    g_settings.gpu_scaled_dithering = false;
    g_settings.gpu_texture_filter = GPUTextureFilter::Nearest;
    g_settings.gpu_disable_interlacing = false;
    g_settings.gpu_force_ntsc_timings = false;
    g_settings.gpu_widescreen_hack = false;
    g_settings.gpu_pgxp_enable = false;
    g_settings.gpu_24bit_chroma_smoothing = false;
    g_settings.cdrom_read_speedup = 1;
    g_settings.cdrom_seek_speedup = 1;
    g_settings.cdrom_mute_cd_audio = false;
    g_settings.texture_replacements.enable_vram_write_replacements = false;
    g_settings.bios_patch_fast_boot = false;
    g_settings.bios_patch_tty_enable = false;
  }

  if (g_settings.gpu_pgxp_enable)
  {
    if (g_settings.gpu_renderer == GPURenderer::Software)
    {
      g_settings.gpu_pgxp_enable = false;
    }
  }

#ifndef WITH_MMAP_FASTMEM
  if (g_settings.cpu_fastmem_mode == CPUFastmemMode::MMap)
  {
    Log_WarningPrintf("mmap fastmem is not available on this platform, using LUT instead.");
    g_settings.cpu_fastmem_mode = CPUFastmemMode::LUT;
  }
#endif

#if defined(__ANDROID__) && defined(__arm__) && !defined(__aarch64__) && !defined(_M_ARM64)
  if (g_settings.rewind_enable)
  {
    Log_WarningPrintf("Rewind is not supported on 32-bit ARM for Android.");
    g_settings.rewind_enable = false;
  }

  if (g_settings.runahead_frames > 0)
  {
    Log_WarningPrintf("Runahead is not supported on 32-bit ARM for Android.");
    g_settings.runahead_frames = 0;
  }
#endif

  if (g_settings.runahead_frames > 0 && g_settings.cpu_recompiler_block_linking)
  {
    // Block linking is good for performance, but hurts when regularly loading (i.e. runahead), since everything has to
    // be unlinked. Which would be thousands of blocks.
    g_settings.cpu_recompiler_block_linking = false;
  }
}

void HostInterface::CheckForSettingsChanges(const Settings& old_settings)
{
  if (System::IsValid() && (g_settings.gpu_renderer != old_settings.gpu_renderer))
  {
    RecreateSystem();
  }

  if (System::IsValid())
  {
    System::ClearMemorySaveStates();

    if (g_settings.cpu_overclock_active != old_settings.cpu_overclock_active ||
        (g_settings.cpu_overclock_active &&
         (g_settings.cpu_overclock_numerator != old_settings.cpu_overclock_numerator ||
          g_settings.cpu_overclock_denominator != old_settings.cpu_overclock_denominator)))
    {
      System::UpdateOverclock();
    }

    if (g_settings.cpu_execution_mode != old_settings.cpu_execution_mode ||
        g_settings.cpu_fastmem_mode != old_settings.cpu_fastmem_mode ||
        g_settings.cpu_fastmem_rewrite != old_settings.cpu_fastmem_rewrite)
    {
      CPU::CodeCache::Reinitialize();
      CPU::ClearICache();
    }

    if (g_settings.cpu_execution_mode == CPUExecutionMode::Recompiler &&
        (g_settings.cpu_recompiler_memory_exceptions != old_settings.cpu_recompiler_memory_exceptions ||
         g_settings.cpu_recompiler_block_linking != old_settings.cpu_recompiler_block_linking ||
         g_settings.cpu_recompiler_icache != old_settings.cpu_recompiler_icache))
    {
      // changing memory exceptions can re-enable fastmem
      if (g_settings.cpu_recompiler_memory_exceptions != old_settings.cpu_recompiler_memory_exceptions)
        CPU::CodeCache::Reinitialize();
      else
        CPU::CodeCache::Flush();

      if (g_settings.cpu_recompiler_icache != old_settings.cpu_recompiler_icache)
        CPU::ClearICache();
    }

    if (g_settings.gpu_resolution_scale != old_settings.gpu_resolution_scale ||
        g_settings.gpu_multisamples != old_settings.gpu_multisamples ||
        g_settings.gpu_per_sample_shading != old_settings.gpu_per_sample_shading ||
        g_settings.gpu_use_thread != old_settings.gpu_use_thread ||
        g_settings.gpu_use_software_renderer_for_readbacks != old_settings.gpu_use_software_renderer_for_readbacks ||
        g_settings.gpu_fifo_size != old_settings.gpu_fifo_size ||
        g_settings.gpu_max_run_ahead != old_settings.gpu_max_run_ahead ||
        g_settings.gpu_true_color != old_settings.gpu_true_color ||
        g_settings.gpu_scaled_dithering != old_settings.gpu_scaled_dithering ||
        g_settings.gpu_texture_filter != old_settings.gpu_texture_filter ||
        g_settings.gpu_disable_interlacing != old_settings.gpu_disable_interlacing ||
        g_settings.gpu_force_ntsc_timings != old_settings.gpu_force_ntsc_timings ||
        g_settings.gpu_24bit_chroma_smoothing != old_settings.gpu_24bit_chroma_smoothing ||
        g_settings.gpu_downsample_mode != old_settings.gpu_downsample_mode ||
        g_settings.display_crop_mode != old_settings.display_crop_mode ||
        g_settings.display_aspect_ratio != old_settings.display_aspect_ratio ||
        g_settings.gpu_pgxp_enable != old_settings.gpu_pgxp_enable ||
        g_settings.gpu_pgxp_texture_correction != old_settings.gpu_pgxp_texture_correction ||
        g_settings.gpu_pgxp_color_correction != old_settings.gpu_pgxp_color_correction ||
        g_settings.gpu_pgxp_depth_buffer != old_settings.gpu_pgxp_depth_buffer ||
        g_settings.display_active_start_offset != old_settings.display_active_start_offset ||
        g_settings.display_active_end_offset != old_settings.display_active_end_offset ||
        g_settings.display_line_start_offset != old_settings.display_line_start_offset ||
        g_settings.display_line_end_offset != old_settings.display_line_end_offset ||
        g_settings.rewind_enable != old_settings.rewind_enable ||
        g_settings.runahead_frames != old_settings.runahead_frames)
    {
      g_gpu->UpdateSettings();
    }

    if (g_settings.gpu_widescreen_hack != old_settings.gpu_widescreen_hack ||
        g_settings.display_aspect_ratio != old_settings.display_aspect_ratio ||
        (g_settings.display_aspect_ratio == DisplayAspectRatio::Custom &&
         (g_settings.display_aspect_ratio_custom_numerator != old_settings.display_aspect_ratio_custom_numerator ||
          g_settings.display_aspect_ratio_custom_denominator != old_settings.display_aspect_ratio_custom_denominator)))
    {
      GTE::UpdateAspectRatio();
    }

    if (g_settings.gpu_pgxp_enable != old_settings.gpu_pgxp_enable ||
        (g_settings.gpu_pgxp_enable && (g_settings.gpu_pgxp_culling != old_settings.gpu_pgxp_culling ||
                                        g_settings.gpu_pgxp_vertex_cache != old_settings.gpu_pgxp_vertex_cache ||
                                        g_settings.gpu_pgxp_cpu != old_settings.gpu_pgxp_cpu)))
    {
      if (g_settings.IsUsingCodeCache())
      {
        CPU::CodeCache::Flush();
      }

      if (old_settings.gpu_pgxp_enable)
        PGXP::Shutdown();

      if (g_settings.gpu_pgxp_enable)
        PGXP::Initialize();
    }

    if (g_settings.cdrom_readahead_sectors != old_settings.cdrom_readahead_sectors)
      g_cdrom.SetReadaheadSectors(g_settings.cdrom_readahead_sectors);

    if (g_settings.memory_card_types != old_settings.memory_card_types ||
        g_settings.memory_card_paths != old_settings.memory_card_paths ||
        (g_settings.memory_card_use_playlist_title != old_settings.memory_card_use_playlist_title &&
         System::HasMediaSubImages()) ||
        g_settings.memory_card_directory != old_settings.memory_card_directory)
    {
      System::UpdateMemoryCardTypes();
    }

    if (g_settings.rewind_enable != old_settings.rewind_enable ||
        g_settings.rewind_save_frequency != old_settings.rewind_save_frequency ||
        g_settings.rewind_save_slots != old_settings.rewind_save_slots ||
        g_settings.runahead_frames != old_settings.runahead_frames)
    {
      System::UpdateMemorySaveStateSettings();
    }

    if (g_settings.texture_replacements.enable_vram_write_replacements !=
          old_settings.texture_replacements.enable_vram_write_replacements ||
        g_settings.texture_replacements.preload_textures != old_settings.texture_replacements.preload_textures)
    {
      g_texture_replacements.Reload();
    }

    g_dma.SetMaxSliceTicks(g_settings.dma_max_slice_ticks);
    g_dma.SetHaltTicks(g_settings.dma_halt_ticks);
  }

  bool controllers_updated = false;
  for (u32 i = 0; i < NUM_CONTROLLER_AND_CARD_PORTS; i++)
  {
    if (g_settings.controller_types[i] != old_settings.controller_types[i])
    {
      if (!System::IsShutdown() && !controllers_updated)
      {
        System::UpdateControllers();
        System::ResetControllers();
        UpdateSoftwareCursor();
        controllers_updated = true;
      }

      OnControllerTypeChanged(i);
    }

    if (!System::IsShutdown() && !controllers_updated)
    {
      System::UpdateControllerSettings();
      UpdateSoftwareCursor();
    }
  }

  if (g_settings.multitap_mode != old_settings.multitap_mode)
    System::UpdateMultitaps();
}

std::string HostInterface::GetUserDirectoryRelativePath(const char* format, ...) const
{
  std::va_list ap;
  va_start(ap, format);
  std::string formatted_path = StringUtil::StdStringFromFormatV(format, ap);
  va_end(ap);

  if (m_user_directory.empty())
    return formatted_path;
  return StringUtil::StdStringFromFormat("%s" FS_OSPATH_SEPARATOR_STR "%s", m_user_directory.c_str(),
                                           formatted_path.c_str());
}

std::string HostInterface::GetProgramDirectoryRelativePath(const char* format, ...) const
{
  std::va_list ap;
  va_start(ap, format);
  std::string formatted_path = StringUtil::StdStringFromFormatV(format, ap);
  va_end(ap);

  if (m_program_directory.empty())
    return formatted_path;
  return StringUtil::StdStringFromFormat("%s" FS_OSPATH_SEPARATOR_STR "%s", m_program_directory.c_str(),
		  formatted_path.c_str());
}

std::string HostInterface::GetSharedMemoryCardPath(u32 slot) const
{
  if (g_settings.memory_card_directory.empty())
    return GetUserDirectoryRelativePath("memcards" FS_OSPATH_SEPARATOR_STR "shared_card_%u.mcd", slot + 1);
  return StringUtil::StdStringFromFormat("%s" FS_OSPATH_SEPARATOR_STR "shared_card_%u.mcd",
                                           g_settings.memory_card_directory.c_str(), slot + 1);
}

std::string HostInterface::GetGameMemoryCardPath(const char* game_code, u32 slot) const
{
  if (g_settings.memory_card_directory.empty())
    return GetUserDirectoryRelativePath("memcards" FS_OSPATH_SEPARATOR_STR "%s_%u.mcd", game_code, slot + 1);
  return StringUtil::StdStringFromFormat("%s" FS_OSPATH_SEPARATOR_STR "%s_%u.mcd",
                                           g_settings.memory_card_directory.c_str(), game_code, slot + 1);
}

bool HostInterface::GetBoolSettingValue(const char* section, const char* key, bool default_value /*= false*/)
{
  std::string value = GetStringSettingValue(section, key, "");
  if (value.empty())
    return default_value;

  std::optional<bool> bool_value = StringUtil::FromChars<bool>(value);
  return bool_value.value_or(default_value);
}

int HostInterface::GetIntSettingValue(const char* section, const char* key, int default_value /*= 0*/)
{
  std::string value = GetStringSettingValue(section, key, "");
  if (value.empty())
    return default_value;

  std::optional<int> int_value = StringUtil::FromChars<int>(value);
  return int_value.value_or(default_value);
}

float HostInterface::GetFloatSettingValue(const char* section, const char* key, float default_value /*= 0.0f*/)
{
  std::string value = GetStringSettingValue(section, key, "");
  if (value.empty())
    return default_value;

  std::optional<float> float_value = StringUtil::FromChars<float>(value);
  return float_value.value_or(default_value);
}

TinyString HostInterface::TranslateString(const char* context, const char* str,
                                          const char* disambiguation /*= nullptr*/, int n /*= -1*/) const
{
  TinyString result(str);
  if (n >= 0)
  {
    const std::string number = std::to_string(n);
    result.Replace("%n", number.c_str());
    result.Replace("%Ln", number.c_str());
  }
  return result;
}

std::string HostInterface::TranslateStdString(const char* context, const char* str,
                                              const char* disambiguation /*= nullptr*/, int n /*= -1*/) const
{
  std::string result(str);
  if (n >= 0)
  {
    const std::string number = std::to_string(n);
    // Made to mimick Qt's behaviour
    // https://github.com/qt/qtbase/blob/255459250d450286a8c5c492dab3f6d3652171c9/src/corelib/kernel/qcoreapplication.cpp#L2099
    size_t percent_pos = 0;
    size_t len = 0;
    while ((percent_pos = result.find('%', percent_pos + len)) != std::string::npos)
    {
      len = 1;
      if (percent_pos + len == result.length())
        break;
      if (result[percent_pos + len] == 'L')
      {
        ++len;
        if (percent_pos + len == result.length())
          break;
      }
      if (result[percent_pos + len] == 'n')
      {
        ++len;
        result.replace(percent_pos, len, number);
        len = number.length();
      }
    }
  }

  return result;
}

void HostInterface::ToggleSoftwareRendering()
{
  if (System::IsShutdown() || g_settings.gpu_renderer == GPURenderer::Software)
    return;

  const GPURenderer new_renderer = g_gpu->IsHardwareRenderer() ? GPURenderer::Software : g_settings.gpu_renderer;

  System::RecreateGPU(new_renderer);
}

void HostInterface::UpdateSoftwareCursor()
{
  if (System::IsShutdown())
  {
    SetMouseMode(false, false);
    m_display->ClearSoftwareCursor();
    return;
  }

  const Common::RGBA8Image* image = nullptr;
  float image_scale = 1.0f;
  bool relative_mode = false;
  bool hide_cursor = false;

  for (u32 i = 0; i < NUM_CONTROLLER_AND_CARD_PORTS; i++)
  {
    Controller* controller = System::GetController(i);
    if (controller && controller->GetSoftwareCursor(&image, &image_scale, &relative_mode))
    {
      hide_cursor = true;
      break;
    }
  }

  SetMouseMode(relative_mode, hide_cursor);

  if (image && image->IsValid())
  {
    m_display->SetSoftwareCursor(image->GetPixels(), image->GetWidth(), image->GetHeight(), image->GetByteStride(),
                                 image_scale);
  }
  else
  {
    m_display->ClearSoftwareCursor();
  }
}

void HostInterface::RecreateSystem()
{
  std::unique_ptr<ByteStream> stream = ByteStream_CreateGrowableMemoryStream(nullptr, 8 * 1024);
  if (!System::SaveState(stream.get()) || !stream->SeekAbsolute(0))
  {
    ReportError("Failed to save state before system recreation. Shutting down.");
    DestroySystem();
    return;
  }

  DestroySystem();

  auto boot_params = std::make_shared<SystemBootParameters>();
  boot_params->state_stream = std::move(stream);
  if (!BootSystem(std::move(boot_params)))
  {
    ReportError("Failed to boot system after recreation.");
    return;
  }
}
