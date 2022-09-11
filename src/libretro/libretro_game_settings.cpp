#include "libretro_game_settings.h"

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
     )
  {
    gs->AddTrait(GameSettings::Trait::ForceInterlacing);
    return gs;
  }

  if (game_code == "SLUS-01260") /* Pro Pinball Big Race USA (NTSC-U) */
  {
    gs->AddTrait(GameSettings::Trait::ForceSoftwareRenderer);
    gs->AddTrait(GameSettings::Trait::ForceInterlacing);
    return gs;
  }

  if (game_code == "SLES-01211") /* Pro Pinball Big Race USA (PAL) */
  {
    gs->AddTrait(GameSettings::Trait::ForceSoftwareRenderer);
    gs->AddTrait(GameSettings::Trait::ForceInterlacing);
    return gs;
  }

  if (game_code == "SLUS-01261") /* Pro Pinball - Fantastic Journey (NTSC-U) */
  {
    gs->AddTrait(GameSettings::Trait::ForceSoftwareRenderer);
    gs->AddTrait(GameSettings::Trait::ForceInterlacing);
    return gs;
  }

  if (game_code == "SLES-02466")
  {
    gs->AddTrait(GameSettings::Trait::ForceSoftwareRenderer);
    gs->AddTrait(GameSettings::Trait::ForceInterlacing);
    return gs;
  }

  if (game_code == "SLES-00259")
  {
    gs->AddTrait(GameSettings::Trait::ForceSoftwareRenderer);
    gs->AddTrait(GameSettings::Trait::ForceInterlacing);
    return gs;
  }

  if (game_code == "SLES-00606")
  {
    gs->AddTrait(GameSettings::Trait::ForceSoftwareRenderer);
    gs->AddTrait(GameSettings::Trait::ForceInterlacing);
    return gs;
  }

  if (game_code == "SLUS-00639") /* Pro Pinball: Timeshock! (NTSC-U) */
  {
    gs->AddTrait(GameSettings::Trait::ForceSoftwareRenderer);
    gs->AddTrait(GameSettings::Trait::ForceInterlacing);
    return gs;
  }

  if (game_code == "SLUS-90039") /* Pro Pinball: Timeshock! (NTSC-U) */
  {
    gs->AddTrait(GameSettings::Trait::ForceSoftwareRenderer);
    gs->AddTrait(GameSettings::Trait::ForceInterlacing);
    return gs;
  }

  if (game_code == "SLUS-00337") /* True Pinball (NTSC-U) */
  {
    gs->AddTrait(GameSettings::Trait::ForceInterlacing);
    return gs;
  }

  if (game_code == "SLPS-03553") /* Naruto - Shinobi no Sato no Jintori Kassen (NTSC_J) *?
  {
    gs->AddTrait(GameSettings::Trait::ForceInterlacing);
    return gs;
  }

  if (game_code == "SLPS-01211")
  {
    gs->AddTrait(GameSettings::Trait::ForceInterlacing);
    return gs;
  }

  if (game_code == "SLUS-00656")
  {
    gs->AddTrait(GameSettings::Trait::ForceInterlacing);
    return gs;
  }

  if (game_code == "SCPS-10126")
  {
    gs->AddTrait(GameSettings::Trait::ForceSoftwareRenderer);
    return gs;
  }

  if (game_code == "SLPS-00078") /* Gakkou no kowai uwasa - Hanako Sangakita!! (NTSC-J) */
  {
    gs->AddTrait(GameSettings::Trait::DisableTrueColor);
    return gs;
  }

  if (game_code == "SLPS-00435") /* PS1 Megatudo 2096 (NTSC-J) */
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SLUS-00388")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SLES-00529")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SLPS-00699")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SCES-02834")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SCUS-94200")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SCES-00002")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SCUS-94003")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SLPS-00025")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SLES-01987")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SLPS-01774")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SLPS-02701")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SLUS-00862")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SLES-03552")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SLUS-01324")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SLPM-87159")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SLPS-02728")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SLPM-86086")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SCED-01979")
  {
    gs->AddTrait(GameSettings::Trait::ForceInterpreter);
    return gs;
  }

  if (game_code == "SLED-00491")
  {
    gs->AddTrait(GameSettings::Trait::ForceInterpreter);
    return gs;
  }

  if (game_code == "SCES-02777") /* Formula One 2000 (PAL) */
  {
    gs->AddTrait(GameSettings::Trait::ForceInterpreter);
    return gs;
  }

  if (game_code == "SCES-02779") /* Formula One 2000 (I-S) */
  {
    gs->AddTrait(GameSettings::Trait::ForceInterpreter);
    return gs;
  }

  if (game_code == "SCES-02778")
  {
    gs->AddTrait(GameSettings::Trait::ForceInterpreter);
    return gs;
  }

  if (game_code == "SLUS-01134")
  {
    gs->AddTrait(GameSettings::Trait::ForceInterpreter);
    return gs;
  }

  if (game_code == "SCES-03404")
  {
    gs->AddTrait(GameSettings::Trait::ForceInterpreter);
    return gs;
  }

  if (game_code == "SCES-03424")
  {
    gs->AddTrait(GameSettings::Trait::ForceInterpreter);
    return gs;
  }

  if (game_code == "SCES-03423")
  {
    gs->AddTrait(GameSettings::Trait::ForceInterpreter);
    return gs;
  }

  if (game_code == "SCES-03524")
  {
    gs->AddTrait(GameSettings::Trait::ForceInterpreter);
    return gs;
  }

  if (game_code == "SCES-02222") /* Formula One '99 (PAL) */
  {
    gs->AddTrait(GameSettings::Trait::ForceInterpreter);
    return gs;
  }

  if (game_code == "SCES-01979")
  {
    gs->AddTrait(GameSettings::Trait::ForceInterpreter);
    return gs;
  }

  if (game_code == "SCPS-10101")
  {
    gs->AddTrait(GameSettings::Trait::ForceInterpreter);
    return gs;
  }

  if (game_code == "SLUS-00870")
  {
    gs->AddTrait(GameSettings::Trait::ForceInterpreter);
    return gs;
  }

  if (game_code == "SCES-03886") /* Formula One Arcade (PAL) */
  {
    gs->AddTrait(GameSettings::Trait::ForceInterpreter);
    return gs;
  }

  if (game_code == "SLUS-00183") /* Zero Divide (NTSC-U) */
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SCPS-10059") /* Legaia Densetsu (NTSC-J) */
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SCUS-94254") /* Legend of Legaia (NTSC-U) */
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SCES-01752")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SCES-01944")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SCES-01945")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SCES-01946")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SCES-01947")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SLUS-00707")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SLPM-86192")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SLES-01514")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SLUS-00875")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SLPM-86739")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SLES-02886")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SLES-02887")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SLES-02888")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SLES-02889")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SLES-02890")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SLES-03449")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SLES-01265")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SLUS-00644")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SLES-00483")
  {
    gs->AddTrait(GameSettings::Trait::ForceInterlacing);
    return gs;
  }

  if (game_code == "SLPS-02361") /* Touge Max G (NTSC-J) */
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPVertexCache);
    return gs;
  }

  if (game_code == "SLPS-00712") /* Tenga Seiha (NTSC-J) */
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SLPS-01434") /* 3D Kakutou Tsukuru (NTSC-J) */
  {
    gs->AddTrait(GameSettings::Trait::ForceInterlacing);
    return gs;
  }

  if (game_code == "SLUS-00684") /* Jackie Chan's Stuntmaster (NTSC-U) */
  {
    gs->AddTrait(GameSettings::Trait::ForceInterpreter);
    return gs;
  }

  if (game_code == "SCES-01444")
  {
    gs->AddTrait(GameSettings::Trait::ForceInterpreter);
    return gs;
  }

  if (game_code == "SLPM-86750") /* Shiritsu Justice Gakuen - Nekketsu Seishun Nikki 2 [Capkore] (NTSC-J) */
  {
    gs->AddTrait(GameSettings::Trait::ForceInterlacing);
    return gs;
  }

  if (game_code == "SLPS-02120") /* Shiritsu Justice Gakuen - Nekketsu Seishun Nikki 2 (NTSC-J) */
  {
    gs->AddTrait(GameSettings::Trait::ForceInterlacing);
    return gs;
  }

  if (game_code == "SLUS-00348") /* Hexen (NTSC-U) */
  {
    gs->AddTrait(GameSettings::Trait::DisableUpscaling);
    return gs;
  }

  if (game_code == "SLPS-01762") /* Pepsiman (NTSC-J) */
  {
    gs->AddTrait(GameSettings::Trait::DisablePGXPCulling);
    return gs;
  }

  if (game_code == "SLES-03868") /* Marcel Desailly Pro Football (PAL) */
  {
    gs->AddTrait(GameSettings::Trait::DisablePGXPCulling);
    return gs;
  }

  if (game_code == "SLED-02439") /* Compilation 03 */
  {
    gs->AddTrait(GameSettings::Trait::DisablePGXPCulling);
    return gs;
  }

  if (game_code == "SCES-02835") /* Spyro - Year Of The Dragon (PAL) */
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SCES-02104") /* Spyro 2 - Gateway To Glimmer (PAL) */
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SCES-01438") /* Spyro The Dragon (PAL) */
  {
    gs->AddTrait(GameSettings::Trait::DisablePGXPCulling);
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SCUS-94467")
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SCUS-94425")
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SCPS-10085") /* Spyro The Dragon (NTSC-J) */
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SCUS-94228") /* Spyro The Dragon (NTSC-U) */
  {
    gs->AddTrait(GameSettings::Trait::DisablePGXPCulling);
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SCUS-94290")
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SLES-02397")
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SLES-12397")
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SLES-02398")
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SLES-12398")
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SLES-02399")
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SLES-12399") /* Grandia (PAL) */
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SLPS-02124") /* Grandia (NTSC-J) */
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SLPS-91205") /* Grandia [PlayStation The Best] (NTSC-J) */
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SLPS-02125") /* Grandia (NTSC-J) */
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SLPS-91206")
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SCUS-94457")
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SCUS-94465")
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SLPM-80297")
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SLES-00593")
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SLED-00038")
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SLUS-00530")
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SLED-02119")
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SLES-02088")
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SLUS-00634")
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SLUS-90056")
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SLPM-86310")
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SLPM-80473")
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SLPS-01055")
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SLPM-80173")
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SLES-02600")
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SLUS-01017")
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SLES-02602")
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SCPS-10115")
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "PAPX-90097")
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SLES-02601")
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SCES-03000")
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SCUS-94569")
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SLES-03972")
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SLES-03974")
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SLES-03973")
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SLES-03975")
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SLUS-01503")
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SLUS-01417")
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SLES-03662")
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SLES-03665") /* Harry Potter and the Philosopher's Stone (PAL) */
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SLES-03663") /* Harry Potter and the Philosopher's Stone (PAL) */
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SLES-03664")
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SLUS-01415")
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SLES-03976")
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SLPS-03492")
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SLPS-03355")
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SLPM-84013")
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    return gs;
  }

  if (game_code == "SCPS-45404") /* Racing Lagoon (NTSC-J) */
  {
    gs->AddTrait(GameSettings::Trait::ForcePGXPCPUMode);
    gs->AddTrait(GameSettings::Trait::ForceRecompilerLUTFastmem);
    return gs;
  }

  if (
         game_code == "SLPS-00935" /* Choukousoku GranDoll (NTSC-J) */
      || game_code == "SLPS-00870" /* Choukousoku GranDoll (NTSC-J) */
      || game_code == "SLPS-00869" /* Choukousoku GranDoll (NTSC-J) */
     ) 
  {
    gs->AddTrait(GameSettings::Trait::DisablePGXP);
    return gs;
  }

  if (game_code == "SLUS-01138") /* Vampire Hunter D (NTSC-U) */
  {
    gs->dma_max_slice_ticks = 200;
    gs->gpu_max_run_ahead = 1;
    return gs;
  }

  if (game_code == "SLPS-02376") /* Little Princess - Maru Oukoko No Ningyou Hime 2 (NTSC-J) */
  {
    gs->dma_max_slice_ticks = 100;
    gs->gpu_max_run_ahead = 1;
    return gs;
  }

  if (game_code == "SLUS-00282") /* Red Asphalt (NTSC-U) */
  {
    gs->dma_max_slice_ticks = 200;
    gs->gpu_max_run_ahead = 1;
    return gs;
  }

  if (game_code == "SLES-00056")
  {
    gs->dma_max_slice_ticks = 200;
    gs->gpu_max_run_ahead = 1;
    return gs;
  }

  if (game_code == "SLES-00654")
  {
    gs->dma_max_slice_ticks = 200;
    gs->gpu_max_run_ahead = 1;
    return gs;
  }

  if (game_code == "SLES-10654")
  {
    gs->dma_max_slice_ticks = 200;
    gs->gpu_max_run_ahead = 1;
    return gs;
  }

  if (game_code == "SLES-00656")
  {
    gs->dma_max_slice_ticks = 200;
    gs->gpu_max_run_ahead = 1;
    return gs;
  }

  if (game_code == "SLES-10656")
  {
    gs->dma_max_slice_ticks = 200;
    gs->gpu_max_run_ahead = 1;
    return gs;
  }

  if (game_code == "SLES-00584")
  {
    gs->dma_max_slice_ticks = 200;
    gs->gpu_max_run_ahead = 1;
    return gs;
  }

  if (game_code == "SLES-10584")
  {
    gs->dma_max_slice_ticks = 200;
    gs->gpu_max_run_ahead = 1;
    return gs;
  }

  if (game_code == "SLES-00643")
  {
    gs->dma_max_slice_ticks = 200;
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
     ) 
  {
    gs->dma_max_slice_ticks = 200;
    gs->gpu_max_run_ahead = 1;
    return gs;
  }

  if (game_code == "SLUS-0381")
  {
    gs->dma_max_slice_ticks = 200;
    gs->gpu_max_run_ahead = 1;
    return gs;
  }

  if (game_code == "SLUS-00022") /* Slam'n'Jam '96 Featuring Magic & Kareem */
  {
    gs->AddTrait(GameSettings::Trait::DisableUpscaling);
    return gs;
  }

  if (game_code == "SLUS-00292") /* Suikoden (NTSC-U) */
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SCUS-94577") /* NHL Faceoff 2001 (NTSC-U) */
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SCUS-94578") /* NHL Faceoff 2001 Demo (NTSC-U) */
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
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
) 
  {
    gs->dma_max_slice_ticks = 100;
    return gs;
  }

  if (game_code == "SCES-00842")
  {
    gs->dma_max_slice_ticks = 100;
    return gs;
  }

  if (game_code == "SLES-00910")
  {
    gs->dma_max_slice_ticks = 100;
    return gs;
  }

  if (game_code == "SLES-01157")
  {
    gs->dma_max_slice_ticks = 100;
    return gs;
  }

  if (game_code == "SLES-01158")
  {
    gs->dma_max_slice_ticks = 100;
    return gs;
  }

  if (game_code == "SLUS-00524")
  {
    gs->dma_max_slice_ticks = 100;
    return gs;
  }

  if (game_code == "SLPM-87395") /* Chrono Cross (Ultimate Hits) (NTSC-J) */
  {
    gs->dma_max_slice_ticks = 100;
    gs->dma_halt_ticks = 150;
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SLPS-02364") /* Chrono Cross (NTSC-J) */
  {
    gs->dma_max_slice_ticks = 100;
    gs->dma_halt_ticks = 150;
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SLPS-02777") /* Chrono Cross (Square Millennium Collection) (NTSC-J) */
  {
    gs->dma_max_slice_ticks = 100;
    gs->dma_halt_ticks = 150;
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SLPS-91464")
  {
    gs->dma_max_slice_ticks = 100;
    gs->dma_halt_ticks = 150;
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SLPM-87396")
  {
    gs->dma_max_slice_ticks = 100;
    gs->dma_halt_ticks = 150;
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SLPS-02365")
  {
    gs->dma_max_slice_ticks = 100;
    gs->dma_halt_ticks = 150;
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SLPS-02778")
  {
    gs->dma_max_slice_ticks = 100;
    gs->dma_halt_ticks = 150;
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SLPS-91465")
  {
    gs->dma_max_slice_ticks = 100;
    gs->dma_halt_ticks = 150;
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SLUS-01041")
  {
    gs->dma_max_slice_ticks = 100;
    gs->dma_halt_ticks = 150;
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SLUS-01080") /* Chrono Cross - Disc 2/2 (NTSC-U) */
  {
    gs->dma_max_slice_ticks = 100;
    gs->dma_halt_ticks = 150;
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SLUS-00912") /* Destruction Derby Raw (NTSC-U) */
  {
    gs->AddTrait(GameSettings::Trait::ForceInterlacing);
    return gs;
  }

  if (game_code == "SLPS-00267")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SCPS-45294")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SLES-01218")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SLED-01401")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SLED-01513")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SLES-01264")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SLUS-00674")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerICache);
    return gs;
  }

  if (game_code == "SLPS-02878")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerLUTFastmem);
    return gs;
  }

  if (game_code == "SLES-02605") /* Colin McRae Rally 2.0 (PAL) */
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerLUTFastmem);
    return gs;
  }

  if (game_code == "SLUS-01222") /* Colin McRae Rally 2.0 (NTSC-U) */
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerLUTFastmem);
    return gs;
  }

  if (game_code == "SLPM-86429")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerLUTFastmem);
    return gs;
  }

  if (game_code == "SLPM-86943")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerLUTFastmem);
    return gs;
  }

  if (game_code == "SLUS-00860") /* Tony Hawk's Pro Skater (NTSC-U) */
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerLUTFastmem);
    return gs;
  }

  if (game_code == "SLED-02879")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerLUTFastmem);
    return gs;
  }

  if (game_code == "SLED-03048")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerLUTFastmem);
    return gs;
  }

  if (game_code == "SLES-02908")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerLUTFastmem);
    return gs;
  }

  if (game_code == "SLES-02909")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerLUTFastmem);
    return gs;
  }

  if (game_code == "SLES-02910")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerLUTFastmem);
    return gs;
  }

  if (game_code == "SLPM-86751") /* Tony Hawk's Pro Skater 2 (NTSC-U) */
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerLUTFastmem);
    return gs;
  }

  if (game_code == "SLUS-01066")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerLUTFastmem);
    return gs;
  }

  if (game_code == "SLUS-90086")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerLUTFastmem);
    return gs;
  }

  if (game_code == "SLES-03645")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerLUTFastmem);
    return gs;
  }

  if (game_code == "SLES-03646")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerLUTFastmem);
    return gs;
  }

  if (game_code == "SLES-03647")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerLUTFastmem);
    return gs;
  }

  if (game_code == "SLUS-01419")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerLUTFastmem);
    return gs;
  }

  if (game_code == "SLES-03954")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerLUTFastmem);
    return gs;
  }

  if (game_code == "SLES-03956")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerLUTFastmem);
    return gs;
  }

  if (game_code == "SLES-03955")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerLUTFastmem);
    return gs;
  }

  if (game_code == "SLUS-01485")
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerLUTFastmem);
    return gs;
  }

  if (game_code == "SLPS-02833") /* Lagnacure [Artdink Best Choice] (NTSC-J) */
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerLUTFastmem);
    return gs;
  }

  if (game_code == "SCPS-45120") /* Lagnacure (NTSC-J) */
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerLUTFastmem);
    return gs;
  }

  if (game_code == "SLPS-01009") /* Lagnacure (NTSC-J) */
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerLUTFastmem);
    return gs;
  }

  if (game_code == "SCUS-94243")/* Einhander (NTSC-U) */
  {
    gs->AddTrait(GameSettings::Trait::ForceRecompilerLUTFastmem);
    return gs;
  }

  return {};
}
