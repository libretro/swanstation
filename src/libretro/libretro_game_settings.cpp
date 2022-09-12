#include "libretro_game_settings.h"
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

std::unique_ptr<GameSettings::Entry> GetSettingsForGame(const std::string& game_code)
{
  std::unique_ptr<GameSettings::Entry> gs = std::make_unique<GameSettings::Entry>();

  /* These games use a software renderer using hardware lines
   * It cannot be upscaled, and PGXP has to be disabled
   */
  if (   game_code == "SLUS-00077" /* Doom (NTSC-U)        */
      || game_code == "SLES-00132" /* Doom (PAL)           */
      || game_code == "SLPS-00308" /* Doom (NTSC-J)        */
      || game_code == "SLUS-00331" /* Final Doom (NTSC-U)  */
      || game_code == "SLES-00487" /* Final Doom (PAL)     */
      || game_code == "SLPS-00727" /* Final Doom (NTSC-J)  */
      || game_code == "SLES-00703" /* Duke Nukem (PAL)     */
      || game_code == "SLES-00987" /* Duke Nukem (PAL)     */
      || game_code == "SLES-01027" /* Duke Nukem (PAL-FR)  */
      || game_code == "SLPS-01557" /* Duke Nukem - Total Meltdown (NTSC-J)  */
      || game_code == "SLUS-00355" /* Duke Nukem - Total Meltdown (NTSC-U)  */
      || game_code == "SLES-00081" /* Defcon 5 (PAL)  */
      || game_code == "SLES-00146" /* Defcon 5 (PAL)  */
      || game_code == "SLES-00148" /* Defcon 5 (PAL)  */
      || game_code == "SLES-00149" /* Defcon 5 (PAL)  */
      || game_code == "SLPS-00275" /* Defcon 5 (NTSC-J)  */
      || game_code == "SLES-00147" /* Defcon 5 (PAL)  */
      || game_code == "SLUS-00009" /* Defcon 5 (NTSC-U)  */
      || game_code == "SLUS-00297" /* Star Wars - Dark Forces (NTSC-U)  */
     ) 
  {
    gs->AddTrait(GameSettings::Trait::DisableUpscaling);
    gs->AddTrait(GameSettings::Trait::DisablePGXP);
    return gs;
  }

  if (   game_code == "SLPM-87089" /* Pop'n Music 6 (NTSC-J)    */
      || game_code == "SLPS-03336" /* Mr. Driller G (NTSC-J)    */
      || game_code == "SLUS-00952" /* Arcade Party Pak (NTSC-U) */
      || game_code == "SCES-01312" /* Devil Dice (PAL)          */
      || game_code == "SLUS-00337" /* True Pinball (NTSC-U)     */
      || game_code == "SLPS-03553" /* Naruto - Shinobi no Sato no Jintori Kassen (NTSC-J)     */
      || game_code == "SLPS-01211" /* Time Bokan Series - Bokandesuyo (NTSC-J)     */
      || game_code == "SLUS-00656" /* Rat Attack (NTSC-U)       */
      || game_code == "SLES-00483" /* Worms Pinball (PAL)       */
      || game_code == "SLUS-00912" /* Destruction Derby Raw (NTSC-U) */
      || game_code == "SLPS-01434" /* 3D Kakutou Tsukuru    (NTSC-J) */
      || game_code == "SLPM-86750" /* Shiritsu Justice Gakuen - Nekketsu Seishun Nikki 2 [Capkore] (NTSC-J) */
      || game_code == "SLPS-02120" /* Shiritsu Justice Gakuen - Nekketsu Seishun Nikki 2 (NTSC-J) */
     )
  {
    gs->AddTrait(GameSettings::Trait::ForceInterlacing);
    return gs;
  }

  if (   game_code == "SLUS-01260" /* Pro Pinball Big Race USA (NTSC-U) */
      || game_code == "SLES-01211" /* Pro Pinball Big Race USA (PAL) */
      || game_code == "SLUS-01261" /* Pro Pinball - Fantastic Journey (NTSC-U) */
      || game_code == "SLES-02466" /* Pro Pinball - Fantastic Journey (PAL)    */
      || game_code == "SLES-00639" /* Pro Pinball: Timeshock! (NTSC-U) */
      || game_code == "SLES-90039" /* Pro Pinball: Timeshock! (NTSC-U) */
      || game_code == "SLES-00606" /* Pro Pinball: Timeshock! (PAL) */
      || game_code == "SLES-00259" /* Pro Pinball - The Web (PAL) */
     )
  {
    gs->AddTrait(GameSettings::Trait::ForceSoftwareRenderer);
    gs->AddTrait(GameSettings::Trait::ForceInterlacing);
    return gs;
  }

  if (game_code == "SCPS-10126") /* Addie No Okurimono - To Moze from Addie (NTSC-J) */
  {
    gs->AddTrait(GameSettings::Trait::ForceSoftwareRenderer);
    return gs;
  }

  if (game_code == "SLPS-00078") /* Gakkou no kowai uwasa - Hanako Sangakita!! (NTSC-J) */
  {
    gs->AddTrait(GameSettings::Trait::DisableTrueColor);
    return gs;
  }

  if (   game_code == "SCED-01979" /* Formula One '99 (PAL) */
      || game_code == "SCES-02222" /* Formula One '99 (PAL) */
      || game_code == "SCES-01979" /* Formula One '99 (PAL) */
      || game_code == "SCPS-10101" /* Formula One '99 (NTSC-J) */
      || game_code == "SLUS-00870" /* Formula One '99 (NTSC-U) */
      || game_code == "SCES-02777" /* Formula One 2000 (PAL) */
      || game_code == "SCES-02779" /* Formula One 2000 (I-S) */
      || game_code == "SCES-02778" /* Formula One 2000 (PAL) */
      || game_code == "SLUS-01134" /* Formula One 2000 (NTSC-U) */
      || game_code == "SCES-03404" /* Formula One 2001 (PAL) */
      || game_code == "SCES-03423" /* Formula One 2001 (PAL) */
      || game_code == "SCES-03424" /* Formula One 2001 (PAL) */
      || game_code == "SCES-03524" /* Formula One 2001 (PAL) */
      || game_code == "SCES-03886" /* Formula One Arcade (PAL) */
      || game_code == "SLED-00491" /* Formula One [Demo] (PAL) */
      || game_code == "SLUS-00684" /* Jackie Chan's Stuntmaster (NTSC-U) */
      || game_code == "SCES-01444" /* Jackie Chan's Stuntmaster (PAL) */
     )
  {
    gs->AddTrait(GameSettings::Trait::ForceInterpreter);
    return gs;
  }

  if (game_code == "SLPS-02361") /* Touge Max G (NTSC-J) */
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPVertexCache);
    return gs;
  }

  if (   game_code == "SLES-03868" /* Marcel Desailly Pro Football (PAL) */
      || game_code == "SLED-02439" /* Compilation 03 */
      || game_code == "SLPS-01762" /* Pepsiman (NTSC-J) */
      || game_code == "SLPS-00935" /* Choukousoku GranDoll (NTSC-J) */
      || game_code == "SLPS-00870" /* Choukousoku GranDoll (NTSC-J) */
      || game_code == "SLPS-00869" /* Choukousoku GranDoll (NTSC-J) */
     )
  {
    gs->AddTrait(GameSettings::Trait::DisablePGXPCulling);
    return gs;
  }

  if (   game_code == "SCES-02835" /* Spyro - Year Of The Dragon (PAL) */
      || game_code == "SCES-02104" /* Spyro 2 - Gateway To Glimmer (PAL) */
      || game_code == "SCUS-94467"
      || game_code == "SCUS-94425"
      || game_code == "SCPS-10085" /* Spyro The Dragon (NTSC-J) */
      || game_code == "SCUS-94290"
      || game_code == "SLES-02397"
      || game_code == "SLES-12397"
      || game_code == "SLES-02398"
      || game_code == "SLES-12398"
      || game_code == "SLES-02399"
      || game_code == "SLES-12399" /* Grandia (PAL) */
      || game_code == "SLPS-02124" /* Grandia (NTSC-J) */
      || game_code == "SLPS-91205" /* Grandia [PlayStation The Best] (NTSC-J) */
      || game_code == "SLPS-02125" /* Grandia (NTSC-J) */
      || game_code == "SLPS-91206" /* Grandia [PlayStation The Best] (NTSC-J) */
      || game_code == "SCUS-94457" /* Grandia (NTSC-U) */
      || game_code == "SCUS-94465" /* Grandia (NTSC-U) */
      || game_code == "SLPM-80297" /* Grandia Prelude Taikenban (NTSC-J) */
      || game_code == "SLES-00593" /* Croc - Legend of the Gobbos (PAL) */
      || game_code == "SLED-00038" /* Croc - Legend of the Gobbos [Demo] (PAL) */
      || game_code == "SLUS-00530" /* Croc - Legend of the Gobbos (NTSC-U) */
      || game_code == "SLED-02119"
      || game_code == "SLES-02088"
      || game_code == "SLUS-00634"
      || game_code == "SLUS-90056"
      || game_code == "SLPM-86310"
      || game_code == "SLPM-80473"
      || game_code == "SLPS-01055"
      || game_code == "SLPM-80173"
      || game_code == "SLES-02600"
      || game_code == "SLUS-01017"
      || game_code == "SLES-02602"
      || game_code == "SCPS-10115"
      || game_code == "PAPX-90097"
      || game_code == "SLES-02601"
      || game_code == "SCES-03000"
      || game_code == "SCUS-94569"
      || game_code == "SLES-03972"
      || game_code == "SLES-03974"
      || game_code == "SLES-03973"
      || game_code == "SLES-03975"
      || game_code == "SLUS-01503"
      || game_code == "SLUS-01417"
      || game_code == "SLES-03662"
      || game_code == "SLES-03665" /* Harry Potter and the Philosopher's Stone (PAL) */
      || game_code == "SLES-03663" /* Harry Potter and the Philosopher's Stone (PAL) */
      || game_code == "SLES-03664" /* Harry Potter & The Philosopher's Stone (PAL) */
      || game_code == "SLUS-01415" /* Harry Potter & The Sorcerer's Stone (NTSC-U) */
      || game_code == "SLES-03976" /* Harry Potter ja Salaisuuksien Kammio (PAL) */
      || game_code == "SLPS-03492" /* Harry Potter to Himitsu no Heya (NTSC-J) */
      || game_code == "SLPS-03355" /* Harry Potter to Kenja no Ishi (NTSC-J) */
      || game_code == "SLPM-84013" /* Harry Potter to Kenja no Ishi Coca-Cola Version (NTSC-J) */
     )
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (   game_code == "SCES-01438" /* Spyro The Dragon (PAL)    */ 
      || game_code == "SCUS-94228" /* Spyro The Dragon (NTSC-U) */
     )
  {
    gs->AddTrait(GameSettings::Trait::DisablePGXPCulling);
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SCPS-45404") /* Racing Lagoon (NTSC-J) */
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    gs->AddTrait(GameSettings::Trait::ForceRecompilerLUTFastmem);
    return gs;
  }

  if (game_code == "SLPS-02376") /* Little Princess - Maru Oukoko No Ningyou Hime 2 (NTSC-J) */
  {
    gs->dma_max_slice_ticks = 100;
    gs->gpu_max_run_ahead = 1;
    return gs;
  }

  if (   game_code == "SLES-10643" /* Star Wars - Rebel Assault II - The Hidden Empire (PAL) */
      || game_code == "SLPS-00638" /* Star Wars - Rebel Assault II - The Hidden Empire (NTSC-J) */
      || game_code == "SLPS-00639" /* Star Wars - Rebel Assault II - The Hidden Empire (NTSC-J) */
      || game_code == "SLES-00644" /* Star Wars - Rebel Assault II - The Hidden Empire (PAL) */
      || game_code == "SLES-10644" /* Star Wars - Rebel Assault II - The Hidden Empire (PAL) */
      || game_code == "SLUS-00381" /* Star Wars - Rebel Assault II - The Hidden Empire (NTSC-U) */
      || game_code == "SLUS-00386" /* Star Wars - Rebel Assault II - The Hidden Empire (NTSC-U) */
      || game_code == "SLES-00654" /* Star Wars - Rebel Assault II - The Hidden Empire (PAL) */
      || game_code == "SLES-10654" /* Star Wars - Rebel Assault II - The Hidden Empire (PAL) */
      || game_code == "SLES-10656" /* Star Wars - Rebel Assault II - The Hidden Empire (PAL) */
      || game_code == "SLES-00584" /* Star Wars - Rebel Assault II - The Hidden Empire (PAL) */
      || game_code == "SLES-10584" /* Star Wars - Rebel Assault II - The Hidden Empire (PAL) */
      || game_code == "SLES-00643" /* Star Wars - Rebel Assault II - The Hidden Empire (PAL) */
      || game_code == "SLES-00656" /* Star Wars - Rebel Assault II - The Hidden Empire (PAL) */
      || game_code == "SLES-00056" /* Rock & Roll Racing 2 - Red Asphalt (PAL) */
      || game_code == "SLUS-00282" /* Red Asphalt (NTSC-U) */
      || game_code == "SLUS-01138" /* Vampire Hunter D (NTSC-U) */
     ) 
  {
    gs->dma_max_slice_ticks = 200;
    gs->gpu_max_run_ahead = 1;
    return gs;
  }

  if (   game_code == "SLUS-00022" /* Slam'n'Jam '96 Featuring Magic & Kareem */
      || game_code == "SLUS-00348" /* Hexen (NTSC-U) */
     )
  {
    gs->AddTrait(GameSettings::Trait::DisableUpscaling);
    return gs;
  }

  if (   game_code == "SLUS-00232" /* Pandemonium! (NTSC-U) */
      || game_code == "SLES-00526" /* Pandemonium! (PAL)    */
      || game_code == "SLED-00570" /* Pandemonium! (Demo Disc)(PAL)    */
      || game_code == "SLUS-00547" /* Adidas Power Soccer '98 (NTSC-U) */
      || game_code == "SLES-01239" /* Adidas Power Soccer '98 (PAL)    */
      || game_code == "SLED-01311" /* Adidas Power Soccer '98 (PAL)    */
      || game_code == "SLED-01310" /* Adidas Power Soccer '98 (PAL-FR) */
      || game_code == "SLPS-00900" /* Armored Core (NTSC-J) */
      || game_code == "SLPS-03581" /* Armored Core [Premium Box] (NTSC-J) */
      || game_code == "SLPS-91064" /* Armored Core [PlayStation The Best] (NTSC-J) */
      || game_code == "SCUS-94182" /* Armored Core (NTSC-U) */
      || game_code == "SLUS-01323" /* Armored Core [Reprint] (NTSC-U) */
      || game_code == "SCES-00842" /* Armored Core (PAL) */
      || game_code == "SLUS-00524" /* Road Rash 3D (NTSC-U) */
      || game_code == "SLES-00910" /* Road Rash 3D (PAL) */
      || game_code == "SLES-01157" /* Road Rash 3D (PAL) */
      || game_code == "SLES-01158" /* Road Rash 3D (PAL) */
) 
  {
    gs->dma_max_slice_ticks = 100;
    return gs;
  }

  if (   game_code == "SLPM-87395" /* Chrono Cross            [Ultimate Hits] (NTSC-J) */
      || game_code == "SLPM-87396" /* Chrono Cross (Disc 2/2) [Ultimate Hits] (NTSC-J) */
      || game_code == "SLPS-02364" /* Chrono Cross            (NTSC-J)      */
      || game_code == "SLPS-02365" /* Chrono Cross (Disc 2/2) (NTSC-J)      */
      || game_code == "SLPS-02777" /* Chrono Cross            (Square Millennium Collection) (NTSC-J) */
      || game_code == "SLPS-02778" /* Chrono Cross (Disc 2/2) (Square Millennium Collection) (NTSC-J) */
      || game_code == "SLPS-91464" /* Chrono Cross            [PSOne Books] (NTSC-J) */
      || game_code == "SLPS-91465" /* Chrono Cross (Disc 2/2) [PSOne Books] (NTSC-J) */
      || game_code == "SLUS-01041" /* Chrono Cross            (NTSC-U) */
      || game_code == "SLUS-01080" /* Chrono Cross (Disc 2/2) (NTSC-U) */
     )
  {
    gs->dma_max_slice_ticks = 100;
    gs->dma_halt_ticks = 150;
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (
         game_code == "SLED-01401" /* International Superstar Soccer '98 Pro Demo (PAL-DE) */
      || game_code == "SLED-01513" /* International Superstar Soccer '98 Pro Demo (PAL) */
      || game_code == "SLES-01218" /* International Superstar Soccer '98 Pro      (PAL) */
      || game_code == "SLES-01264" /* International Superstar Soccer '98 Pro      (PAL) */
      || game_code == "SCPS-45294" /* International Superstar Soccer '98 Pro      (NTSC-J) */
      || game_code == "SLUS-00674" /* International Superstar Soccer '98 Pro      (NTSC-U) */
      || game_code == "SLPM-86086" /* World Soccer Jikkyou Winning Eleven 3 - World Cup France '98 (NTSC-J) */
      || game_code == "SLPS-00435" /* PS1 Megatudo 2096 (NTSC-J) */
      || game_code == "SLUS-00388" /* NBA Jam Extreme (NTSC-U) */
      || game_code == "SLES-00529" /* NBA Jam Extreme (PAL) */
      || game_code == "SLPS-00699" /* NBA Jam Extreme (NTSC-J) */
      || game_code == "SCES-02834" /* Crash Bash (PAL) */
      || game_code == "SCUS-94200" /* Battle Arena Toshinden (NTSC-U) */
      || game_code == "SCES-00002" /* Battle Arena Toshinden (PAL) */
      || game_code == "SCUS-94003" /* Battle Arena Toshinden (NTSC-U) */
      || game_code == "SLPS-00025" /* Battle Arena Toshinden (NTSC-J) */
      || game_code == "SLES-01987" /* The Next Tetris (PAL) */
      || game_code == "SLPS-01774" /* The Next Tetris (NTSC-J) */
      || game_code == "SLPS-02701" /* The Next Tetris [BPS The Choice] (NTSC-J) */
      || game_code == "SLUS-00862" /* The Next Tetris (NTSC-U) */
      || game_code == "SLES-03552" /* Breath of Fire IV (PAL) */
      || game_code == "SLUS-01324" /* Breath of Fire IV (NTSC-U) */
      || game_code == "SLPS-02728" /* Breath of Fire IV (NTSC-J) */
      || game_code == "SLPM-87159" /* Breath of Fire IV [PlayStation The Best] (NTSC-J) */
      || game_code == "SCPS-10059" /* Legaia Densetsu (NTSC-J) */
      || game_code == "SCUS-94254" /* Legend of Legaia (NTSC-U) */
      || game_code == "SCES-01752" /* Legend of Legaia (PAL) */
      || game_code == "SCES-01944" /* Legend of Legaia (PAL) */
      || game_code == "SCES-01947" /* Legend of Legaia (PAL) */
      || game_code == "SCES-01946" /* Legend of Legaia (PAL) */
      || game_code == "SCES-01945" /* Legend of Legaia (PAL) */
      || game_code == "SLES-01265" /* World Cup '98                               (PAL)    */
      || game_code == "SLUS-00644" /* World Cup '98                               (NTSC-U) */
      || game_code == "SLPS-00267" /* Deadheat Road                               (NTSC-J) */
      || game_code == "SLUS-00292" /* Suikoden                                    (NTSC-U) */
      || game_code == "SCUS-94577" /* NHL Faceoff 2001                            (NTSC-U) */
      || game_code == "SCUS-94578" /* NHL Faceoff 2001 Demo                       (NTSC-U) */
      || game_code == "SLPS-00712" /* Tenga Seiha                                 (NTSC-J) */
      || game_code == "SLES-03449" /* Roland Garros 2001                          (PAL)    */
      || game_code == "SLUS-00707" /* Silent Hill                                 (NTSC-U) */
      || game_code == "SLPM-86192" /* Silent Hill                                 (NTSC-J) */
      || game_code == "SLES-01514" /* Silent Hill                                 (PAL)    */
      || game_code == "SLUS-00875" /* Spiderman                                   (NTSC-U) */
      || game_code == "SLPM-86739" /* Spiderman                                   (NTSC-J) */
      || game_code == "SLES-02886" /* Spiderman                                   (PAL)    */
      || game_code == "SLES-02887" /* Spiderman                                   (PAL)    */
      || game_code == "SLES-02888" /* Spiderman                                   (PAL)    */
      || game_code == "SLES-02889" /* Spiderman                                   (PAL)    */
      || game_code == "SLES-02890" /* Spiderman                                   (PAL)    */
      || game_code == "SLUS-00183" /* Zero Divide                                 (NTSC-U) */
     )
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (   
         game_code == "SLPS-01009" /* Lagnacure (NTSC-J) */
      || game_code == "SCPS-45120" /* Lagnacure (NTSC-J) */
      || game_code == "SLPS-02833" /* Lagnacure [Artdink Best Choice] (NTSC-J) */
      || game_code == "SCUS-94243" /* Einhander (NTSC-U) */
      || game_code == "SLPS-02878" /* Colin McRae Rally 02  (NTSC-J) */
      || game_code == "SLUS-01222" /* Colin McRae Rally 2.0 (NTSC-U) */
      || game_code == "SLES-02605" /* Colin McRae Rally 2.0 (PAL) */
      || game_code == "SLPM-86943" /* Tony Hawk's Pro Skater [SuperLite 1500 Series] (NTSC-J) */
      || game_code == "SLUS-00860" /* Tony Hawk's Pro Skater (NTSC-U) */
      || game_code == "SLPM-86429" /* Tony Hawk's Pro Skater   (NTSC-J) */
      || game_code == "SLES-02908" /* Tony Hawk's Pro Skater 2 (PAL)    */
      || game_code == "SLES-02909" /* Tony Hawk's Pro Skater 2 (PAL)    */
      || game_code == "SLES-02910" /* Tony Hawk's Pro Skater 2 (PAL)    */
      || game_code == "SLPM-86751" /* Tony Hawk's Pro Skater 2 (NTSC-J) */
      || game_code == "SLUS-01066" /* Tony Hawk's Pro Skater 2 (NTSC-U) */
      || game_code == "SLED-02879" /* Tony Hawk's Pro Skater 2 Demo (PAL) */
      || game_code == "SLED-03048" /* Tony Hawk's Pro Skater 2 Demo (PAL) */
      || game_code == "SLUS-90086" /* Tony Hawk's Pro Skater 2 Demo (NTSC-U) */
      || game_code == "SLES-03645" /* Tony Hawk's Pro Skater 3 (PAL)    */
      || game_code == "SLES-03646" /* Tony Hawk's Pro Skater 3 (PAL)    */
      || game_code == "SLES-03647" /* Tony Hawk's Pro Skater 3 (PAL)    */
      || game_code == "SLUS-01419" /* Tony Hawk's Pro Skater 3 (NTSC-U) */
      || game_code == "SLES-03954" /* Tony Hawk's Pro Skater 4 (PAL)    */
      || game_code == "SLES-03955" /* Tony Hawk's Pro Skater 4 (PAL)    */
      || game_code == "SLES-03956" /* Tony Hawk's Pro Skater 4 (PAL)    */
      || game_code == "SLUS-01485" /* Tony Hawk's Pro Skater 4 (NTSC-U) */
     )
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerLUTFastmem);
    return gs;
  }

  return {};
}
