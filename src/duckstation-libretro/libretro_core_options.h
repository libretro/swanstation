#ifndef LIBRETRO_CORE_OPTIONS_H__
#define LIBRETRO_CORE_OPTIONS_H__

#include <stdlib.h>
#include <string.h>

#include "libretro.h"
#include "retro_inline.h"

#ifndef HAVE_NO_LANGEXTRA
#include "libretro_core_options_intl.h"
#endif

/*
 ********************************
 * VERSION: 2.0
 ********************************
 *
 * - 2.0: Add support for core options v2 interface
 * - 1.3: Move translations to libretro_core_options_intl.h
 *        - libretro_core_options_intl.h includes BOM and utf-8
 *          fix for MSVC 2010-2013
 *        - Added HAVE_NO_LANGEXTRA flag to disable translations
 *          on platforms/compilers without BOM support
 * - 1.2: Use core options v1 interface when
 *        RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION is >= 1
 *        (previously required RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION == 1)
 * - 1.1: Support generation of core options v0 retro_core_option_value
 *        arrays containing options with a single value
 * - 1.0: First commit
*/

#ifdef __cplusplus
extern "C" {
#endif

/*
 ********************************
 * Core Option Definitions
 ********************************
*/

/* RETRO_LANGUAGE_ENGLISH */

/* Default language:
 * - All other languages must include the same keys and values
 * - Will be used as a fallback in the event that frontend language
 *   is not available
 * - Will be used as a fallback for any missing entries in
 *   frontend language definition */

struct retro_core_option_v2_category option_cats_us[] = {
   {
      "console",
      "Console Settings",
      "Settings which control behavior of the various elements of the console."
   },
   {
      "advanced",
      "Advanced Settings",
      "Settings which control internal emulator behavior. Use with care."
   },
   {
      "enhancement",
      "Enhancement Settings",
      "Settings which control graphical rendering and enhancements."
   },
   {
      "display",
      "Display Settings",
      "Settings which control how the image is displayed on the screen."
   },
   {
      "port",
      "Port Settings",
      "Settings which control controller and memory card behavior."
   },
   { NULL, NULL, NULL },
};

struct retro_core_option_v2_definition option_defs_us[] = {
   {
      "duckstation_Console.Region",
      "Console Region",
      NULL,
      "Determines which region/hardware to emulate. Auto-Detect will use the region of the disc inserted.",
      NULL,
      "console",
      {
         { "Auto",   "Auto-Detect" },
         { "NTSC-J", "NTSC-J (Japan)" },
         { "NTSC-U", "NTSC-U (US, Canada)" },
         { "PAL",    "PAL (Europe, Australia)" },
         { NULL, NULL },
      },
      "Auto"
   },
   {
      "duckstation_BIOS.PathNTSCJ",
      "NTSC-J BIOS (Restart)",
      NULL,
      "Select which standard BIOS to use for NTSC-J.",
      NULL,
      "console",
      {
         { "psxonpsp660.bin",   "POPS BIOS" },
         { "scph5500.bin", "SCPH5500 BIOS" },
         { "ps1_rom.bin", "PS3 BIOS" },
         { NULL, NULL },
      },
      "psxonpsp660.bin"
   },
   {
      "duckstation_BIOS.PathNTSCU",
      "NTSC-U BIOS (Restart)",
      NULL,
      "Select which standard BIOS to use for NTSC-U.",
      NULL,
      "console",
      {
         { "psxonpsp660.bin",   "POPS BIOS" },
         { "scph5501.bin", "SCPH5501 BIOS" },
         { "ps1_rom.bin", "PS3 BIOS" },
         { NULL, NULL },
      },
      "psxonpsp660.bin"
   },
   {
      "duckstation_BIOS.PathPAL",
      "PAL BIOS (Restart)",
      NULL,
      "Select which standard BIOS to use for PAL.",
      NULL,
      "console",
      {
         { "psxonpsp660.bin",   "POPS BIOS" },
         { "scph5502.bin", "SCPH5502 BIOS" },
         { "ps1_rom.bin", "PS3 BIOS" },
         { NULL, NULL },
      },
      "psxonpsp660.bin"
   },
   {
      "duckstation_BIOS.PatchFastBoot",
      "Fast Boot",
      NULL,
      "Skips the BIOS shell/intro, booting directly into the game. Usually safe to enable, but some games break.",
      NULL,
      "console",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "false"
   },
   {
      "duckstation_CDROM.RegionCheck",
      "CD-ROM Region Check",
      NULL,
      "Prevents discs from incorrect regions being read by the emulator. Usually safe to disable.",
      NULL,
      "console",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "false"
   },
   {
      "duckstation_CDROM.ReadThread",
      "CD-ROM Read Thread",
      NULL,
      "Reads CD-ROM sectors ahead asynchronously, reducing the risk of frame time spikes.",
      NULL,
      "console",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "true"
   },
   {
      "duckstation_CDROM.LoadImagePatches",
      "Apply Image Patches",
      NULL,
      "Automatically applies patches to disc images when they are present in the same directory. Currently only PPF "
      "patches are supported with this option. Requires the core to be restarted to apply.",
      NULL,
      "console",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "false"
   },
   {
      "duckstation_CDROM.LoadImageToRAM",
      "Preload CD-ROM Image To RAM",
      NULL,
      "Loads the disc image to RAM before starting emulation. May reduce hitching if you are running off a network share, "
      "at a cost of a greater startup time. As libretro provides no way to draw overlays, the emulator will appear to "
      "lock up while the image is preloaded.",
      NULL,
      "console",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "false"
   },
   {
      "duckstation_CDROM.MuteCDAudio",
      "Mute CD Audio",
      NULL,
      "Forcibly mutes both CD-DA and XA audio from the CD-ROM. Can be used to disable background music in some games.",
      NULL,
      "console",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "false"
   },
   {
      "duckstation_CDROM.ReadSpeedup",
      "CD-ROM Read Speedup",
      NULL,
      "Speeds up CD-ROM reads by the specified factor. Only applies to double-speed reads, and is ignored when audio "
      "is playing. May improve loading speeds in some games, at the cost of breaking others.",
      NULL,
      "console",
      {
         { "1",  "None (Double Speed)" },
         { "2",  "2x (Quad Speed)" },
         { "3",  "3x (6x Speed)" },
         { "4",  "4x (8x Speed)" },
         { "5",  "5x (10x Speed)" },
         { "6",  "6x (12x Speed)" },
         { "7",  "7x (14x Speed)" },
         { "8",  "8x (16x Speed)" },
         { "9",  "9x (18x Speed)" },
         { "10", "10x (20x Speed)" },
         { NULL, NULL },
      },
      "1"
   },
   {
      "duckstation_CDROM.SeekSpeedup",
      "CD-ROM Seek Speedup",
      NULL,
      "Speeds up CD-ROM seeks by the specified factor. May improve loading speeds in some game, at "
      "the cost of breaking others.",
      NULL,
      "console",
      {
         { "0",  "Infinite/Instantaneous" },
         { "1",  "None (Double Speed)" },
         { "2",  "2x (Quad Speed)" },
         { "3",  "3x (6x Speed)" },
         { "4",  "4x (8x Speed)" },
         { "5",  "5x (10x Speed)" },
         { "6",  "6x (12x Speed)" },
         { "7",  "7x (14x Speed)" },
         { "8",  "8x (16x Speed)" },
         { "9",  "9x (18x Speed)" },
         { "10", "10x (20x Speed)" },
         { NULL, NULL },
      },
      "1"
   },
   {
      "duckstation_CPU.ExecutionMode",
      "CPU Execution Mode",
      NULL,
      "Which mode to use for CPU emulation. Recompiler provides the best performance.",
      NULL,
      "console",
      {
         { "Interpreter",      NULL },
         { "CachedIntepreter", "Cached Interpreter" },
         { "Recompiler",       NULL },
         { NULL, NULL },
      },
      "Recompiler"
   },
   {
      "duckstation_CDROM.ReadaheadSectors",
      "CD-ROM Async Readahead",
      NULL,
      "Determines how far the CD-ROM thread will read ahead. Can reduce hitches on slow storage "
      "mediums or with compressed games.",
      NULL,
      "advanced",
      {
         { "0",   "Disabled (Synchronous)" },
         { "1",   "1 Sector (7KB / 2ms)" },
         { "2",  "2 Sectors (13KB / 4ms)" },
         { "3",  "3 Sectors (20KB / 6ms)" },
         { "4",  "4 Sectors (27KB / 8ms)" },
         { "5",  "5 Sectors (33KB / 10ms)" },
         { "6",  "6 Sectors (40KB / 12ms)" },
         { "7",  "7 Sectors (47KB / 14ms)" },
         { "8",  "8 Sectors (53KB / 16ms)" },
         { "9",  "9 Sectors (60KB / 18ms)" },
         { "10",  "10 Sectors (67KB / 20ms)" },
         { "11",  "11 Sectors (73KB / 22ms)" },
         { "12",  "12 Sectors (80KB / 24ms)" },
         { "13",  "13 Sectors (87KB / 26ms)" },
         { "14",  "14 Sectors (93KB / 28ms)" },
         { "15",  "15 Sectors (100KB / 30ms)" },
         { "16",  "16 Sectors (107KB / 32ms)" },
         { "17",  "17 Sectors (113KB / 34ms)" },
         { "18",  "18 Sectors (120KB / 36ms)" },
         { "19", "19 Sectors (127KB / 38ms)" },
         { "20", "20 Sectors (133KB / 40ms)" },
         { "21", "21 Sectors (140KB / 42ms)" },
         { "22",  "22 Sectors (147KB / 44ms)" },
         { "23",  "23 Sectors (153KB / 46ms)" },
         { "24",  "24 Sectors (160KB / 48ms)" },
         { "25",  "25 Sectors (167KB / 50ms)" },
         { "26",  "26 Sectors (173KB / 52ms)" },
         { "27",  "27 Sectors (180KB / 54ms)" },
         { "28",  "28 Sectors (187KB / 56ms)" },
         { "29", "29 Sectors (193KB / 58ms)" },
         { "30",  "30 Sectors (200KB / 60ms)" },
         { "31",  "31 Sectors (207KB / 62ms)" },
         { "32", "32 Sectors (213KB / 64ms)" },
         { NULL, NULL },
      },
      "8"
   },
   {
      "duckstation_CPU.Overclock",
      "CPU Overclocking",
      NULL,
      "Runs the emulated CPU faster or slower than native speed, which can improve framerates in some games. Will break "
      "other games and increase system requirements, use with caution.",
      NULL,
      "advanced",
      {
         { "25",   "25%" },
         { "50",   "50%" },
         { "100",  "100% (Default)" },
         { "125",  "125%" },
         { "150",  "150%" },
         { "175",  "175%" },
         { "200",  "200%" },
         { "225",  "225%" },
         { "250",  "250%" },
         { "275",  "275%" },
         { "300",  "300%" },
         { "350",  "350%" },
         { "400",  "400%" },
         { "450",  "450%" },
         { "500",  "500%" },
         { "600",  "600%" },
         { "700",  "700%" },
         { "800",  "800%" },
         { "900",  "900%" },
         { "1000", "1000%" },
         { NULL, NULL },
      },
      "100"
   },
   {
      "duckstation_GPU.Renderer",
      "GPU Renderer",
      NULL,
      "Which renderer to use to emulate the GPU.",
      NULL,
      "enhancement",
      {
         { "Auto", "Hardware (Auto)" },
#ifdef WIN32
         { "D3D11", "Hardware (D3D11)" },
#endif
         { "OpenGL", "Hardware (OpenGL)" },
         { "Vulkan", "Hardware (Vulkan)" },
         { "Software", "Software" },
         { NULL, NULL },
      },
#ifdef WIN32
      "D3D11"
#else
      "Auto"
#endif
   },
   {
      "duckstation_GPU.ResolutionScale",
      "Internal Resolution Scale",
      NULL,
      "Scales internal VRAM resolution by the specified multiplier for the hardware renderer. Larger values are slower. "
      " Some games require 1x VRAM resolution or they will have rendering issues.",
      NULL,
      "enhancement",
      {
         { "1",  "1x" },
         { "2",  "2x" },
         { "3",  "3x (for 720p)" },
         { "4",  "4x" },
         { "5",  "5x (for 1080p)" },
         { "6",  "6x (for 1440p)" },
         { "7",  "7x" },
         { "8",  "8x" },
         { "9",  "9x (for 4K)" },
         { "10", "10x" },
         { "11", "11x" },
         { "12", "12x" },
         { "13", "13x" },
         { "14", "14x" },
         { "15", "15x" },
         { "16", "16x" },
         { NULL, NULL },
      },
      "1"
   },
   {
      "duckstation_GPU.ResolutionSoftScale",
      "Internal Resolution Scale (Software)",
      NULL,
      "Scales internal VRAM resolution by the specified multiplier for the software renderer. Larger values are slower. "
      "Some games require 1x VRAM resolution or they will have rendering issues.",
      NULL,
      "enhancement",
      {
         { "1",  "1x" },
#ifndef ANDROID
         { "2",  "2x" },
         { "4",  "4x" },
#endif
         { NULL, NULL },
      },
      "1"
   },
   {
      "duckstation_GPU.UseThread",
      "Threaded Rendering (Software)",
      NULL,
      "Uses a second thread for drawing graphics. Currently only available for the software renderer.",
      NULL,
      "enhancement",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "true"
   },
   {
      "duckstation_GPU.UseSoftwareRendererForReadbacks",
      "Use Software Renderer For Readbacks",
      NULL,
      "Runs the software renderer in parallel for VRAM readbacks. On some systems, this may "
      "result in greater performance when using graphical enhancements with the hardware renderer.",
      NULL,
      "enhancement",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "false"
   },
   {
      "duckstation_GPU.MSAA",
      "Multisample Antialiasing",
      NULL,
      "Uses multisample antialiasing for rendering 3D objects. Can smooth out jagged edges on polygons at a lower "
      "cost to performance compared to increasing the resolution scale, but may be more likely to cause rendering "
      "errors in some games.",
      NULL,
      "enhancement",
      {
         { "1",       "Disabled" },
         { "2",       "2x MSAA" },
         { "4",       "4x MSAA" },
         { "8",       "8x MSAA" },
         { "16",      "16x MSAA" },
         { "32",      "32x MSAA" },
         { "2-ssaa",  "2x SSAA" },
         { "4-ssaa",  "4x SSAA" },
         { "8-ssaa",  "8x SSAA" },
         { "16-ssaa", "16x SSAA" },
         { "32-ssaa", "32x SSAA" },
         { NULL, NULL },
      },
      "1"
   },
   {
      "duckstation_GPU.TrueColor",
      "True Color Rendering",
      NULL,
      "Disables dithering and uses the full 8 bits per channel of color information. May break rendering in some games.",
      NULL,
      "enhancement",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "false"
   },
   {
      "duckstation_GPU.ScaledDithering",
      "Scaled Dithering",
      NULL,
      "Scales the dithering pattern with the internal rendering resolution, making it less noticeable. Usually safe to enable.",
      NULL,
      "enhancement",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "true"
   },
   {
      "duckstation_GPU.DisableInterlacing",
      "Disable Interlacing",
      NULL,
      "Disables interlaced rendering and display in the GPU. Some games can render in 480p this way, but others will break.",
      NULL,
      "enhancement",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "false"
   },
   {
      "duckstation_GPU.ForceNTSCTimings",
      "Force NTSC Timings",
      NULL,
      "Forces PAL games to run at NTSC timings, i.e. 60hz. Some PAL games will run at their \"normal\" speeds, while others will break.",
      NULL,
      "enhancement",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "false"
   },
   {
      "duckstation_Display.Force4_3For24Bit",
      "Force 4:3 For 24-Bit Display",
      NULL,
      "Switches back to 4:3 display aspect ratio when displaying 24-bit content, usually FMVs.",
      NULL,
      "enhancement",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "false"
   },
   {
      "duckstation_GPU.ChromaSmoothing24Bit",
      "Chroma Smoothing For 24-Bit Display",
      NULL,
      "Smooths out blockyness between colour transitions in 24-bit content, usually FMVs. Only applies to the hardware renderers.",
      NULL,
      "enhancement",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "false"
   },
   {
      "duckstation_GPU.TextureFilter",
      "Texture Filtering",
      NULL,
      "Smooths out the blockyness of magnified textures on 3D object by using bilinear filtering. Will have a "
      "greater effect on higher resolution scales. Only applies to the hardware renderers.",
      NULL,
      "enhancement",
      {
         { "Nearest",          "Nearest-Neighbor" },
         { "Bilinear",         "Bilinear" },
         { "BilinearBinAlpha", "Bilinear (No Edge Blending)" },
         { "JINC2",            "JINC2" },
         { "JINC2BinAlpha",    "JINC2 (No Edge Blending)" },
         { "xBR",              "xBR" },
         { "xBRBinAlpha",      "xBR (No Edge Blending)" },
         { NULL, NULL },
      },
      "Nearest"
   },
   {
      "duckstation_GPU.WidescreenHack",
      "Widescreen Hack",
      NULL,
      "Increases the field of view from 4:3 to the chosen display aspect ratio in 3D games. For 2D games, or games which "
      "use pre-rendered backgrounds, this enhancement will not work as expected.",
      NULL,
      "enhancement",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "false"
   },
   {
      "duckstation_GPU.PGXPEnable",
      "PGXP Geometry Correction",
      NULL,
      "Reduces \"wobbly\" polygons by attempting to preserve the fractional component through memory transfers. Only "
      "works with the hardware renderers, and may not be compatible with all games.",
      NULL,
      "enhancement",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "false"
   },
   {
      "duckstation_GPU.PGXPCulling",
      "PGXP Culling Correction",
      NULL,
      "Increases the precision of polygon culling, reducing the number of holes in geometry. Requires geometry correction enabled.",
      NULL,
      "enhancement",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "true"
   },
   {
      "duckstation_GPU.PGXPTextureCorrection",
      "PGXP Texture Correction",
      NULL,
      "Uses perspective-correct interpolation for texture coordinates and colors, straightening out warped textures. "
      "Requires geometry correction enabled.",
      NULL,
      "enhancement",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "true"
   },
   {
      "duckstation_GPU.PGXPDepthBuffer",
      "PGXP Depth Buffer",
      NULL,
      "Attempts to reduce polygon Z-fighting by testing pixels against the depth values from PGXP. Low compatibility, "
      "but can work well in some games. Requires geometry correction enabled.",
      NULL,
      "enhancement",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "false"
   },
   {
      "duckstation_GPU.PGXPVertexCache",
      "PGXP Vertex Cache",
      NULL,
      "Uses screen coordinates as a fallback when tracking vertices through memory fails. May improve PGXP compatibility.",
      NULL,
      "enhancement",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "false"
   },
   {
      "duckstation_GPU.PGXPCPU",
      "PGXP CPU Mode",
      NULL,
      "Tries to track vertex manipulation through the CPU. Some games require this option for PGXP to be effective. "
      "Very slow, and incompatible with the recompiler.",
      NULL,
      "enhancement",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "false"
   },
   {
      "duckstation_Display.AspectRatio",
      "Aspect Ratio",
      NULL,
      "Sets the core-provided aspect ratio.",
      NULL,
      "display",
      {
         { "Auto",           "Auto (Game Native)" },
         { "4:3",            "4:3" },
         { "16:9",           "16:9" },
         { "16:10",          "16:10" },
         { "19:9",           "19:9" },
         { "21:9",           "21:9" },
         { "32:9",           "32:9" },
         { "8:7",            "8:7" },
         { "5:4",            "5:4" },
         { "3:2",            "3:2" },
         { "2:1 (VRAM 1:1)", "2:1 (VRAM 1:1)" },
         { "1:1",            "1:1" },
         { "PAR 1:1",        "PAR 1:1" },
         { NULL, NULL },
      },
      "Auto"
   },
   {
      "duckstation_Display.CropMode",
      "Crop Mode",
      NULL,
      "Changes how much of the image is cropped. Some games display garbage in the overscan area which is typically hidden.",
      NULL,
      "display",
      {
         { "None",     "None" },
         { "Overscan", "Only Overscan Area" },
         { "Borders",  "All Borders" },
         { NULL, NULL },
      },
      "Overscan"
   },
   {
      "duckstation_Display.LinearFiltering",
      "Linear Upscaling",
      NULL,
      "Uses bilinear texture filtering when displaying the console's framebuffer to the screen. Disabling filtering will produce "
      "a sharper, blockier/pixelated image. Enabling will smooth out the image. This option will be less noticable the higher "
      "the resolution scale.",
      NULL,
      "display",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "true"
   },
   {
      "duckstation_GPU.DownsampleMode",
      "Downsampling",
      NULL,
      "Downsamples the rendered image prior to displaying it. Can improve overall image quality in mixed 2D/3D games, but "
      "should be disabled for pure 3D games. Only applies to the hardware renderers.",
      NULL,
      "display",
      {
         { "Disabled", NULL },
         { "Box",      "Box (Downsample 3D/Smooth All)" },
         { "Adaptive", "Adaptive (Preserve 3D/Smooth 2D)" },
         { NULL, NULL },
      },
      "Disabled"
   },
   {
      "duckstation_Main.LoadDevicesFromSaveStates",
      "Load Devices From Save States",
      NULL,
      "Sets whether the contents of devices and memory cards will be loaded when a save state is loaded.",
      NULL,
      "port",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "false"
   },
   {
      "duckstation_MemoryCards.Card1Type",
      "Memory Card 1 Type",
      NULL,
      "Sets the type of memory card for Slot 1. Restart core when switching to Libretro from other formats.",
      NULL,
      "port",
      {
         { "None",         "No Memory Card" },
         { "Libretro",     NULL },
         { "Shared",       "Shared Between All Games" },
         { "PerGame",      "Separate Card Per Game (Game Code)" },
         { "PerGameTitle", "Separate Card Per Game (Game Title)" },
         { NULL, NULL },
      },
      "Libretro"
   },
   {
      "duckstation_MemoryCards.Card2Type",
      "Memory Card 2 Type",
      NULL,
      "Sets the type of memory card for Slot 2.",
      NULL,
      "port",
      {
         { "None",         "No Memory Card" },
         { "Shared",       "Shared Between All Games" },
         { "PerGame",      "Separate Card Per Game (Game Code)" },
         { "PerGameTitle", "Separate Card Per Game (Game Title)" },
         { NULL, NULL },
      },
      "None"
   },
   {
      "duckstation_MemoryCards.UsePlaylistTitle",
      "Use Single Card For Playlist",
      NULL,
      "When using a playlist (m3u) and per-game (title) memory cards, a single memory card "
      "will be used for all discs. If unchecked, a separate card will be used for each disc.",
      NULL,
      "port",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "true"
   },
   {
      "duckstation_ControllerPorts.MultitapMode",
      "Multitap Mode",
      NULL,
      "Sets the mode for the multitap.",
      NULL,
      "port",
      {
         { "Disabled",  NULL },
         { "Port1Only", "Enable on Port 1 Only" },
         { "Port2Only", "Enable on Port 2 Only" },
         { "BothPorts", "Enable on Ports 1 and 2" },
         { NULL, NULL },
      },
      "Disabled"
   },
   {
      "duckstation_Controller1.ForceAnalogOnReset",
      "Controller 1 Force Analog Mode on Reset",
      NULL,
      "Forces analog mode in Analog Controller (DualShock) at start/reset. May cause issues with some games. Only use "
      "this option for games that support analog mode but do not automatically enable it themselves.",
      NULL,
      "port",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "true"
   },
   {
      "duckstation_Controller1.AnalogDPadInDigitalMode",
      "Controller 1 Use Analog Sticks for D-Pad in Digital Mode",
      NULL,
      "Allows you to use the analog sticks to control the d-pad in digital mode, as well as the buttons.",
      NULL,
      "port",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "true"
   },
   {
      "duckstation_Controller1.AxisScale",
      "Controller 1 Analog Axis Scale",
      NULL,
      "Sets the analog stick axis scaling factor.",
      NULL,
      "port",
      {
         { "1.00f", "1.00" },
         { "1.40f", "1.40" },
         { NULL, NULL },
      },
      "1.00f"
   },
   {
      "duckstation_Controller1.VibrationBias",
      "Controller 1 Vibration Bias",
      NULL,
      "Applies an offset to vibration intensities, higher values will make smaller vibrations more noticable.",
      NULL,
      "port",
      {
         { "0", NULL },
         { "1", NULL },
         { "2", NULL },
         { "3", NULL },
         { "4", NULL },
         { "5", NULL },
         { "6", NULL },
         { "7", NULL },
         { "8", NULL },
         { "9", NULL },
         { "10", NULL },
         { "11", NULL },
         { "12", NULL },
         { "13", NULL },
         { "14", NULL },
         { "15", NULL },
         { "16", NULL },
         { "17", NULL },
         { "18", NULL },
         { "19", NULL },
         { "20", NULL },
         { "21", NULL },
         { "22", NULL },
         { "23", NULL },
         { "24", NULL },
         { "25", NULL },
         { "26", NULL },
         { "27", NULL },
         { "28", NULL },
         { "29", NULL },
         { "30", NULL },
         { "31", NULL },
         { "32", NULL },
         { "33", NULL },
         { "34", NULL },
         { "35", NULL },
         { "36", NULL },
         { "37", NULL },
         { "38", NULL },
         { "39", NULL },
         { "40", NULL },
         { "41", NULL },
         { "42", NULL },
         { "43", NULL },
         { "44", NULL },
         { "45", NULL },
         { "46", NULL },
         { "47", NULL },
         { "48", NULL },
         { "49", NULL },
         { "50", NULL },
         { "51", NULL },
         { "52", NULL },
         { "53", NULL },
         { "54", NULL },
         { "55", NULL },
         { "56", NULL },
         { "57", NULL },
         { "58", NULL },
         { "59", NULL },
         { "60", NULL },
         { "61", NULL },
         { "62", NULL },
         { "63", NULL },
         { "64", NULL },
         { "65", NULL },
         { "66", NULL },
         { "67", NULL },
         { "68", NULL },
         { "69", NULL },
         { "70", NULL },
         { "71", NULL },
         { "72", NULL },
         { "73", NULL },
         { "74", NULL },
         { "75", NULL },
         { "76", NULL },
         { "77", NULL },
         { "78", NULL },
         { "79", NULL },
         { "80", NULL },
         { "81", NULL },
         { "82", NULL },
         { "83", NULL },
         { "84", NULL },
         { "85", NULL },
         { "86", NULL },
         { "87", NULL },
         { "88", NULL },
         { "89", NULL },
         { "90", NULL },
         { "91", NULL },
         { "92", NULL },
         { "93", NULL },
         { "94", NULL },
         { "95", NULL },
         { "96", NULL },
         { "97", NULL },
         { "98", NULL },
         { "99", NULL },
         { "100", NULL },
         { "101", NULL },
         { "102", NULL },
         { "103", NULL },
         { "104", NULL },
         { "105", NULL },
         { "106", NULL },
         { "107", NULL },
         { "108", NULL },
         { "109", NULL },
         { "110", NULL },
         { "111", NULL },
         { "112", NULL },
         { "113", NULL },
         { "114", NULL },
         { "115", NULL },
         { "116", NULL },
         { "117", NULL },
         { "118", NULL },
         { "119", NULL },
         { "120", NULL },
         { "121", NULL },
         { "122", NULL },
         { "123", NULL },
         { "124", NULL },
         { "125", NULL },
         { "126", NULL },
         { NULL, NULL },
      },
      "8"
   },
   {
      "duckstation_Controller2.ForceAnalogOnReset",
      "Controller 2 Force Analog Mode on Reset",
      NULL,
      "Forces analog mode in Analog Controller (DualShock) at start/reset. May cause issues with some games. Only use "
      "this option for games that support analog mode but do not automatically enable it themselves.",
      NULL,
      "port",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "true"
   },
   {
      "duckstation_Controller2.AnalogDPadInDigitalMode",
      "Controller 2 Use Analog Sticks for D-Pad in Digital Mode",
      NULL,
      "Allows you to use the analog sticks to control the d-pad in digital mode, as well as the buttons.",
      NULL,
      "port",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "true"
   },
   {
      "duckstation_Controller2.AxisScale",
      "Controller 2 Analog Axis Scale",
      NULL,
      "Sets the analog stick axis scaling factor.",
      NULL,
      "port",
      {
         { "1.00f", "1.00" }, 
         { "1.40f", "1.40" },
         { NULL, NULL },
      },
      "1.00f"
   },
   {
      "duckstation_Controller2.VibrationBias",
      "Controller 2 Vibration Bias",
      NULL,
      "Applies an offset to vibration intensities, higher values will make smaller vibrations more noticable.",
      NULL,
      "port",
      {
         { "0", NULL },
         { "1", NULL },
         { "2", NULL },
         { "3", NULL },
         { "4", NULL },
         { "5", NULL },
         { "6", NULL },
         { "7", NULL },
         { "8", NULL },
         { "9", NULL },
         { "10", NULL },
         { "11", NULL },
         { "12", NULL },
         { "13", NULL },
         { "14", NULL },
         { "15", NULL },
         { "16", NULL },
         { "17", NULL },
         { "18", NULL },
         { "19", NULL },
         { "20", NULL },
         { "21", NULL },
         { "22", NULL },
         { "23", NULL },
         { "24", NULL },
         { "25", NULL },
         { "26", NULL },
         { "27", NULL },
         { "28", NULL },
         { "29", NULL },
         { "30", NULL },
         { "31", NULL },
         { "32", NULL },
         { "33", NULL },
         { "34", NULL },
         { "35", NULL },
         { "36", NULL },
         { "37", NULL },
         { "38", NULL },
         { "39", NULL },
         { "40", NULL },
         { "41", NULL },
         { "42", NULL },
         { "43", NULL },
         { "44", NULL },
         { "45", NULL },
         { "46", NULL },
         { "47", NULL },
         { "48", NULL },
         { "49", NULL },
         { "50", NULL },
         { "51", NULL },
         { "52", NULL },
         { "53", NULL },
         { "54", NULL },
         { "55", NULL },
         { "56", NULL },
         { "57", NULL },
         { "58", NULL },
         { "59", NULL },
         { "60", NULL },
         { "61", NULL },
         { "62", NULL },
         { "63", NULL },
         { "64", NULL },
         { "65", NULL },
         { "66", NULL },
         { "67", NULL },
         { "68", NULL },
         { "69", NULL },
         { "70", NULL },
         { "71", NULL },
         { "72", NULL },
         { "73", NULL },
         { "74", NULL },
         { "75", NULL },
         { "76", NULL },
         { "77", NULL },
         { "78", NULL },
         { "79", NULL },
         { "80", NULL },
         { "81", NULL },
         { "82", NULL },
         { "83", NULL },
         { "84", NULL },
         { "85", NULL },
         { "86", NULL },
         { "87", NULL },
         { "88", NULL },
         { "89", NULL },
         { "90", NULL },
         { "91", NULL },
         { "92", NULL },
         { "93", NULL },
         { "94", NULL },
         { "95", NULL },
         { "96", NULL },
         { "97", NULL },
         { "98", NULL },
         { "99", NULL },
         { "100", NULL },
         { "101", NULL },
         { "102", NULL },
         { "103", NULL },
         { "104", NULL },
         { "105", NULL },
         { "106", NULL },
         { "107", NULL },
         { "108", NULL },
         { "109", NULL },
         { "110", NULL },
         { "111", NULL },
         { "112", NULL },
         { "113", NULL },
         { "114", NULL },
         { "115", NULL },
         { "116", NULL },
         { "117", NULL },
         { "118", NULL },
         { "119", NULL },
         { "120", NULL },
         { "121", NULL },
         { "122", NULL },
         { "123", NULL },
         { "124", NULL },
         { "125", NULL },
         { "126", NULL },
         { NULL, NULL },
      },
      "8"
   },
   {
      "duckstation_Controller3.ForceAnalogOnReset",
      "Controller 3 Force Analog Mode on Reset",
      NULL,
      "Forces analog mode in Analog Controller (DualShock) at start/reset. May cause issues with some games. Only use "
      "this option for games that support analog mode but do not automatically enable it themselves.",
      NULL,
      "port",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "true"
   },
   {
      "duckstation_Controller3.AnalogDPadInDigitalMode",
      "Controller 3 Use Analog Sticks for D-Pad in Digital Mode",
      NULL,
      "Allows you to use the analog sticks to control the d-pad in digital mode, as well as the buttons.",
      NULL,
      "port",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "true"
   },
   {
      "duckstation_Controller3.AxisScale",
      "Controller 3 Analog Axis Scale",
      NULL,
      "Sets the analog stick axis scaling factor.",
      NULL,
      "port",
      {
         { "1.00f", "1.00" }, 
         { "1.40f", "1.40" },
         { NULL, NULL },
      },
      "1.00f"
   },
   {
      "duckstation_Controller3.VibrationBias",
      "Controller 3 Vibration Bias",
      NULL,
      "Applies an offset to vibration intensities, higher values will make smaller vibrations more noticable.",
      NULL,
      "port",
      {
         { "0", NULL },
         { "1", NULL },
         { "2", NULL },
         { "3", NULL },
         { "4", NULL },
         { "5", NULL },
         { "6", NULL },
         { "7", NULL },
         { "8", NULL },
         { "9", NULL },
         { "10", NULL },
         { "11", NULL },
         { "12", NULL },
         { "13", NULL },
         { "14", NULL },
         { "15", NULL },
         { "16", NULL },
         { "17", NULL },
         { "18", NULL },
         { "19", NULL },
         { "20", NULL },
         { "21", NULL },
         { "22", NULL },
         { "23", NULL },
         { "24", NULL },
         { "25", NULL },
         { "26", NULL },
         { "27", NULL },
         { "28", NULL },
         { "29", NULL },
         { "30", NULL },
         { "31", NULL },
         { "32", NULL },
         { "33", NULL },
         { "34", NULL },
         { "35", NULL },
         { "36", NULL },
         { "37", NULL },
         { "38", NULL },
         { "39", NULL },
         { "40", NULL },
         { "41", NULL },
         { "42", NULL },
         { "43", NULL },
         { "44", NULL },
         { "45", NULL },
         { "46", NULL },
         { "47", NULL },
         { "48", NULL },
         { "49", NULL },
         { "50", NULL },
         { "51", NULL },
         { "52", NULL },
         { "53", NULL },
         { "54", NULL },
         { "55", NULL },
         { "56", NULL },
         { "57", NULL },
         { "58", NULL },
         { "59", NULL },
         { "60", NULL },
         { "61", NULL },
         { "62", NULL },
         { "63", NULL },
         { "64", NULL },
         { "65", NULL },
         { "66", NULL },
         { "67", NULL },
         { "68", NULL },
         { "69", NULL },
         { "70", NULL },
         { "71", NULL },
         { "72", NULL },
         { "73", NULL },
         { "74", NULL },
         { "75", NULL },
         { "76", NULL },
         { "77", NULL },
         { "78", NULL },
         { "79", NULL },
         { "80", NULL },
         { "81", NULL },
         { "82", NULL },
         { "83", NULL },
         { "84", NULL },
         { "85", NULL },
         { "86", NULL },
         { "87", NULL },
         { "88", NULL },
         { "89", NULL },
         { "90", NULL },
         { "91", NULL },
         { "92", NULL },
         { "93", NULL },
         { "94", NULL },
         { "95", NULL },
         { "96", NULL },
         { "97", NULL },
         { "98", NULL },
         { "99", NULL },
         { "100", NULL },
         { "101", NULL },
         { "102", NULL },
         { "103", NULL },
         { "104", NULL },
         { "105", NULL },
         { "106", NULL },
         { "107", NULL },
         { "108", NULL },
         { "109", NULL },
         { "110", NULL },
         { "111", NULL },
         { "112", NULL },
         { "113", NULL },
         { "114", NULL },
         { "115", NULL },
         { "116", NULL },
         { "117", NULL },
         { "118", NULL },
         { "119", NULL },
         { "120", NULL },
         { "121", NULL },
         { "122", NULL },
         { "123", NULL },
         { "124", NULL },
         { "125", NULL },
         { "126", NULL },
         { NULL, NULL },
      },
      "8"
   },
   {
      "duckstation_Controller4.ForceAnalogOnReset",
      "Controller 4 Force Analog Mode on Reset",
      NULL,
      "Forces analog mode in Analog Controller (DualShock) at start/reset. May cause issues with some games. Only use "
      "this option for games that support analog mode but do not automatically enable it themselves.",
      NULL,
      "port",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "true"
   },
   {
      "duckstation_Controller4.AnalogDPadInDigitalMode",
      "Controller 4 Use Analog Sticks for D-Pad in Digital Mode",
      NULL,
      "Allows you to use the analog sticks to control the d-pad in digital mode, as well as the buttons.",
      NULL,
      "port",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "true"
   },
   {
      "duckstation_Controller4.AxisScale",
      "Controller 4 Analog Axis Scale",
      NULL,
      "Sets the analog stick axis scaling factor.",
      NULL,
      "port",
      {
         { "1.00f", "1.00" }, 
         { "1.40f", "1.40" },
         { NULL, NULL },
      },
      "1.00f"
   },
   {
      "duckstation_Controller4.VibrationBias",
      "Controller 4 Vibration Bias",
      NULL,
      "Applies an offset to vibration intensities, higher values will make smaller vibrations more noticable.",
      NULL,
      "port",
      {
         { "0", NULL },
         { "1", NULL },
         { "2", NULL },
         { "3", NULL },
         { "4", NULL },
         { "5", NULL },
         { "6", NULL },
         { "7", NULL },
         { "8", NULL },
         { "9", NULL },
         { "10", NULL },
         { "11", NULL },
         { "12", NULL },
         { "13", NULL },
         { "14", NULL },
         { "15", NULL },
         { "16", NULL },
         { "17", NULL },
         { "18", NULL },
         { "19", NULL },
         { "20", NULL },
         { "21", NULL },
         { "22", NULL },
         { "23", NULL },
         { "24", NULL },
         { "25", NULL },
         { "26", NULL },
         { "27", NULL },
         { "28", NULL },
         { "29", NULL },
         { "30", NULL },
         { "31", NULL },
         { "32", NULL },
         { "33", NULL },
         { "34", NULL },
         { "35", NULL },
         { "36", NULL },
         { "37", NULL },
         { "38", NULL },
         { "39", NULL },
         { "40", NULL },
         { "41", NULL },
         { "42", NULL },
         { "43", NULL },
         { "44", NULL },
         { "45", NULL },
         { "46", NULL },
         { "47", NULL },
         { "48", NULL },
         { "49", NULL },
         { "50", NULL },
         { "51", NULL },
         { "52", NULL },
         { "53", NULL },
         { "54", NULL },
         { "55", NULL },
         { "56", NULL },
         { "57", NULL },
         { "58", NULL },
         { "59", NULL },
         { "60", NULL },
         { "61", NULL },
         { "62", NULL },
         { "63", NULL },
         { "64", NULL },
         { "65", NULL },
         { "66", NULL },
         { "67", NULL },
         { "68", NULL },
         { "69", NULL },
         { "70", NULL },
         { "71", NULL },
         { "72", NULL },
         { "73", NULL },
         { "74", NULL },
         { "75", NULL },
         { "76", NULL },
         { "77", NULL },
         { "78", NULL },
         { "79", NULL },
         { "80", NULL },
         { "81", NULL },
         { "82", NULL },
         { "83", NULL },
         { "84", NULL },
         { "85", NULL },
         { "86", NULL },
         { "87", NULL },
         { "88", NULL },
         { "89", NULL },
         { "90", NULL },
         { "91", NULL },
         { "92", NULL },
         { "93", NULL },
         { "94", NULL },
         { "95", NULL },
         { "96", NULL },
         { "97", NULL },
         { "98", NULL },
         { "99", NULL },
         { "100", NULL },
         { "101", NULL },
         { "102", NULL },
         { "103", NULL },
         { "104", NULL },
         { "105", NULL },
         { "106", NULL },
         { "107", NULL },
         { "108", NULL },
         { "109", NULL },
         { "110", NULL },
         { "111", NULL },
         { "112", NULL },
         { "113", NULL },
         { "114", NULL },
         { "115", NULL },
         { "116", NULL },
         { "117", NULL },
         { "118", NULL },
         { "119", NULL },
         { "120", NULL },
         { "121", NULL },
         { "122", NULL },
         { "123", NULL },
         { "124", NULL },
         { "125", NULL },
         { "126", NULL },
         { NULL, NULL },
      },
      "8"
   },
   {
      "duckstation_Controller5.ForceAnalogOnReset",
      "Controller 5 Force Analog Mode on Reset",
      NULL,
      "Forces analog mode in Analog Controller (DualShock) at start/reset. May cause issues with some games. Only use "
      "this option for games that support analog mode but do not automatically enable it themselves.",
      NULL,
      "port",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "true"
   },
   {
      "duckstation_Controller5.AnalogDPadInDigitalMode",
      "Controller 5 Use Analog Sticks for D-Pad in Digital Mode",
      NULL,
      "Allows you to use the analog sticks to control the d-pad in digital mode, as well as the buttons.",
      NULL,
      "port",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "true"
   },
   {
      "duckstation_Controller5.AxisScale",
      "Controller 5 Analog Axis Scale",
      NULL,
      "Sets the analog stick axis scaling factor.",
      NULL,
      "port",
      {
         { "1.00f", "1.00" },
         { "1.40f", "1.40" },
         { NULL, NULL },
      },
      "1.00f"
   },
   {
      "duckstation_Controller5.VibrationBias",
      "Controller 5 Vibration Bias",
      NULL,
      "Applies an offset to vibration intensities, higher values will make smaller vibrations more noticable.",
      NULL,
      "port",
      {
         { "0", NULL },
         { "1", NULL },
         { "2", NULL },
         { "3", NULL },
         { "4", NULL },
         { "5", NULL },
         { "6", NULL },
         { "7", NULL },
         { "8", NULL },
         { "9", NULL },
         { "10", NULL },
         { "11", NULL },
         { "12", NULL },
         { "13", NULL },
         { "14", NULL },
         { "15", NULL },
         { "16", NULL },
         { "17", NULL },
         { "18", NULL },
         { "19", NULL },
         { "20", NULL },
         { "21", NULL },
         { "22", NULL },
         { "23", NULL },
         { "24", NULL },
         { "25", NULL },
         { "26", NULL },
         { "27", NULL },
         { "28", NULL },
         { "29", NULL },
         { "30", NULL },
         { "31", NULL },
         { "32", NULL },
         { "33", NULL },
         { "34", NULL },
         { "35", NULL },
         { "36", NULL },
         { "37", NULL },
         { "38", NULL },
         { "39", NULL },
         { "40", NULL },
         { "41", NULL },
         { "42", NULL },
         { "43", NULL },
         { "44", NULL },
         { "45", NULL },
         { "46", NULL },
         { "47", NULL },
         { "48", NULL },
         { "49", NULL },
         { "50", NULL },
         { "51", NULL },
         { "52", NULL },
         { "53", NULL },
         { "54", NULL },
         { "55", NULL },
         { "56", NULL },
         { "57", NULL },
         { "58", NULL },
         { "59", NULL },
         { "60", NULL },
         { "61", NULL },
         { "62", NULL },
         { "63", NULL },
         { "64", NULL },
         { "65", NULL },
         { "66", NULL },
         { "67", NULL },
         { "68", NULL },
         { "69", NULL },
         { "70", NULL },
         { "71", NULL },
         { "72", NULL },
         { "73", NULL },
         { "74", NULL },
         { "75", NULL },
         { "76", NULL },
         { "77", NULL },
         { "78", NULL },
         { "79", NULL },
         { "80", NULL },
         { "81", NULL },
         { "82", NULL },
         { "83", NULL },
         { "84", NULL },
         { "85", NULL },
         { "86", NULL },
         { "87", NULL },
         { "88", NULL },
         { "89", NULL },
         { "90", NULL },
         { "91", NULL },
         { "92", NULL },
         { "93", NULL },
         { "94", NULL },
         { "95", NULL },
         { "96", NULL },
         { "97", NULL },
         { "98", NULL },
         { "99", NULL },
         { "100", NULL },
         { "101", NULL },
         { "102", NULL },
         { "103", NULL },
         { "104", NULL },
         { "105", NULL },
         { "106", NULL },
         { "107", NULL },
         { "108", NULL },
         { "109", NULL },
         { "110", NULL },
         { "111", NULL },
         { "112", NULL },
         { "113", NULL },
         { "114", NULL },
         { "115", NULL },
         { "116", NULL },
         { "117", NULL },
         { "118", NULL },
         { "119", NULL },
         { "120", NULL },
         { "121", NULL },
         { "122", NULL },
         { "123", NULL },
         { "124", NULL },
         { "125", NULL },
         { "126", NULL },
         { NULL, NULL },
      },
      "8"
   },
   {
      "duckstation_Controller6.ForceAnalogOnReset",
      "Controller 6 Force Analog Mode on Reset",
      NULL,
      "Forces analog mode in Analog Controller (DualShock) at start/reset. May cause issues with some games. Only use "
      "this option for games that support analog mode but do not automatically enable it themselves.",
      NULL,
      "port",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "true"
   },
   {
      "duckstation_Controller6.AnalogDPadInDigitalMode",
      "Controller 6 Use Analog Sticks for D-Pad in Digital Mode",
      NULL,
      "Allows you to use the analog sticks to control the d-pad in digital mode, as well as the buttons.",
      NULL,
      "port",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "true"
   },
   {
      "duckstation_Controller6.AxisScale",
      "Controller 6 Analog Axis Scale",
      NULL,
      "Sets the analog stick axis scaling factor.",
      NULL,
      "port",
      {
         { "1.00f", "1.00" },
         { "1.40f", "1.40" },
         { NULL, NULL },
      },
      "1.00f"
   },
   {
      "duckstation_Controller6.VibrationBias",
      "Controller 6 Vibration Bias",
      NULL,
      "Applies an offset to vibration intensities, higher values will make smaller vibrations more noticable.",
      NULL,
      "port",
      {
         { "0", NULL },
         { "1", NULL },
         { "2", NULL },
         { "3", NULL },
         { "4", NULL },
         { "5", NULL },
         { "6", NULL },
         { "7", NULL },
         { "8", NULL },
         { "9", NULL },
         { "10", NULL },
         { "11", NULL },
         { "12", NULL },
         { "13", NULL },
         { "14", NULL },
         { "15", NULL },
         { "16", NULL },
         { "17", NULL },
         { "18", NULL },
         { "19", NULL },
         { "20", NULL },
         { "21", NULL },
         { "22", NULL },
         { "23", NULL },
         { "24", NULL },
         { "25", NULL },
         { "26", NULL },
         { "27", NULL },
         { "28", NULL },
         { "29", NULL },
         { "30", NULL },
         { "31", NULL },
         { "32", NULL },
         { "33", NULL },
         { "34", NULL },
         { "35", NULL },
         { "36", NULL },
         { "37", NULL },
         { "38", NULL },
         { "39", NULL },
         { "40", NULL },
         { "41", NULL },
         { "42", NULL },
         { "43", NULL },
         { "44", NULL },
         { "45", NULL },
         { "46", NULL },
         { "47", NULL },
         { "48", NULL },
         { "49", NULL },
         { "50", NULL },
         { "51", NULL },
         { "52", NULL },
         { "53", NULL },
         { "54", NULL },
         { "55", NULL },
         { "56", NULL },
         { "57", NULL },
         { "58", NULL },
         { "59", NULL },
         { "60", NULL },
         { "61", NULL },
         { "62", NULL },
         { "63", NULL },
         { "64", NULL },
         { "65", NULL },
         { "66", NULL },
         { "67", NULL },
         { "68", NULL },
         { "69", NULL },
         { "70", NULL },
         { "71", NULL },
         { "72", NULL },
         { "73", NULL },
         { "74", NULL },
         { "75", NULL },
         { "76", NULL },
         { "77", NULL },
         { "78", NULL },
         { "79", NULL },
         { "80", NULL },
         { "81", NULL },
         { "82", NULL },
         { "83", NULL },
         { "84", NULL },
         { "85", NULL },
         { "86", NULL },
         { "87", NULL },
         { "88", NULL },
         { "89", NULL },
         { "90", NULL },
         { "91", NULL },
         { "92", NULL },
         { "93", NULL },
         { "94", NULL },
         { "95", NULL },
         { "96", NULL },
         { "97", NULL },
         { "98", NULL },
         { "99", NULL },
         { "100", NULL },
         { "101", NULL },
         { "102", NULL },
         { "103", NULL },
         { "104", NULL },
         { "105", NULL },
         { "106", NULL },
         { "107", NULL },
         { "108", NULL },
         { "109", NULL },
         { "110", NULL },
         { "111", NULL },
         { "112", NULL },
         { "113", NULL },
         { "114", NULL },
         { "115", NULL },
         { "116", NULL },
         { "117", NULL },
         { "118", NULL },
         { "119", NULL },
         { "120", NULL },
         { "121", NULL },
         { "122", NULL },
         { "123", NULL },
         { "124", NULL },
         { "125", NULL },
         { "126", NULL },
         { NULL, NULL },
      },
      "8"
   },
   {
      "duckstation_Controller7.ForceAnalogOnReset",
      "Controller 7 Force Analog Mode on Reset",
      NULL,
      "Forces analog mode in Analog Controller (DualShock) at start/reset. May cause issues with some games. Only use "
      "this option for games that support analog mode but do not automatically enable it themselves.",
      NULL,
      "port",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "true"
   },
   {
      "duckstation_Controller7.AnalogDPadInDigitalMode",
      "Controller 7 Use Analog Sticks for D-Pad in Digital Mode",
      NULL,
      "Allows you to use the analog sticks to control the d-pad in digital mode, as well as the buttons.",
      NULL,
      "port",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "true"
   },
   {
      "duckstation_Controller7.AxisScale",
      "Controller 7 Analog Axis Scale",
      NULL,
      "Sets the analog stick axis scaling factor.",
      NULL,
      "port",
      {
         { "1.00f", "1.00" },
         { "1.40f", "1.40" },
         { NULL, NULL },
      },
      "1.00f"
   },
   {
      "duckstation_Controller7.VibrationBias",
      "Controller 7 Vibration Bias",
      NULL,
      "Applies an offset to vibration intensities, higher values will make smaller vibrations more noticable.",
      NULL,
      "port",
      {
         { "0", NULL },
         { "1", NULL },
         { "2", NULL },
         { "3", NULL },
         { "4", NULL },
         { "5", NULL },
         { "6", NULL },
         { "7", NULL },
         { "8", NULL },
         { "9", NULL },
         { "10", NULL },
         { "11", NULL },
         { "12", NULL },
         { "13", NULL },
         { "14", NULL },
         { "15", NULL },
         { "16", NULL },
         { "17", NULL },
         { "18", NULL },
         { "19", NULL },
         { "20", NULL },
         { "21", NULL },
         { "22", NULL },
         { "23", NULL },
         { "24", NULL },
         { "25", NULL },
         { "26", NULL },
         { "27", NULL },
         { "28", NULL },
         { "29", NULL },
         { "30", NULL },
         { "31", NULL },
         { "32", NULL },
         { "33", NULL },
         { "34", NULL },
         { "35", NULL },
         { "36", NULL },
         { "37", NULL },
         { "38", NULL },
         { "39", NULL },
         { "40", NULL },
         { "41", NULL },
         { "42", NULL },
         { "43", NULL },
         { "44", NULL },
         { "45", NULL },
         { "46", NULL },
         { "47", NULL },
         { "48", NULL },
         { "49", NULL },
         { "50", NULL },
         { "51", NULL },
         { "52", NULL },
         { "53", NULL },
         { "54", NULL },
         { "55", NULL },
         { "56", NULL },
         { "57", NULL },
         { "58", NULL },
         { "59", NULL },
         { "60", NULL },
         { "61", NULL },
         { "62", NULL },
         { "63", NULL },
         { "64", NULL },
         { "65", NULL },
         { "66", NULL },
         { "67", NULL },
         { "68", NULL },
         { "69", NULL },
         { "70", NULL },
         { "71", NULL },
         { "72", NULL },
         { "73", NULL },
         { "74", NULL },
         { "75", NULL },
         { "76", NULL },
         { "77", NULL },
         { "78", NULL },
         { "79", NULL },
         { "80", NULL },
         { "81", NULL },
         { "82", NULL },
         { "83", NULL },
         { "84", NULL },
         { "85", NULL },
         { "86", NULL },
         { "87", NULL },
         { "88", NULL },
         { "89", NULL },
         { "90", NULL },
         { "91", NULL },
         { "92", NULL },
         { "93", NULL },
         { "94", NULL },
         { "95", NULL },
         { "96", NULL },
         { "97", NULL },
         { "98", NULL },
         { "99", NULL },
         { "100", NULL },
         { "101", NULL },
         { "102", NULL },
         { "103", NULL },
         { "104", NULL },
         { "105", NULL },
         { "106", NULL },
         { "107", NULL },
         { "108", NULL },
         { "109", NULL },
         { "110", NULL },
         { "111", NULL },
         { "112", NULL },
         { "113", NULL },
         { "114", NULL },
         { "115", NULL },
         { "116", NULL },
         { "117", NULL },
         { "118", NULL },
         { "119", NULL },
         { "120", NULL },
         { "121", NULL },
         { "122", NULL },
         { "123", NULL },
         { "124", NULL },
         { "125", NULL },
         { "126", NULL },
         { NULL, NULL },
      },
      "8"
   },
   {
      "duckstation_Controller8.ForceAnalogOnReset",
      "Controller 8 Force Analog Mode on Reset",
      NULL,
      "Forces analog mode in Analog Controller (DualShock) at start/reset. May cause issues with some games. Only use "
      "this option for games that support analog mode but do not automatically enable it themselves.",
      NULL,
      "port",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "true"
   },
   {
      "duckstation_Controller8.AnalogDPadInDigitalMode",
      "Controller 8 Use Analog Sticks for D-Pad in Digital Mode",
      NULL,
      "Allows you to use the analog sticks to control the d-pad in digital mode, as well as the buttons.",
      NULL,
      "port",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "true"
   },
   {
      "duckstation_Controller8.AxisScale",
      "Controller 8 Analog Axis Scale",
      NULL,
      "Sets the analog stick axis scaling factor.",
      NULL,
      "port",
      {
         { "1.00f", "1.00" },
         { "1.40f", "1.40" },
         { NULL, NULL },
      },
      "1.00f"
   },
   {
      "duckstation_Controller8.VibrationBias",
      "Controller 8 Vibration Bias",
      NULL,
      "Applies an offset to vibration intensities, higher values will make smaller vibrations more noticable.",
      NULL,
      "port",
      {
         { "0", NULL },
         { "1", NULL },
         { "2", NULL },
         { "3", NULL },
         { "4", NULL },
         { "5", NULL },
         { "6", NULL },
         { "7", NULL },
         { "8", NULL },
         { "9", NULL },
         { "10", NULL },
         { "11", NULL },
         { "12", NULL },
         { "13", NULL },
         { "14", NULL },
         { "15", NULL },
         { "16", NULL },
         { "17", NULL },
         { "18", NULL },
         { "19", NULL },
         { "20", NULL },
         { "21", NULL },
         { "22", NULL },
         { "23", NULL },
         { "24", NULL },
         { "25", NULL },
         { "26", NULL },
         { "27", NULL },
         { "28", NULL },
         { "29", NULL },
         { "30", NULL },
         { "31", NULL },
         { "32", NULL },
         { "33", NULL },
         { "34", NULL },
         { "35", NULL },
         { "36", NULL },
         { "37", NULL },
         { "38", NULL },
         { "39", NULL },
         { "40", NULL },
         { "41", NULL },
         { "42", NULL },
         { "43", NULL },
         { "44", NULL },
         { "45", NULL },
         { "46", NULL },
         { "47", NULL },
         { "48", NULL },
         { "49", NULL },
         { "50", NULL },
         { "51", NULL },
         { "52", NULL },
         { "53", NULL },
         { "54", NULL },
         { "55", NULL },
         { "56", NULL },
         { "57", NULL },
         { "58", NULL },
         { "59", NULL },
         { "60", NULL },
         { "61", NULL },
         { "62", NULL },
         { "63", NULL },
         { "64", NULL },
         { "65", NULL },
         { "66", NULL },
         { "67", NULL },
         { "68", NULL },
         { "69", NULL },
         { "70", NULL },
         { "71", NULL },
         { "72", NULL },
         { "73", NULL },
         { "74", NULL },
         { "75", NULL },
         { "76", NULL },
         { "77", NULL },
         { "78", NULL },
         { "79", NULL },
         { "80", NULL },
         { "81", NULL },
         { "82", NULL },
         { "83", NULL },
         { "84", NULL },
         { "85", NULL },
         { "86", NULL },
         { "87", NULL },
         { "88", NULL },
         { "89", NULL },
         { "90", NULL },
         { "91", NULL },
         { "92", NULL },
         { "93", NULL },
         { "94", NULL },
         { "95", NULL },
         { "96", NULL },
         { "97", NULL },
         { "98", NULL },
         { "99", NULL },
         { "100", NULL },
         { "101", NULL },
         { "102", NULL },
         { "103", NULL },
         { "104", NULL },
         { "105", NULL },
         { "106", NULL },
         { "107", NULL },
         { "108", NULL },
         { "109", NULL },
         { "110", NULL },
         { "111", NULL },
         { "112", NULL },
         { "113", NULL },
         { "114", NULL },
         { "115", NULL },
         { "116", NULL },
         { "117", NULL },
         { "118", NULL },
         { "119", NULL },
         { "120", NULL },
         { "121", NULL },
         { "122", NULL },
         { "123", NULL },
         { "124", NULL },
         { "125", NULL },
         { "126", NULL },
         { NULL, NULL },
      },
      "8"
   },
   {
      "duckstation_Display.ShowOSDMessages",
      "Display OSD Messages",
      NULL,
      "Shows on-screen messages generated by the core.",
      NULL,
      "display",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "true"
   },
   {
      "duckstation_Main.ApplyGameSettings",
      "Apply Compatibility Settings",
      NULL,
      "Automatically disables enhancements on games which are incompatible.",
      NULL,
      "advanced",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "true"
   },
   {
      "duckstation_Logging.LogLevel",
      "Log Level",
      NULL,
      "Sets the level of information logged by the core.",
      NULL,
      "advanced",
      {
         { "None",    "None" },
         { "Error",   "Error" },
         { "Warning", "Warning" },
         { "Perf",    "Performance" },
         { "Success", "Success" },
         { "Info",    "Information" },
         { "Dev",     "Developer" },
         { "Profile", "Profile" },
         { "Debug",   "Debug" },
         { "Trace",   "Trace" },
         { NULL, NULL },
      },
      "Info"
   },
   {
      "duckstation_CPU.RecompilerICache",
      "CPU Recompiler ICache",
      NULL,
      "Determines whether the CPU's instruction cache is simulated in the recompiler. Improves accuracy at a small cost "
      "to performance. If games are running too fast, try enabling this option.",
      NULL,
      "advanced",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "false"
   },
   {
      "duckstation_CPU.RecompilerBlockLinking",
      "CPU Recompiler Block Linking",
      NULL,
      "Enables the generated code to directly jump between blocks without going through the "
      "dispatcher. Provides a measurable speed boost.",
      NULL,
      "advanced",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "true"
   },
   {
      "duckstation_CPU.FastmemMode",
      "CPU Recompiler Fast Memory Access",
      NULL,
      "Uses page faults to determine hardware memory accesses at runtime. Can provide a significant performance "
      "improvement in some games, but make the core more difficult to debug.",
      NULL,
      "advanced",
      {
         {"Disabled", "Disabled (Slowest)"},
         {"MMap",     "MMap (Hardware, Fastest, 64-Bit Only)"},
         {"LUT",      "LUT (Faster)"},
         { NULL, NULL },
      },
      "LUT"
   },
   {
      "duckstation_Debug.ShowVRAM",
      "Show VRAM",
      NULL,
      "Shows the entirety of the console's VRAM instead of the display area (debugging feature).",
      NULL,
      "advanced",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "false"
   },
   {
      "duckstation_Display.ActiveStartOffset",
      "Display Active Start Offset",
      NULL,
      "Pads or crops off lines from the left of the displayed image.",
      NULL,
      "display",
      {
         { "-30", NULL },
         { "-29", NULL },
         { "-28", NULL },
         { "-27", NULL },
         { "-26", NULL },
         { "-25", NULL },
         { "-24", NULL },
         { "-23", NULL },
         { "-22", NULL },
         { "-21", NULL },
         { "-20", NULL },
         { "-19", NULL },
         { "-18", NULL },
         { "-17", NULL },
         { "-16", NULL },
         { "-15", NULL },
         { "-14", NULL },
         { "-13", NULL },
         { "-12", NULL },
         { "-11", NULL },
         { "-10", NULL },
         { "-9",  NULL },
         { "-8",  NULL },
         { "-7",  NULL },
         { "-6",  NULL },
         { "-5",  NULL },
         { "-4",  NULL },
         { "-3",  NULL },
         { "-2",  NULL },
         { "-1",  NULL },
         { "0",   NULL },
         { "1",   NULL },
         { "2",   NULL },
         { "3",   NULL },
         { "4",   NULL },
         { "5",   NULL },
         { "6",   NULL },
         { "7",   NULL },
         { "8",   NULL },
         { "9",   NULL },
         { "10",  NULL },
         { "11",  NULL },
         { "12",  NULL },
         { "13",  NULL },
         { "14",  NULL },
         { "15",  NULL },
         { "16",  NULL },
         { "17",  NULL },
         { "18",  NULL },
         { "19",  NULL },
         { "20",  NULL },
         { "21",  NULL },
         { "22",  NULL },
         { "23",  NULL },
         { "24",  NULL },
         { "25",  NULL },
         { "26",  NULL },
         { "27",  NULL },
         { "28",  NULL },
         { "29",  NULL },
         { "30",  NULL },
         { NULL,  NULL },
      },
      "0"
   },
   {
      "duckstation_Display.ActiveEndOffset",
      "Display Active End Offset",
      NULL,
      "Pads or crops off lines from the right of the displayed image.",
      NULL,
      "display",
      {
         { "-30", NULL },
         { "-29", NULL },
         { "-28", NULL },
         { "-27", NULL },
         { "-26", NULL },
         { "-25", NULL },
         { "-24", NULL },
         { "-23", NULL },
         { "-22", NULL },
         { "-21", NULL },
         { "-20", NULL },
         { "-19", NULL },
         { "-18", NULL },
         { "-17", NULL },
         { "-16", NULL },
         { "-15", NULL },
         { "-14", NULL },
         { "-13", NULL },
         { "-12", NULL },
         { "-11", NULL },
         { "-10", NULL },
         { "-9",  NULL },
         { "-8",  NULL },
         { "-7",  NULL },
         { "-6",  NULL },
         { "-5",  NULL },
         { "-4",  NULL },
         { "-3",  NULL },
         { "-2",  NULL },
         { "-1",  NULL },
         { "0",   NULL },
         { "1",   NULL },
         { "2",   NULL },
         { "3",   NULL },
         { "4",   NULL },
         { "5",   NULL },
         { "6",   NULL },
         { "7",   NULL },
         { "8",   NULL },
         { "9",   NULL },
         { "10",  NULL },
         { "11",  NULL },
         { "12",  NULL },
         { "13",  NULL },
         { "14",  NULL },
         { "15",  NULL },
         { "16",  NULL },
         { "17",  NULL },
         { "18",  NULL },
         { "19",  NULL },
         { "20",  NULL },
         { "21",  NULL },
         { "22",  NULL },
         { "23",  NULL },
         { "24",  NULL },
         { "25",  NULL },
         { "26",  NULL },
         { "27",  NULL },
         { "28",  NULL },
         { "29",  NULL },
         { "30",  NULL },
         { NULL,  NULL },
      },
      "0"
   },
   {
      "duckstation_Display.LineStartOffset",
      "Display Line Start Offset",
      NULL,
      "Pads or crops off lines from the top of the displayed image.",
      NULL,
      "display",
      {
         { "-30", NULL },
         { "-29", NULL },
         { "-28", NULL },
         { "-27", NULL },
         { "-26", NULL },
         { "-25", NULL },
         { "-24", NULL },
         { "-23", NULL },
         { "-22", NULL },
         { "-21", NULL },
         { "-20", NULL },
         { "-19", NULL },
         { "-18", NULL },
         { "-17", NULL },
         { "-16", NULL },
         { "-15", NULL },
         { "-14", NULL },
         { "-13", NULL },
         { "-12", NULL },
         { "-11", NULL },
         { "-10", NULL },
         { "-9",  NULL },
         { "-8",  NULL },
         { "-7",  NULL },
         { "-6",  NULL },
         { "-5",  NULL },
         { "-4",  NULL },
         { "-3",  NULL },
         { "-2",  NULL },
         { "-1",  NULL },
         { "0",   NULL },
         { "1",   NULL },
         { "2",   NULL },
         { "3",   NULL },
         { "4",   NULL },
         { "5",   NULL },
         { "6",   NULL },
         { "7",   NULL },
         { "8",   NULL },
         { "9",   NULL },
         { "10",  NULL },
         { "11",  NULL },
         { "12",  NULL },
         { "13",  NULL },
         { "14",  NULL },
         { "15",  NULL },
         { "16",  NULL },
         { "17",  NULL },
         { "18",  NULL },
         { "19",  NULL },
         { "20",  NULL },
         { "21",  NULL },
         { "22",  NULL },
         { "23",  NULL },
         { "24",  NULL },
         { "25",  NULL },
         { "26",  NULL },
         { "27",  NULL },
         { "28",  NULL },
         { "29",  NULL },
         { "30",  NULL },
         { NULL,  NULL },
      },
      "0"
   },
   {
      "duckstation_Display.LineEndOffset",
      "Display Line End Offset",
      NULL,
      "Pads or crops off lines from the bottom of the displayed image.",
      NULL,
      "display",
      {
         { "-30", NULL },
         { "-29", NULL },
         { "-28", NULL },
         { "-27", NULL },
         { "-26", NULL },
         { "-25", NULL },
         { "-24", NULL },
         { "-23", NULL },
         { "-22", NULL },
         { "-21", NULL },
         { "-20", NULL },
         { "-19", NULL },
         { "-18", NULL },
         { "-17", NULL },
         { "-16", NULL },
         { "-15", NULL },
         { "-14", NULL },
         { "-13", NULL },
         { "-12", NULL },
         { "-11", NULL },
         { "-10", NULL },
         { "-9",  NULL },
         { "-8",  NULL },
         { "-7",  NULL },
         { "-6",  NULL },
         { "-5",  NULL },
         { "-4",  NULL },
         { "-3",  NULL },
         { "-2",  NULL },
         { "-1",  NULL },
         { "0",   NULL },
         { "1",   NULL },
         { "2",   NULL },
         { "3",   NULL },
         { "4",   NULL },
         { "5",   NULL },
         { "6",   NULL },
         { "7",   NULL },
         { "8",   NULL },
         { "9",   NULL },
         { "10",  NULL },
         { "11",  NULL },
         { "12",  NULL },
         { "13",  NULL },
         { "14",  NULL },
         { "15",  NULL },
         { "16",  NULL },
         { "17",  NULL },
         { "18",  NULL },
         { "19",  NULL },
         { "20",  NULL },
         { "21",  NULL },
         { "22",  NULL },
         { "23",  NULL },
         { "24",  NULL },
         { "25",  NULL },
         { "26",  NULL },
         { "27",  NULL },
         { "28",  NULL },
         { "29",  NULL },
         { "30",  NULL },
         { NULL,  NULL },
      },
      "0"
   },
   {
      "duckstation_GPU.PGXPPreserveProjFP",
      "PGXP Preserve Projection Precision",
      NULL,
      "Enables additional precision for PGXP. May improve visuals in some games but break others.",
      NULL,
      "enhancement",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "false"
   },
   {
      "duckstation_GPU.PGXPTolerance",
      "PGXP Geometry Tolerance",
      NULL,
      "Ignores precise positions if the difference exceeds this threshold.",
      NULL,
      "enhancement",
      {
         {"-1.0", "None"},
         {"0.5",  "0.5 pixels"},
         {"1.0",  "1.0 pixels"},
         {"1.5",  "1.5 pixels"},
         {"2.0",  "2.0 pixels"},
         {"2.5",  "2.5 pixels"},
         {"3.0",  "3.0 pixels"},
         {"3.5",  "3.5 pixels"},
         {"4.0",  "4.0 pixels"},
         {"4.5",  "4.5 pixels"},
         {"5.0",  "5.0 pixels"},
         {"5.5",  "5.5 pixels"},
         {"6.0",  "6.0 pixels"},
         {"6.5",  "6.5 pixels"},
         {"7.0",  "7.0 pixels"},
         {"7.5",  "7.5 pixels"},
         {"8.0",  "8.0 pixels"},
         {"8.5",  "8.5 pixels"},
         {"9.0",  "9.0 pixels"},
         {"9.5",  "9.0 pixels"},
         {"10.0", "10.0 pixels"},
         { NULL, NULL },
      },
      "-1.0"
   },
   {
      "duckstation_Main.RunaheadFrameCount",
      "Internal Run-Ahead",
      NULL,
      "Simulates the system ahead of time and rolls back/replays to reduce input lag. Has very high system "
      "requirements and forces CPU Execution Mode to Interpreter.",
      NULL,
      "advanced",
      {
         {"0", "0 Frames (Disabled)"},
         {"1", "1 Frame"},
         {"2", "2 Frames"},
         {"3", "3 Frames"},
         {"4", "4 Frames"},
         {"5", "5 Frames"},
         {"6", "6 Frames"},
         {"7", "7 Frames"},
         {"8", "8 Frames"},
         {"9", "9 Frames"},
         {"10", "10 Frames"},
         { NULL, NULL },
      },
      "0"
   },
   {
      "duckstation_Console.Enable8MBRAM",
      "Enable 8MB RAM (Dev Console)",
      NULL,
      "Enabled an additional 6MB of RAM, usually present on dev consoles. Games have to use a "
      "larger heap size for this additional RAM to be usable, and may break games which rely "
      "on memory mirroring, so it should only be used with compatible mods.",
      NULL,
      "advanced",
      {
         { "true",  "Enabled" },
         { "false", "Disabled" },
         { NULL, NULL },
      },
      "false"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};

struct retro_core_options_v2 options_us = {
   option_cats_us,
   option_defs_us
};

/*
 ********************************
 * Language Mapping
 ********************************
*/

#ifndef HAVE_NO_LANGEXTRA
struct retro_core_options_v2 *options_intl[RETRO_LANGUAGE_LAST] = {
   &options_us,    /* RETRO_LANGUAGE_ENGLISH */
   NULL,           /* RETRO_LANGUAGE_JAPANESE */
   NULL,           /* RETRO_LANGUAGE_FRENCH */
   NULL,           /* RETRO_LANGUAGE_SPANISH */
   NULL,           /* RETRO_LANGUAGE_GERMAN */
   NULL,           /* RETRO_LANGUAGE_ITALIAN */
   NULL,           /* RETRO_LANGUAGE_DUTCH */
   NULL,           /* RETRO_LANGUAGE_PORTUGUESE_BRAZIL */
   NULL,           /* RETRO_LANGUAGE_PORTUGUESE_PORTUGAL */
   NULL,           /* RETRO_LANGUAGE_RUSSIAN */
   NULL,           /* RETRO_LANGUAGE_KOREAN */
   NULL,           /* RETRO_LANGUAGE_CHINESE_TRADITIONAL */
   NULL,           /* RETRO_LANGUAGE_CHINESE_SIMPLIFIED */
   NULL,           /* RETRO_LANGUAGE_ESPERANTO */
   NULL,           /* RETRO_LANGUAGE_POLISH */
   NULL,           /* RETRO_LANGUAGE_VIETNAMESE */
   NULL,           /* RETRO_LANGUAGE_ARABIC */
   NULL,           /* RETRO_LANGUAGE_GREEK */
   NULL,           /* RETRO_LANGUAGE_TURKISH */
};
#endif

/*
 ********************************
 * Functions
 ********************************
*/

/* Handles configuration/setting of core options.
 * Should be called as early as possible - ideally inside
 * retro_set_environment(), and no later than retro_load_game()
 * > We place the function body in the header to avoid the
 *   necessity of adding more .c files (i.e. want this to
 *   be as painless as possible for core devs)
 */

static INLINE void libretro_set_core_options(retro_environment_t g_retro_environment_callback,
      bool *categories_supported)
{
   unsigned version  = 0;
#ifndef HAVE_NO_LANGEXTRA
   unsigned language = 0;
#endif

   if (!g_retro_environment_callback || !categories_supported)
      return;

   *categories_supported = false;

   if (!g_retro_environment_callback(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &version))
      version = 0;

   if (version >= 2)
   {
#ifndef HAVE_NO_LANGEXTRA
      struct retro_core_options_v2_intl core_options_intl;

      core_options_intl.us    = &options_us;
      core_options_intl.local = NULL;

      if (g_retro_environment_callback(RETRO_ENVIRONMENT_GET_LANGUAGE, &language) &&
          (language < RETRO_LANGUAGE_LAST) && (language != RETRO_LANGUAGE_ENGLISH))
         core_options_intl.local = options_intl[language];

      *categories_supported = g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL,
            &core_options_intl);
#else
      *categories_supported = g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2,
            &options_us);
#endif
   }
   else
   {
      size_t i, j;
      size_t option_index              = 0;
      size_t num_options               = 0;
      struct retro_core_option_definition
            *option_v1_defs_us         = NULL;
#ifndef HAVE_NO_LANGEXTRA
      size_t num_options_intl          = 0;
      struct retro_core_option_v2_definition
            *option_defs_intl          = NULL;
      struct retro_core_option_definition
            *option_v1_defs_intl       = NULL;
      struct retro_core_options_intl
            core_options_v1_intl;
#endif
      struct retro_variable *variables = NULL;
      char **values_buf                = NULL;

      /* Determine total number of options */
      while (true)
      {
         if (option_defs_us[num_options].key)
            num_options++;
         else
            break;
      }

      if (version >= 1)
      {
         /* Allocate US array */
         option_v1_defs_us = (struct retro_core_option_definition *)
               calloc(num_options + 1, sizeof(struct retro_core_option_definition));

         /* Copy parameters from option_defs_us array */
         for (i = 0; i < num_options; i++)
         {
            struct retro_core_option_v2_definition *option_def_us = &option_defs_us[i];
            struct retro_core_option_value *option_values         = option_def_us->values;
            struct retro_core_option_definition *option_v1_def_us = &option_v1_defs_us[i];
            struct retro_core_option_value *option_v1_values      = option_v1_def_us->values;

            option_v1_def_us->key           = option_def_us->key;
            option_v1_def_us->desc          = option_def_us->desc;
            option_v1_def_us->info          = option_def_us->info;
            option_v1_def_us->default_value = option_def_us->default_value;

            /* Values must be copied individually... */
            while (option_values->value)
            {
               option_v1_values->value = option_values->value;
               option_v1_values->label = option_values->label;

               option_values++;
               option_v1_values++;
            }
         }

#ifndef HAVE_NO_LANGEXTRA
         if (g_retro_environment_callback(RETRO_ENVIRONMENT_GET_LANGUAGE, &language) &&
             (language < RETRO_LANGUAGE_LAST) && (language != RETRO_LANGUAGE_ENGLISH) &&
             options_intl[language])
            option_defs_intl = options_intl[language]->definitions;

         if (option_defs_intl)
         {
            /* Determine number of intl options */
            while (true)
            {
               if (option_defs_intl[num_options_intl].key)
                  num_options_intl++;
               else
                  break;
            }

            /* Allocate intl array */
            option_v1_defs_intl = (struct retro_core_option_definition *)
                  calloc(num_options_intl + 1, sizeof(struct retro_core_option_definition));

            /* Copy parameters from option_defs_intl array */
            for (i = 0; i < num_options_intl; i++)
            {
               struct retro_core_option_v2_definition *option_def_intl = &option_defs_intl[i];
               struct retro_core_option_value *option_values           = option_def_intl->values;
               struct retro_core_option_definition *option_v1_def_intl = &option_v1_defs_intl[i];
               struct retro_core_option_value *option_v1_values        = option_v1_def_intl->values;

               option_v1_def_intl->key           = option_def_intl->key;
               option_v1_def_intl->desc          = option_def_intl->desc;
               option_v1_def_intl->info          = option_def_intl->info;
               option_v1_def_intl->default_value = option_def_intl->default_value;

               /* Values must be copied individually... */
               while (option_values->value)
               {
                  option_v1_values->value = option_values->value;
                  option_v1_values->label = option_values->label;

                  option_values++;
                  option_v1_values++;
               }
            }
         }

         core_options_v1_intl.us    = option_v1_defs_us;
         core_options_v1_intl.local = option_v1_defs_intl;

         g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL, &core_options_v1_intl);
#else
         g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS, option_v1_defs_us);
#endif
      }
      else
      {
         /* Allocate arrays */
         variables  = (struct retro_variable *)calloc(num_options + 1,
               sizeof(struct retro_variable));
         values_buf = (char **)calloc(num_options, sizeof(char *));

         if (!variables || !values_buf)
            goto error;

         /* Copy parameters from option_defs_us array */
         for (i = 0; i < num_options; i++)
         {
            const char *key                        = option_defs_us[i].key;
            const char *desc                       = option_defs_us[i].desc;
            const char *default_value              = option_defs_us[i].default_value;
            struct retro_core_option_value *values = option_defs_us[i].values;
            size_t buf_len                         = 3;
            size_t default_index                   = 0;

            values_buf[i] = NULL;

            if (desc)
            {
               size_t num_values = 0;

               /* Determine number of values */
               while (true)
               {
                  if (values[num_values].value)
                  {
                     /* Check if this is the default value */
                     if (default_value)
                        if (strcmp(values[num_values].value, default_value) == 0)
                           default_index = num_values;

                     buf_len += strlen(values[num_values].value);
                     num_values++;
                  }
                  else
                     break;
               }

               /* Build values string */
               if (num_values > 0)
               {
                  buf_len += num_values - 1;
                  buf_len += strlen(desc);

                  values_buf[i] = (char *)calloc(buf_len, sizeof(char));
                  if (!values_buf[i])
                     goto error;

                  strcpy(values_buf[i], desc);
                  strcat(values_buf[i], "; ");

                  /* Default value goes first */
                  strcat(values_buf[i], values[default_index].value);

                  /* Add remaining values */
                  for (j = 0; j < num_values; j++)
                  {
                     if (j != default_index)
                     {
                        strcat(values_buf[i], "|");
                        strcat(values_buf[i], values[j].value);
                     }
                  }
               }
            }

            variables[option_index].key   = key;
            variables[option_index].value = values_buf[i];
            option_index++;
         }

         /* Set variables */
         g_retro_environment_callback(RETRO_ENVIRONMENT_SET_VARIABLES, variables);
      }

error:
      /* Clean up */

      if (option_v1_defs_us)
      {
         free(option_v1_defs_us);
         option_v1_defs_us = NULL;
      }

#ifndef HAVE_NO_LANGEXTRA
      if (option_v1_defs_intl)
      {
         free(option_v1_defs_intl);
         option_v1_defs_intl = NULL;
      }
#endif

      if (values_buf)
      {
         for (i = 0; i < num_options; i++)
         {
            if (values_buf[i])
            {
               free(values_buf[i]);
               values_buf[i] = NULL;
            }
         }

         free(values_buf);
         values_buf = NULL;
      }

      if (variables)
      {
         free(variables);
         variables = NULL;
      }
   }
}

#ifdef __cplusplus
}
#endif

#endif
