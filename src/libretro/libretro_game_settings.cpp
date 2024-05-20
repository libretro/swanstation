#include "libretro_game_settings.h"
#include "common/string.h"
#include "common/string_util.h"
#include "core/host_interface.h"
#include "core/settings.h"

#ifdef _WIN32
#include "common/windows_headers.h"
#endif

namespace GameSettings {

void Entry::ApplySettings(bool display_osd_messages) const
{
  constexpr float osd_duration = 5.0f;

  // Sets values to a default state. Removing this section WILL break games (E.G. Chrono Cross' status screen hanging).
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
  if (display_force_4_3_for_24bit.has_value())
    g_settings.display_force_4_3_for_24bit = display_force_4_3_for_24bit.value();
  if (gpu_renderer.has_value())
    g_settings.gpu_renderer = gpu_renderer.value();
  if (gpu_use_software_renderer_for_readbacks.has_value())
    g_settings.gpu_use_software_renderer_for_readbacks = gpu_use_software_renderer_for_readbacks.value();
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

  std::string gamesettings_message = "";

  if (HasTrait(Trait::ForceInterpreter))
  {
    if (g_settings.cpu_execution_mode != CPUExecutionMode::Interpreter)
    {
      gamesettings_message.append("CPU interpreter forced by game settings. ", 41);
    }

    g_settings.cpu_execution_mode = CPUExecutionMode::Interpreter;
  }

  if (HasTrait(Trait::ForceSoftwareRenderer))
  {
    if (g_settings.gpu_renderer != GPURenderer::Software)
    {
      gamesettings_message.append("Software renderer forced by game settings. ", 43);
    }

    g_settings.gpu_renderer = GPURenderer::Software;
  }

  if (HasTrait(Trait::ForceSoftwareRendererForReadbacks))
  {
    if (g_settings.gpu_renderer != GPURenderer::Software && !g_settings.gpu_use_software_renderer_for_readbacks)
    {
      gamesettings_message.append("Using software renderer for readbacks based on game settings. ", 62);
    }

    g_settings.gpu_use_software_renderer_for_readbacks = true;
  }

  if (HasTrait(Trait::ForceInterlacing))
  {
    if (g_settings.gpu_disable_interlacing)
    {
      gamesettings_message.append("Interlacing forced by game settings. ", 36);
    }

    g_settings.gpu_disable_interlacing = false;
  }

  if (HasTrait(Trait::DisableTrueColor))
  {
    if (g_settings.gpu_true_color)
    {
      gamesettings_message.append("True color disabled by game settings. ", 38);
    }

    g_settings.gpu_true_color = false;
  }

  if (HasTrait(Trait::DisableUpscaling))
  {
    if (g_settings.gpu_resolution_scale > 1)
    {
      gamesettings_message.append("Upscaling disabled by game settings. ", 37);
    }

    g_settings.gpu_resolution_scale = 1;
  }

  if (HasTrait(Trait::DisableScaledDithering))
  {
    if (g_settings.gpu_scaled_dithering)
    {
      gamesettings_message.append("Scaled dithering disabled by game settings. ", 44);
    }

    g_settings.gpu_scaled_dithering = false;
  }

  if (HasTrait(Trait::DisableWidescreen))
  {
    if (g_settings.gpu_widescreen_hack)
    {
      gamesettings_message.append("Widescreen hack disabled by game settings. ", 43);
    }

    g_settings.gpu_widescreen_hack = false;
  }

  if (HasTrait(Trait::DisableForceNTSCTimings))
  {
    if (g_settings.gpu_force_ntsc_timings)
    {
      gamesettings_message.append("Forcing NTSC Timings disallowed by game settings. ", 50);
    }

    g_settings.gpu_force_ntsc_timings = false;
  }

  if (HasTrait(Trait::DisablePGXP))
  {
    if (g_settings.gpu_pgxp_enable)
    {
      gamesettings_message.append("PGXP geometry correction disabled by game settings. ", 52);
    }

    g_settings.gpu_pgxp_enable = false;
  }

  if (HasTrait(Trait::DisablePGXPCulling))
  {
    if (g_settings.gpu_pgxp_enable && g_settings.gpu_pgxp_culling)
    {
      gamesettings_message.append("PGXP culling disabled by game settings. ", 40);
    }

    g_settings.gpu_pgxp_culling = false;
  }

  if (HasTrait(Trait::DisablePGXPTextureCorrection))
  {
    if (g_settings.gpu_pgxp_enable && g_settings.gpu_pgxp_texture_correction)
    {
      gamesettings_message.append("PGXP perspective corrected textures disabled by game settings. ", 63);
    }

    g_settings.gpu_pgxp_texture_correction = false;
  }

  if (HasTrait(Trait::DisablePGXPColorCorrection))
  {
    if (g_settings.gpu_pgxp_enable && g_settings.gpu_pgxp_color_correction)
    {
      gamesettings_message.append("PGXP perspective corrected colors disabled by game settings. ", 61);
    }

    g_settings.gpu_pgxp_color_correction = false;
  }

  if (HasTrait(Trait::ForcePGXPVertexCache))
  {
    if (g_settings.gpu_pgxp_enable && !g_settings.gpu_pgxp_vertex_cache)
    {
      gamesettings_message.append("PGXP vertex cache forced by game settings. ", 43);
    }

    g_settings.gpu_pgxp_vertex_cache = true;
  }

  if (HasTrait(Trait::ForcePGXPCPUMode))
  {
    if (g_settings.gpu_pgxp_enable && !g_settings.gpu_pgxp_cpu)
    {
      gamesettings_message.append("PGXP CPU mode forced by game settings. ", 39);
    }

    g_settings.gpu_pgxp_cpu = true;
  }

  if (HasTrait(Trait::DisablePGXPDepthBuffer))
  {
    if (g_settings.gpu_pgxp_enable && g_settings.gpu_pgxp_depth_buffer)
    {
      gamesettings_message.append("PGXP Depth Buffer disabled by game settings. ", 45);
    }

    g_settings.gpu_pgxp_depth_buffer = false;
  }

  if (HasTrait(Trait::ForceRecompilerMemoryExceptions))
  {
    if (g_settings.cpu_execution_mode == CPUExecutionMode::Recompiler && !g_settings.cpu_recompiler_memory_exceptions)
    {
      gamesettings_message.append("Memory exceptions for recompiler forced by game settings. ", 58);
    }

    g_settings.cpu_recompiler_memory_exceptions = true;
  }

  if (HasTrait(Trait::ForceRecompilerICache))
  {
    if (g_settings.cpu_execution_mode == CPUExecutionMode::Recompiler && !g_settings.cpu_recompiler_icache)
    {
      gamesettings_message.append("ICache for recompiler forced by game settings. ", 47);
    }

    g_settings.cpu_recompiler_icache = true;
  }

  if (g_settings.cpu_fastmem_mode == CPUFastmemMode::MMap && HasTrait(Trait::ForceRecompilerLUTFastmem))
  {
    if (g_settings.cpu_execution_mode == CPUExecutionMode::Recompiler)
    {
      gamesettings_message.append("LUT fastmem for recompiler forced by game settings. ", 52);
    }

    g_settings.cpu_fastmem_mode = CPUFastmemMode::LUT;
  }

  if (HasTrait(Trait::ForceOldAudioHook))
  {
    if (g_settings.audio_fast_hook)
    {
      gamesettings_message.append("Old audio hook forced by game settings. ", 40);
    }

    g_settings.audio_fast_hook = false;
  }

  if (display_osd_messages && gamesettings_message.length() > 0)
  {
    g_host_interface->AddOSDMessage(
      g_host_interface->TranslateStdString("OSDMessage", gamesettings_message.c_str()),
      osd_duration);
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
      || game_code == "SLES-00585" /* Star Wars - Dark Forces (PAL)  */
      || game_code == "SLES-00640" /* Star Wars - Dark Forces (Italy) (En,It)  */
      || game_code == "SLPS-00685" /* Star Wars - Dark Forces (NTSC-J)  */
      || game_code == "SLES-00646" /* Star Wars - Dark Forces (Spain)  */
      || game_code == "SLES-00555" /* Hexen (Europe)  */
      || game_code == "SLUS-00348" /* Hexen (USA)  */
      || game_code == "SLPS-00972" /* Hexen - Beyond Heretic (Japan)  */
     ) 
  {
    gs->AddTrait(GameSettings::Trait::DisableUpscaling);
    gs->AddTrait(GameSettings::Trait::DisablePGXP);
    return gs;
  }

  if (   game_code == "SCUS-94244" /* Crash Bandicoot - Warped (NTSC-U)    */
      || game_code == "SCES-01420" /* Crash Bandicoot 3 - Warped (PAL)    */
      || game_code == "SCPS-10073" /* Crash Bandicoot 3 - Buttobi! Sekai Isshuu (NTSC-J) */
     )
  {
    gs->AddTrait(GameSettings::Trait::DisablePGXPColorCorrection);
    return gs;
  }

  if (   game_code == "SLPM-87089" /* Pop'n Music 6 (NTSC-J)    */
      || game_code == "SLPS-03336" /* Mr. Driller G (NTSC-J)    */
      || game_code == "SLUS-00952" /* Arcade Party Pak (NTSC-U) */
      || game_code == "SCES-01312" /* Devil Dice (PAL)          */
      || game_code == "SLPS-03553" /* Naruto - Shinobi no Sato no Jintori Kassen (NTSC-J)     */
      || game_code == "SLPS-01211" /* Time Bokan Series - Bokandesuyo (NTSC-J)     */
      || game_code == "SLUS-00656" /* Rat Attack (NTSC-U)       */
      || game_code == "SLUS-00912" /* Destruction Derby Raw (NTSC-U) */
      || game_code == "SLPS-01434" /* 3D Kakutou Tsukuru    (NTSC-J) */
      || game_code == "SLPM-86750" /* Shiritsu Justice Gakuen - Nekketsu Seishun Nikki 2 [Capkore] (NTSC-J) */
      || game_code == "SLPS-02120" /* Shiritsu Justice Gakuen - Nekketsu Seishun Nikki 2 (NTSC-J) */
     )
  {
    gs->AddTrait(GameSettings::Trait::ForceInterlacing);
    return gs;
  }

  if (game_code == "SLES-00483") /* Worms Pinball (PAL)       */
  {
    gs->AddTrait(GameSettings::Trait::ForceInterlacing);
    gs->AddTrait(GameSettings::Trait::DisableUpscaling);
    gs->AddTrait(GameSettings::Trait::DisablePGXP);
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

  if (   game_code == "SCPS-45446" /* Chrono Trigger */
      || game_code == "SLPM-87374" /* Chrono Trigger (NTSC-J) */
      || game_code == "SLUS-01363" /* Final Fantasy Chronicles - Chrono Trigger (NTSC-U) */
      || game_code == "SCPS-10091" /* Saru! Get You! (NTSC-J) */
      || game_code == "SCPS-91196" /* Saru! Get You! (NTSC-J) */
      || game_code == "SCPS-91331" /* Saru! Get You! (NTSC-J) */
      || game_code == "SCPS-45411" /* Saru! Get You! (NTSC-J) */
      || game_code == "SCUS-94423" /* Ape Escape (NTSC-U) */
      || game_code == "SCES-01564" /* Ape Escape (PAL) */
      || game_code == "SCES-02028" /* Ape Escape (PAL-FR) */
      || game_code == "SCES-02029" /* Ape Escape (PAL-DE) */
      || game_code == "SCES-02030" /* Ape Escape (PAL-IT) */
      || game_code == "SCES-02031" /* Ape Escape (PAL-ES) */
     )
  {
    gs->AddTrait(GameSettings::Trait::ForceSoftwareRendererForReadbacks);
    return gs;
  }

  if (   game_code == "SLPS-00078" /* Gakkou no kowai uwasa - Hanako Sangakita!! (NTSC-J) */
      || game_code == "SLES-01064" /* Mega Man 8 (PAL) */
      || game_code == "SLUS-00453" /* Mega Man 8 (NTSC-U) */
      || game_code == "SLPS-00630" /* Mega Man 8 (NTSC-J) */
     )
  {
    gs->AddTrait(GameSettings::Trait::DisableTrueColor);
    return gs;
  }

  if (   game_code == "SCES-01705" /* Dragon Valor (Europe) (Disc 1) */
      || game_code == "SCES-11705" /* Dragon Valor (Europe) (Disc 2) */
      || game_code == "SCES-02565" /* Dragon Valor (France) (Disc 1) */
      || game_code == "SCES-12565" /* Dragon Valor (France) (Disc 2) */
      || game_code == "SCES-02566" /* Dragon Valor (Germany) (Disc 1) */
      || game_code == "SCES-12566" /* Dragon Valor (Germany) (Disc 2) */
      || game_code == "SCES-02567" /* Dragon Valor (Italy) (Disc 1) */
      || game_code == "SCES-12567" /* Dragon Valor (Italy) (Disc 2) */
      || game_code == "SLPS-02190" /* Dragon Valor (Japan) (Disc 1) */
      || game_code == "SLPS-02191" /* Dragon Valor (Japan) (Disc 2) */
      || game_code == "SCES-02568" /* Dragon Valor (Spain) (Disc 1) */
      || game_code == "SCES-12568" /* Dragon Valor (Spain) (Disc 2) */
      || game_code == "SLUS-01092" /* Dragon Valor (USA) (Disc 1) */
      || game_code == "SLUS-01164" /* Dragon Valor (USA) (Disc 2) */
     )
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
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
      || game_code == "SCES-01659" /* Kingsley's Adventure (Europe) (En,Fr,De,Es,It,Nl,Sv,No,Da,Fi) */
      || game_code == "SLUS-00801" /* Kingsley's Adventure (USA) */
     )
  {
    gs->AddTrait(GameSettings::Trait::DisablePGXPCulling);
    return gs;
  }

  if (   game_code == "SLPS-00935" /* Choukousoku GranDoll (NTSC-J) */
      || game_code == "SLPS-00870" /* Choukousoku GranDoll (NTSC-J) */
      || game_code == "SLPS-00869" /* Choukousoku GranDoll (NTSC-J) */
     )
  {
    gs->AddTrait(GameSettings::Trait::DisablePGXP);
    return gs;
  }

  if (   game_code == "SCES-02835" /* Spyro - Year Of The Dragon (PAL) */
      || game_code == "SCES-02104" /* Spyro 2 - Gateway To Glimmer (PAL) */
      || game_code == "SCUS-94467" /* Spyro - Year of the Dragon (USA) */
      || game_code == "SCUS-94425" /* Spyro 2 - Ripto's Rage! (USA) */
      || game_code == "SCPS-10085" /* Spyro The Dragon (NTSC-J) */
      || game_code == "SCUS-94290" /* Spyro the Dragon (USA) (Demo 1) */
      || game_code == "SLES-00933" /* Newman Haas Racing (Europe) (En,Fr,De,It) */
      || game_code == "SLUS-00602" /* Newman Haas Racing (USA) */
      || game_code == "SLPM-86967" /* Persona 2 - Batsu - Eternal Punishment (Japan) (Disc 1) */
      || game_code == "SLPS-02826" /* Persona 2 - Batsu - Eternal Punishment (Japan) (Disc 2) */
      || game_code == "SLPS-02785" /* Persona 2 - Batsu - Eternal Punishment (Japan) (Disc 1) (Deluxe Pack) */
      || game_code == "SLPS-02786" /* Persona 2 - Batsu - Eternal Punishment (Japan) (Disc 2) (Deluxe Pack) */
      || game_code == "SCPS-45495" /* Persona 2 - Eternal Punishment */
      || game_code == "SCPS-45496" /* Persona 2 - Eternal Punishment */
      || game_code == "SLUS-01158" /* Persona 2 - Eternal Punishment (USA) */
      || game_code == "SLUS-01339" /* Persona 2 - Eternal Punishment (USA) (Bonus Disc) */
      || game_code == "SCPS-45410" /* Persona 2 - Innocent Sin (aka Persona 2 - Innocent Sin / Persona 2 - Tsumi) */
      || game_code == "SLPS-02100" /* Persona 2 - Tsumi - Innocent Sin (Japan) */
      || game_code == "SLPS-01923" /* Persona 2 - Tsumi - Innocent Sin (Japan) (Demo) */
      || game_code == "SLPS-91211" /* Persona 2 - Tsumi - Innocent Sin (Japan) (Rev 1) */
      || game_code == "SLES-02397" /* Grandia (Europe) (Disc 1) */
      || game_code == "SLES-12397" /* Grandia (Europe) (Disc 2) */
      || game_code == "SLES-02398" /* Grandia (France) (Disc 1) */
      || game_code == "SLES-12398" /* Grandia (France) (Disc 2) */
      || game_code == "SLES-02399" /* Grandia (Germany) (Disc 1) */
      || game_code == "SLES-12399" /* Grandia (Germany) (Disc 2) */
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
      || game_code == "SLED-02119" /* Croc 2 (Europe) (Demo) */
      || game_code == "SLES-02088" /* Croc 2 (Europe) (En,Fr,De,Es,It,Nl,Sv) */
      || game_code == "SLUS-00634" /* Croc 2 (USA) */
      || game_code == "SLUS-90056" /* Croc 2 (USA) (Demo) */
      || game_code == "SLPM-86310" /* Croc Adventure (Japan) */
      || game_code == "SLPM-80473" /* Croc Adventure (Japan) (Demo) */
      || game_code == "SLPS-01055" /* Croc! Pau-Pau Island (Japan) */
      || game_code == "SLPM-80173" /* Croc! Pau-Pau Island (Japan) (Demo) */
      || game_code == "SLES-02600" /* Alundra 2 - A New Legend Begins (Europe) */
      || game_code == "SLUS-01017" /* Alundra 2 - A New Legend Begins (USA) */
      || game_code == "SLES-02602" /* Alundra 2 - Der Beginn einer neuen Legende (Germany) */
      || game_code == "SCPS-10115" /* Alundra 2 - Mashinka no Nazo (Japan) */
      || game_code == "PAPX-90097" /* Alundra 2 - Mashinka no Nazo (Japan) (Demo) */
      || game_code == "SLES-02601" /* Alundra 2 - Une Legende Est Nee (France) */
      || game_code == "SCUS-94409" /* Codename - Tenka (USA) */
      || game_code == "SCES-03000" /* Disney's Aladdin in Nasira's Revenge (Europe) */
      || game_code == "SCUS-94569" /* Disney's Aladdin in Nasira's Revenge (USA) */
      || game_code == "SLES-03972" /* Harry Potter and the Chamber of Secrets (Europe) (En,Fr,De) */
      || game_code == "SLES-03974" /* Harry Potter and the Chamber of Secrets (Europe) (En,Nl,Da) */
      || game_code == "SLES-03973" /* Harry Potter and the Chamber of Secrets (Europe) (Es,It,Pt) */
      || game_code == "SLES-03975" /* Harry Potter and the Chamber of Secrets (Scandinavia) (En,Sv,No) */
      || game_code == "SLUS-01503" /* Harry Potter and the Chamber of Secrets (USA) (En,Fr,Es) */
      || game_code == "SLUS-01417" /* Harry Potter and the Philosopher's Stone (Canada) (En,Fr,De) */
      || game_code == "SLES-03662" /* Harry Potter and the Philosopher's Stone (Europe) (En,Fr,De) */
      || game_code == "SCPS-45077" /* Formula 1 */
      || game_code == "SLES-00298" /* Formula 1 (Europe) (En,Fr,De,Es,It) */
      || game_code == "SIPS-60011" /* Formula 1 (Japan) */
      || game_code == "SCUS-94353" /* Formula 1 (USA) */
      || game_code == "SLES-03665" /* Harry Potter and the Philosopher's Stone (PAL) */
      || game_code == "SLES-03663" /* Harry Potter and the Philosopher's Stone (PAL) */
      || game_code == "SLES-03664" /* Harry Potter & The Philosopher's Stone (PAL) */
      || game_code == "SLUS-01415" /* Harry Potter & The Sorcerer's Stone (NTSC-U) */
      || game_code == "SLES-03976" /* Harry Potter ja Salaisuuksien Kammio (PAL) */
      || game_code == "SLPS-03492" /* Harry Potter to Himitsu no Heya (NTSC-J) */
      || game_code == "SLPS-03355" /* Harry Potter to Kenja no Ishi (NTSC-J) */
      || game_code == "SLPM-84013" /* Harry Potter to Kenja no Ishi Coca-Cola Version (NTSC-J) */
      || game_code == "SCUS-94309" /* Jet Moto (USA) */
      || game_code == "SLES-00613" /* Lifeforce Tenka (Europe) */
      || game_code == "SLED-00690" /* Lifeforce Tenka (Europe) (Demo) */
      || game_code == "SLES-00614" /* Lifeforce Tenka (France) */
      || game_code == "SLES-00615" /* Lifeforce Tenka (Germany) */
      || game_code == "SLES-00616" /* Lifeforce Tenka (Italy) */
      || game_code == "SLES-00617" /* Lifeforce Tenka (Spain) */
      || game_code == "SCPS-45320" /* Metal Gear Solid */
      || game_code == "SCPS-45321" /* Metal Gear Solid */
      || game_code == "SCPS-45322" /* Metal Gear Solid */
      || game_code == "PAPX-90044" /* Metal Gear Solid (Asia) (Demo) */
      || game_code == "SCPS-45317" /* Metal Gear Solid (Asia) (Disc 1) */
      || game_code == "SCPS-45318" /* Metal Gear Solid (Asia) (Disc 2) */
      || game_code == "SLED-01400" /* Metal Gear Solid (Europe) (Demo 1) */
      || game_code == "SLED-01775" /* Metal Gear Solid (Europe) (Demo 2) */
      || game_code == "SLES-01370" /* Metal Gear Solid (Europe) (Disc 1) */
      || game_code == "SLES-11370" /* Metal Gear Solid (Europe) (Disc 2) */
      || game_code == "SLES-01506" /* Metal Gear Solid (France) (Disc 1) */
      || game_code == "SLES-11506" /* Metal Gear Solid (France) (Disc 2) */
      || game_code == "SLES-01507" /* Metal Gear Solid (Germany) (Disc 1) */
      || game_code == "SLES-11507" /* Metal Gear Solid (Germany) (Disc 2) */
      || game_code == "SLES-01508" /* Metal Gear Solid (Italy) (Disc 1) */
      || game_code == "SLES-11508" /* Metal Gear Solid (Italy) (Disc 2) */
      || game_code == "SLPM-86098" /* Metal Gear Solid (Japan) (Demo) */
      || game_code == "SLPM-86114" /* Metal Gear Solid (Japan) (Disc 1) */
      || game_code == "SLPM-86115" /* Metal Gear Solid (Japan) (Disc 2) */
      || game_code == "SLPM-87411" /* Metal Gear Solid [Premium Package Sai Hakkou Kinen] */
      || game_code == "SLPM-87412" /* Metal Gear Solid [Premium Package Sai Hakkou Kinen] */
      || game_code == "SLPM-87413" /* Metal Gear Solid [Premium Package Sai Hakkou Kinen] */
      || game_code == "SLES-01734" /* Metal Gear Solid (Spain) (Disc 1) */
      || game_code == "SLES-11734" /* Metal Gear Solid (Spain) (Disc 2) */
      || game_code == "SLUS-90035" /* Metal Gear Solid (USA) (Demo) */
      || game_code == "SLUS-00594" /* Metal Gear Solid (USA) (Disc 1) */
      || game_code == "SLUS-00776" /* Metal Gear Solid (USA) (Disc 2) */
      || game_code == "SLPM-86485" /* Metal Gear Solid [Konami the Best] */
      || game_code == "SLPM-86486" /* Metal Gear Solid [Konami the Best] */
      || game_code == "SLPM-87030" /* Metal Gear Solid [PSOne Books] */
      || game_code == "SLPM-87031" /* Metal Gear Solid [PSOne Books] */
      || game_code == "SLPS-00417" /* Racingroovy VS (Japan) */
      || game_code == "SLUS-00818" /* Street Sk8er (USA) */
      || game_code == "SLUS-01083" /* Street Sk8er 2 (USA) */
      || game_code == "SLES-01759" /* Street Skater (Europe) (En,Fr,De) */
      || game_code == "SLES-02703" /* Street Skater 2 (Europe) (En,Fr,De) */

      || game_code == "SCUS-94484" /* Wild Arms 2 (USA) (Disc 1) */
      || game_code == "SCUS-94498" /* Wild Arms 2 (USA) (Disc 2) */
      || game_code == "SCPS-45429" /* Wild Arms 2nd Ignition */
      || game_code == "SCPS-45430" /* Wild Arms 2nd Ignition */
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
    gs->dma_halt_ticks = 200;
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
      || game_code == "SLPS-00365" /* Tekkyuu - True Pinball (Japan) */
      || game_code == "SLES-00052" /* True Pinball (Europe) */
     )
  {
    gs->AddTrait(GameSettings::Trait::DisableUpscaling);
    return gs;
  }

  if (game_code == "SLUS-00337") /* True Pinball (NTSC-U)     */
  {
    gs->AddTrait(GameSettings::Trait::ForceInterlacing);
    gs->AddTrait(GameSettings::Trait::DisableUpscaling);
    return gs;
  }

  if (   game_code == "SLPS-02832" /* Lagnacure Legend (Japan) */
      || game_code == "SLES-02510" /* Sesame Street - Elmo's Letter Adventure (Europe) */
      || game_code == "SLUS-00621" /* Sesame Street - Elmo's Letter Adventure (USA) */
     )
  {
    gs->dma_max_slice_ticks = 100;
    gs->dma_halt_ticks = 150;
    return gs;
  }

  if (game_code == "SCPS-10001") /* Motor Toon Grand Prix (Japan) */
  {
    gs->AddTrait(GameSettings::Trait::ForceInterpreter);
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    gs->dma_max_slice_ticks = 400;
    gs->dma_halt_ticks = 155;
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
      || game_code == "SLES-01182" /* Castrol Honda Superbike Racing (Europe) (En,Fr,De,It) */
      || game_code == "SLUS-00882" /* Castrol Honda Superbike Racing (USA) */
      || game_code == "SLES-02731" /* Vampire Hunter D (Europe) (En,Fr,De) */
      || game_code == "SLPS-02477" /* Vampire Hunter D (Japan) */
      || game_code == "SLPS-03198" /* Vampire Hunter D (Japan) (Rev 1) */
      || game_code == "SLUS-01138" /* Vampire Hunter D (USA) */
      || game_code == "SLPS-91523" /* Vampire Hunter D [PSOne Books] */
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
      || game_code == "SLES-03224" /* Dino Crisis 2 (Italy) */
      || game_code == "SLES-03225" /* Dino Crisis 2 (Spain) */
      || game_code == "SLPS-02507" /* Next Tetris DLX, The (Japan) */
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

  if (   
         game_code == "SLUS-00546" /* Formula 1 - Championship Edition (NTSC-U) */
      || game_code == "SIPS-60023" /* Formula 1 '97 (NTSC-J) */
     )
  {
    gs->AddTrait(GameSettings::Trait::ForceOldAudioHook);
    return gs;
  }

  return {};
}
