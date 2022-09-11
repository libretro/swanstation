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
