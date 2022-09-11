#include "game_settings.h"
#include "common/log.h"
#include "common/string.h"
#include "common/string_util.h"
#include "core/host_interface.h"
#include "core/settings.h"
#include <array>
#include <utility>
Log_SetChannel(GameSettings);

#ifdef _WIN32
#include "common/windows_headers.h"
#endif

namespace GameSettings {

void Entry::ApplySettings(bool display_osd_messages) const
{
  constexpr float osd_duration = 10.0f;

  if (runahead_frames.has_value())
    g_settings.runahead_frames = runahead_frames.value();
  if (cpu_overclock_numerator.has_value())
    g_settings.cpu_overclock_numerator = cpu_overclock_numerator.value();
  if (cpu_overclock_denominator.has_value())
    g_settings.cpu_overclock_denominator = cpu_overclock_denominator.value();
  if (cpu_overclock_enable.has_value())
    g_settings.cpu_overclock_enable = cpu_overclock_enable.value();
  if (enable_8mb_ram.has_value())
    g_settings.enable_8mb_ram = enable_8mb_ram.value();
  g_settings.UpdateOverclockActive();

  if (cdrom_read_speedup.has_value())
    g_settings.cdrom_read_speedup = cdrom_read_speedup.value();
  if (cdrom_seek_speedup.has_value())
    g_settings.cdrom_seek_speedup = cdrom_seek_speedup.value();

  if (display_active_start_offset.has_value())
    g_settings.display_active_start_offset = display_active_start_offset.value();
  if (display_active_end_offset.has_value())
    g_settings.display_active_end_offset = display_active_end_offset.value();
  if (display_line_start_offset.has_value())
    g_settings.display_line_start_offset = display_line_start_offset.value();
  if (display_line_end_offset.has_value())
    g_settings.display_line_end_offset = display_line_end_offset.value();
  if (dma_max_slice_ticks.has_value())
    g_settings.dma_max_slice_ticks = dma_max_slice_ticks.value();
  if (dma_halt_ticks.has_value())
    g_settings.dma_halt_ticks = dma_halt_ticks.value();
  if (gpu_fifo_size.has_value())
    g_settings.gpu_fifo_size = gpu_fifo_size.value();
  if (gpu_max_run_ahead.has_value())
    g_settings.gpu_max_run_ahead = gpu_max_run_ahead.value();
  if (gpu_pgxp_tolerance.has_value())
    g_settings.gpu_pgxp_tolerance = gpu_pgxp_tolerance.value();
  if (gpu_pgxp_depth_threshold.has_value())
    g_settings.SetPGXPDepthClearThreshold(gpu_pgxp_depth_threshold.value());

  if (display_crop_mode.has_value())
    g_settings.display_crop_mode = display_crop_mode.value();
  if (display_aspect_ratio.has_value())
    g_settings.display_aspect_ratio = display_aspect_ratio.value();
  if (display_aspect_ratio_custom_numerator.has_value())
    g_settings.display_aspect_ratio_custom_numerator = display_aspect_ratio_custom_numerator.value();
  if (display_aspect_ratio_custom_denominator.has_value())
    g_settings.display_aspect_ratio_custom_denominator = display_aspect_ratio_custom_denominator.value();
  if (gpu_downsample_mode.has_value())
    g_settings.gpu_downsample_mode = gpu_downsample_mode.value();
  if (display_linear_upscaling.has_value())
    g_settings.display_linear_filtering = display_linear_upscaling.value();
  if (display_force_4_3_for_24bit.has_value())
    g_settings.display_force_4_3_for_24bit = display_force_4_3_for_24bit.value();

  if (gpu_renderer.has_value())
    g_settings.gpu_renderer = gpu_renderer.value();
  if (gpu_resolution_scale.has_value())
    g_settings.gpu_resolution_scale = gpu_resolution_scale.value();
  if (gpu_multisamples.has_value())
    g_settings.gpu_multisamples = gpu_multisamples.value();
  if (gpu_per_sample_shading.has_value())
    g_settings.gpu_per_sample_shading = gpu_per_sample_shading.value();
  if (gpu_true_color.has_value())
    g_settings.gpu_true_color = gpu_true_color.value();
  if (gpu_scaled_dithering.has_value())
    g_settings.gpu_scaled_dithering = gpu_scaled_dithering.value();
  if (gpu_force_ntsc_timings.has_value())
    g_settings.gpu_force_ntsc_timings = gpu_force_ntsc_timings.value();
  if (gpu_texture_filter.has_value())
    g_settings.gpu_texture_filter = gpu_texture_filter.value();
  if (gpu_widescreen_hack.has_value())
    g_settings.gpu_widescreen_hack = gpu_widescreen_hack.value();
  if (gpu_pgxp.has_value())
    g_settings.gpu_pgxp_enable = gpu_pgxp.value();
  if (gpu_pgxp_projection_precision.has_value())
    g_settings.gpu_pgxp_preserve_proj_fp = gpu_pgxp_projection_precision.value();
  if (gpu_pgxp_depth_buffer.has_value())
    g_settings.gpu_pgxp_depth_buffer = gpu_pgxp_depth_buffer.value();

  if (multitap_mode.has_value())
    g_settings.multitap_mode = multitap_mode.value();
  if (controller_1_type.has_value())
    g_settings.controller_types[0] = controller_1_type.value();
  if (controller_2_type.has_value())
    g_settings.controller_types[1] = controller_2_type.value();

  if (memory_card_1_type.has_value())
    g_settings.memory_card_types[0] = memory_card_1_type.value();
  if (memory_card_2_type.has_value())
    g_settings.memory_card_types[1] = memory_card_2_type.value();

  if (HasTrait(Trait::ForceInterpreter))
  {
    if (display_osd_messages && g_settings.cpu_execution_mode != CPUExecutionMode::Interpreter)
    {
      g_host_interface->AddOSDMessage(
        g_host_interface->TranslateStdString("OSDMessage", "CPU interpreter forced by game settings."), osd_duration);
    }

    g_settings.cpu_execution_mode = CPUExecutionMode::Interpreter;
  }

  if (HasTrait(Trait::ForceSoftwareRenderer))
  {
    if (display_osd_messages && g_settings.gpu_renderer != GPURenderer::Software)
    {
      g_host_interface->AddOSDMessage(
        g_host_interface->TranslateStdString("OSDMessage", "Software renderer forced by game settings."), osd_duration);
    }

    g_settings.gpu_renderer = GPURenderer::Software;
  }

  if (HasTrait(Trait::ForceInterlacing))
  {
    if (display_osd_messages && g_settings.gpu_disable_interlacing)
    {
      g_host_interface->AddOSDMessage(
        g_host_interface->TranslateStdString("OSDMessage", "Interlacing forced by game settings."), osd_duration);
    }

    g_settings.gpu_disable_interlacing = false;
  }

  if (HasTrait(Trait::DisableTrueColor))
  {
    if (display_osd_messages && g_settings.gpu_true_color)
    {
      g_host_interface->AddOSDMessage(
        g_host_interface->TranslateStdString("OSDMessage", "True color disabled by game settings."), osd_duration);
    }

    g_settings.gpu_true_color = false;
  }

  if (HasTrait(Trait::DisableUpscaling))
  {
    if (display_osd_messages && g_settings.gpu_resolution_scale > 1)
    {
      g_host_interface->AddOSDMessage(
        g_host_interface->TranslateStdString("OSDMessage", "Upscaling disabled by game settings."), osd_duration);
    }

    g_settings.gpu_resolution_scale = 1;
  }

  if (HasTrait(Trait::DisableScaledDithering))
  {
    if (display_osd_messages && g_settings.gpu_scaled_dithering)
    {
      g_host_interface->AddOSDMessage(
        g_host_interface->TranslateStdString("OSDMessage", "Scaled dithering disabled by game settings."),
        osd_duration);
    }

    g_settings.gpu_scaled_dithering = false;
  }

  if (HasTrait(Trait::DisableWidescreen))
  {
    if (display_osd_messages &&
        (g_settings.display_aspect_ratio == DisplayAspectRatio::R16_9 || g_settings.gpu_widescreen_hack))
    {
      g_host_interface->AddOSDMessage(
        g_host_interface->TranslateStdString("OSDMessage", "Widescreen disabled by game settings."), osd_duration);
    }

    g_settings.display_aspect_ratio = DisplayAspectRatio::R4_3;
    g_settings.gpu_widescreen_hack = false;
  }

  if (HasTrait(Trait::DisableForceNTSCTimings))
  {
    if (display_osd_messages && g_settings.gpu_force_ntsc_timings)
    {
      g_host_interface->AddOSDMessage(
        g_host_interface->TranslateStdString("OSDMessage", "Forcing NTSC Timings disallowed by game settings."),
        osd_duration);
    }

    g_settings.gpu_force_ntsc_timings = false;
  }

  if (HasTrait(Trait::DisablePGXP))
  {
    if (display_osd_messages && g_settings.gpu_pgxp_enable)
    {
      g_host_interface->AddOSDMessage(
        g_host_interface->TranslateStdString("OSDMessage", "PGXP geometry correction disabled by game settings."),
        osd_duration);
    }

    g_settings.gpu_pgxp_enable = false;
  }

  if (HasTrait(Trait::DisablePGXPCulling))
  {
    if (display_osd_messages && g_settings.gpu_pgxp_enable && g_settings.gpu_pgxp_culling)
    {
      g_host_interface->AddOSDMessage(
        g_host_interface->TranslateStdString("OSDMessage", "PGXP culling disabled by game settings."), osd_duration);
    }

    g_settings.gpu_pgxp_culling = false;
  }

  if (HasTrait(Trait::DisablePGXPTextureCorrection))
  {
    if (display_osd_messages && g_settings.gpu_pgxp_enable && g_settings.gpu_pgxp_texture_correction)
    {
      g_host_interface->AddOSDMessage(
        g_host_interface->TranslateStdString("OSDMessage", "PGXP texture correction disabled by game settings."),
        osd_duration);
    }

    g_settings.gpu_pgxp_texture_correction = false;
  }

  if (HasTrait(Trait::ForcePGXPVertexCache))
  {
    if (display_osd_messages && g_settings.gpu_pgxp_enable && !g_settings.gpu_pgxp_vertex_cache)
    {
      g_host_interface->AddOSDMessage(
        g_host_interface->TranslateStdString("OSDMessage", "PGXP vertex cache forced by game settings."), osd_duration);
    }

    g_settings.gpu_pgxp_vertex_cache = true;
  }

  if (HasTrait(Trait::ForcePGXPCPUMode))
  {
    if (display_osd_messages && g_settings.gpu_pgxp_enable && !g_settings.gpu_pgxp_cpu)
    {
      g_host_interface->AddOSDMessage(
        g_host_interface->TranslateStdString("OSDMessage", "PGXP CPU mode forced by game settings."), osd_duration);
    }

    g_settings.gpu_pgxp_cpu = true;
  }

  if (HasTrait(Trait::DisablePGXPDepthBuffer))
  {
    if (display_osd_messages && g_settings.gpu_pgxp_enable && g_settings.gpu_pgxp_depth_buffer)
    {
      g_host_interface->AddOSDMessage(
        g_host_interface->TranslateStdString("OSDMessage", "PGXP Depth Buffer disabled by game settings."),
        osd_duration);
    }

    g_settings.gpu_pgxp_depth_buffer = false;
  }

  if (HasTrait(Trait::ForceSoftwareRenderer))
  {
    Log_WarningPrint("Using software renderer for readbacks.");
    g_settings.gpu_renderer = GPURenderer::Software;
  }

  if (HasTrait(Trait::ForceRecompilerMemoryExceptions))
  {
    Log_WarningPrint("Memory exceptions for recompiler forced by game settings.");
    g_settings.cpu_recompiler_memory_exceptions = true;
  }

  if (HasTrait(Trait::ForceRecompilerICache))
  {
    Log_WarningPrint("ICache for recompiler forced by game settings.");
    g_settings.cpu_recompiler_icache = true;
  }

  if (g_settings.cpu_fastmem_mode == CPUFastmemMode::MMap && HasTrait(Trait::ForceRecompilerLUTFastmem))
  {
    Log_WarningPrint("LUT fastmem for recompiler forced by game settings.");
    g_settings.cpu_fastmem_mode = CPUFastmemMode::LUT;
  }
}

} // namespace GameSettings
