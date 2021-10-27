#include "libretro_host_interface.h"
#include "common/assert.h"
#include "common/byte_stream.h"
#include "common/platform.h"
#include "common/file_system.h"
#include "common/log.h"
#include "common/string_util.h"
#include "core/analog_controller.h"
#include "core/analog_joystick.h"
#include "core/bus.h"
#include "core/cheats.h"
#include "core/digital_controller.h"
#include "core/gpu.h"
#include "core/negcon.h"
#include "core/system.h"
#include "core/pad.h"
#include "libretro_audio_stream.h"
#include "libretro_game_settings.h"
#include "libretro_host_display.h"
#include "libretro_opengl_host_display.h"
#include "libretro_settings_interface.h"
#include "libretro_vulkan_host_display.h"
#include <array>
#include <cstring>
#include <tuple>
#include <utility>
#include <vector>
Log_SetChannel(LibretroHostInterface);

#ifdef WIN32
#include "libretro_d3d11_host_display.h"
#endif

LibretroHostInterface g_libretro_host_interface;
#define P_THIS (&g_libretro_host_interface)

retro_environment_t g_retro_environment_callback;
retro_video_refresh_t g_retro_video_refresh_callback;
retro_audio_sample_t g_retro_audio_sample_callback;
retro_audio_sample_batch_t g_retro_audio_sample_batch_callback;
retro_input_poll_t g_retro_input_poll_callback;
retro_input_state_t g_retro_input_state_callback;

static retro_log_callback s_libretro_log_callback = {};
static bool s_libretro_log_callback_valid = false;
static bool s_libretro_log_callback_registered = false;
static bool libretro_supports_option_categories = false;
static int show_multitap = -1;

static void LibretroLogCallback(void* pUserParam, const char* channelName, const char* functionName, LOGLEVEL level,
                                const char* message)
{
  static constexpr std::array<retro_log_level, LOGLEVEL_COUNT> levels = {
    {RETRO_LOG_ERROR, RETRO_LOG_ERROR, RETRO_LOG_WARN, RETRO_LOG_INFO, RETRO_LOG_INFO, RETRO_LOG_INFO, RETRO_LOG_DEBUG,
     RETRO_LOG_DEBUG, RETRO_LOG_DEBUG, RETRO_LOG_DEBUG}};

  s_libretro_log_callback.log(levels[level], "[%s] %s\n", (level <= LOGLEVEL_PERF) ? functionName : channelName,
                              message);
}

LibretroHostInterface::LibretroHostInterface() = default;

LibretroHostInterface::~LibretroHostInterface()
{
  // a few things we are safe to cleanup because these pointers are garaunteed to be initialized to zero (0)
  // when the shared library (dll/so) is loaded into memory. Other things are not safe, such as calling
  // HostInterface::Shutdown, because it depends on a bunch of vars being initialized to zero at runtime,
  // otherwise it thinks it needs to clean them up and they're actually invalid, and crashes happen.

  m_audio_stream.reset();   // assert checks will expect this is nullified.
  ReleaseHostDisplay();     // assert checks will expect this is nullified.
}

#include "libretro_core_options.h"

void LibretroHostInterface::retro_set_environment()
{
  libretro_supports_option_categories = false;
  libretro_set_core_options(g_retro_environment_callback, &libretro_supports_option_categories);
  InitLogging();
}

void LibretroHostInterface::InitInterfaces()
{
  InitRumbleInterface();
  InitDiskControlInterface();

  unsigned dummy = 0;
  m_supports_input_bitmasks = g_retro_environment_callback(RETRO_ENVIRONMENT_GET_INPUT_BITMASKS, &dummy);
}

void LibretroHostInterface::InitLogging()
{
  if (s_libretro_log_callback_registered)
    return;

  s_libretro_log_callback_valid =
    g_retro_environment_callback(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &s_libretro_log_callback);

  if (s_libretro_log_callback_valid)
  {
    Log::RegisterCallback(LibretroLogCallback, nullptr);
    s_libretro_log_callback_registered = true;
  }
}

bool LibretroHostInterface::Initialize()
{
  if (!HostInterface::Initialize())
    return false;

  /* Reset disk control info struct */
  P_THIS->m_disk_control_info.has_sub_images      = false;
  P_THIS->m_disk_control_info.initial_image_index = 0;
  P_THIS->m_disk_control_info.image_index         = 0;
  P_THIS->m_disk_control_info.image_count         = 0;
  P_THIS->m_disk_control_info.sub_images_parent_path.clear();
  P_THIS->m_disk_control_info.image_paths.clear();
  P_THIS->m_disk_control_info.image_labels.clear();

  InitInterfaces();
  LibretroSettingsInterface si;
  LoadSettings(si);
  FixIncompatibleSettings(true);
  UpdateLogging();

  m_last_aspect_ratio = g_settings.GetDisplayAspectRatioValue();
  m_last_width = 320;
  m_last_height = 240;
  return true;
}

void LibretroHostInterface::Shutdown()
{
  libretro_supports_option_categories = false;
  HostInterface::Shutdown();

  /* Reset disk control info struct */
  P_THIS->m_disk_control_info.has_sub_images      = false;
  P_THIS->m_disk_control_info.initial_image_index = 0;
  P_THIS->m_disk_control_info.image_index         = 0;
  P_THIS->m_disk_control_info.image_count         = 0;
  P_THIS->m_disk_control_info.sub_images_parent_path.clear();
  P_THIS->m_disk_control_info.image_paths.clear();
  P_THIS->m_disk_control_info.image_labels.clear();
}

void LibretroHostInterface::ReportError(const char* message)
{
  AddFormattedOSDMessage(10.0f, "ERROR: %s", message);
  Log_ErrorPrint(message);
}

void LibretroHostInterface::ReportMessage(const char* message)
{
  AddOSDMessage(message, 5.0f);
  Log_InfoPrint(message);
}

bool LibretroHostInterface::ConfirmMessage(const char* message)
{
  Log_InfoPrintf("Confirm: %s", message);
  return false;
}

void LibretroHostInterface::GetGameInfo(const char* path, CDImage* image, std::string* code, std::string* title)
{
  // Just use the filename for now... we don't have the game list. Unless we can pull this from the frontend somehow?
  *title = FileSystem::GetFileTitleFromPath(path);
  *code = System::GetGameCodeForImage(image, true);
}

static const char* GetSaveDirectory()
{
  const char* save_directory = nullptr;
  if (!g_retro_environment_callback(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &save_directory) || !save_directory)
    save_directory = "saves";

  return save_directory;
}

std::string LibretroHostInterface::GetSharedMemoryCardPath(u32 slot) const
{
  return StringUtil::StdStringFromFormat("%s" FS_OSPATH_SEPARATOR_STR "duckstation_shared_card_%d.mcd",
                                         GetSaveDirectory(), slot + 1);
}

std::string LibretroHostInterface::GetGameMemoryCardPath(const char* game_code, u32 slot) const
{
  return StringUtil::StdStringFromFormat("%s" FS_OSPATH_SEPARATOR_STR "%s_%d.mcd", GetSaveDirectory(), game_code,
                                         slot + 1);
}

std::string LibretroHostInterface::GetShaderCacheBasePath() const
{
  // Use the save directory, and failing that, the system directory.
  const char* save_directory_ptr = nullptr;
  if (!g_retro_environment_callback(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &save_directory_ptr) || !save_directory_ptr)
  {
    save_directory_ptr = nullptr;
    if (!g_retro_environment_callback(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &save_directory_ptr) ||
        !save_directory_ptr)
    {
      Log_WarningPrint("No shader cache directory available, startup will be slower.");
      return std::string();
    }
  }

  // Use a directory named "duckstation_cache" in the save/system directory.
  std::string shader_cache_path = StringUtil::StdStringFromFormat(
    "%s" FS_OSPATH_SEPARATOR_STR "duckstation_cache" FS_OSPATH_SEPARATOR_STR, save_directory_ptr);
  if (!FileSystem::DirectoryExists(shader_cache_path.c_str()) &&
      !FileSystem::CreateDirectory(shader_cache_path.c_str(), false))
  {
    Log_ErrorPrintf("Failed to create shader cache directory: '%s'", shader_cache_path.c_str());
    return std::string();
  }

  Log_InfoPrintf("Shader cache directory: '%s'", shader_cache_path.c_str());
  return shader_cache_path;
}

std::string LibretroHostInterface::GetStringSettingValue(const char* section, const char* key,
                                                         const char* default_value /*= ""*/)
{
  TinyString name;
  name.Format("duckstation_%s.%s", section, key);
  retro_variable var{name, default_value};
  if (g_retro_environment_callback(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    return var.value;
  else
    return default_value;
}

void LibretroHostInterface::AddOSDMessage(std::string message, float duration /*= 2.0f*/)
{
  if (!g_settings.display_show_osd_messages)
    return;

  retro_message msg = {};
  msg.msg = message.c_str();
  msg.frames = static_cast<u32>(duration * (System::IsShutdown() ? 60.0f : System::GetThrottleFrequency()));
  g_retro_environment_callback(RETRO_ENVIRONMENT_SET_MESSAGE, &msg);
}

void LibretroHostInterface::retro_get_system_av_info(struct retro_system_av_info* info)
{
  const bool use_resolution_scale = (g_settings.gpu_renderer != GPURenderer::Software);
  GetSystemAVInfo(info, use_resolution_scale);

  Log_InfoPrintf("base = %ux%u, max = %ux%u, aspect ratio = %.2f, fps = %.2f", info->geometry.base_width,
                 info->geometry.base_height, info->geometry.max_width, info->geometry.max_height,
                 info->geometry.aspect_ratio, info->timing.fps);
}

void LibretroHostInterface::GetSystemAVInfo(struct retro_system_av_info* info, bool use_resolution_scale)
{
  const u32 resolution_scale = use_resolution_scale ? g_settings.gpu_resolution_scale : 1u;
  Assert(System::IsValid());

  std::memset(info, 0, sizeof(*info));

  const auto [effective_width, effective_height] = g_gpu->GetEffectiveDisplayResolution();

  info->geometry.aspect_ratio = m_last_aspect_ratio;
  info->geometry.base_width = effective_width;
  info->geometry.base_height = effective_height;
  info->geometry.max_width = VRAM_WIDTH * resolution_scale;
  info->geometry.max_height = VRAM_HEIGHT * resolution_scale;

  info->timing.fps = System::GetThrottleFrequency();
  info->timing.sample_rate = static_cast<double>(AUDIO_SAMPLE_RATE);
}

bool LibretroHostInterface::UpdateSystemAVInfo(bool use_resolution_scale)
{
  struct retro_system_av_info avi;
  GetSystemAVInfo(&avi, use_resolution_scale);

  Log_InfoPrintf("base = %ux%u, max = %ux%u, aspect ratio = %.2f, fps = %.2f", avi.geometry.base_width,
                 avi.geometry.base_height, avi.geometry.max_width, avi.geometry.max_height, avi.geometry.aspect_ratio,
                 avi.timing.fps);

  if (!g_retro_environment_callback(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &avi))
  {
    Log_ErrorPrintf("Failed to update system AV info on resolution change");
    return false;
  }

  return true;
}

void LibretroHostInterface::UpdateGeometry()
{
  struct retro_system_av_info avi;
  const bool use_resolution_scale = (g_settings.gpu_renderer != GPURenderer::Software);
  GetSystemAVInfo(&avi, use_resolution_scale);

  Log_InfoPrintf("base = %ux%u, max = %ux%u, aspect ratio = %.2f", avi.geometry.base_width, avi.geometry.base_height,
                 avi.geometry.max_width, avi.geometry.max_height, avi.geometry.aspect_ratio);

  if (!g_retro_environment_callback(RETRO_ENVIRONMENT_SET_GEOMETRY, &avi.geometry))
    Log_WarningPrint("RETRO_ENVIRONMENT_SET_GEOMETRY failed");
}

void LibretroHostInterface::UpdateLogging()
{
  Log::SetFilterLevel(g_settings.log_level);

  if (s_libretro_log_callback_valid)
    Log::SetConsoleOutputParams(false);
  else
    Log::SetConsoleOutputParams(true, nullptr, g_settings.log_level);
}

bool LibretroHostInterface::UpdateGameSettings()
{
  std::unique_ptr<GameSettings::Entry> new_game_settings;

  if (!System::IsShutdown() && !System::GetRunningCode().empty())
  {
    new_game_settings = GetSettingsForGame(System::GetRunningCode());
    if (new_game_settings)
      Log_InfoPrintf("Game settings found for %s", System::GetRunningCode().c_str());
  }

  if (new_game_settings == m_game_settings)
    return false;

  m_game_settings = std::move(new_game_settings);
  return true;
}

void LibretroHostInterface::ApplyGameSettings()
{
  if (!g_settings.apply_game_settings || !m_game_settings)
    return;

  m_game_settings->ApplySettings(System::GetState() == System::State::Starting);
}

bool LibretroHostInterface::retro_load_game(const struct retro_game_info* game)
{
  auto bp = std::make_shared<SystemBootParameters>();
  bp->filename = game->path;
  bp->media_playlist_index = P_THIS->m_disk_control_info.initial_image_index;
  bp->force_software_renderer = !m_hw_render_callback_valid;

  struct retro_input_descriptor desc[] = {
#define JOYP(port)                                                                                                     \
  {port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "D-Pad Left"},                                           \
    {port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "D-Pad Up"},                                             \
    {port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "D-Pad Down"},                                         \
    {port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right"},                                       \
    {port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "Cross"},                                                 \
    {port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "Circle"},                                                \
    {port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X, "Triangle"},                                              \
    {port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y, "Square"},                                                \
    {port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L, "L1"},                                                    \
    {port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2, "L2"},                                                   \
    {port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3, "L3"},                                                   \
    {port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R, "R1"},                                                    \
    {port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2, "R2"},                                                   \
    {port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3, "R3"},                                                   \
    {port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Select"},                                           \
    {port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start"},                                             \
    {port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Left Analog X"},            \
    {port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Left Analog Y"},            \
    {port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Right Analog X"},          \
    {port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Right Analog Y"},

    JOYP(0) JOYP(1) JOYP(2) JOYP(3) JOYP(4) JOYP(5) JOYP(6) JOYP(7)

      {0},
  };

  g_retro_environment_callback(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);

  if (!BootSystem(std::move(bp)))
    return false;

  if (g_settings.gpu_renderer != GPURenderer::Software)
  {
    if (!m_hw_render_callback_valid)
      RequestHardwareRendererContext();
    else
      SwitchToHardwareRenderer();
  }

  /* Initialise disk control info struct */
  if (System::HasMedia())
  {
    if (System::HasMediaSubImages())
    {
      const std::string& parent_path = System::GetMediaFileName();
      if (parent_path.empty())
        return false;

      P_THIS->m_disk_control_info.has_sub_images         = true;
      P_THIS->m_disk_control_info.image_index            = System::GetMediaSubImageIndex();
      P_THIS->m_disk_control_info.image_count            = System::GetMediaSubImageCount();
      P_THIS->m_disk_control_info.sub_images_parent_path = parent_path;

      for (u32 i = 0; i < P_THIS->m_disk_control_info.image_count; i++)
      {
        const std::string& sub_image_path = System::GetMediaSubImagePath(i);
        if (sub_image_path.empty())
          return false;

        const std::string& sub_image_label = System::GetMediaSubImageTitle(i);
        if (sub_image_label.empty())
          return false;

        P_THIS->m_disk_control_info.image_paths.push_back(sub_image_path);
        P_THIS->m_disk_control_info.image_labels.push_back(sub_image_label);
      }
    }
    else
    {
      const std::string& image_path = System::GetMediaFileName();
      if (image_path.empty())
        return false;

      const std::string_view image_label = FileSystem::GetFileTitleFromPath(image_path);
      if (image_label.empty())
        return false;

      P_THIS->m_disk_control_info.has_sub_images = false;
      P_THIS->m_disk_control_info.image_index    = 0;
      P_THIS->m_disk_control_info.image_count    = 1;
      P_THIS->m_disk_control_info.sub_images_parent_path.clear();

      P_THIS->m_disk_control_info.image_paths.push_back(image_path);
      P_THIS->m_disk_control_info.image_labels.push_back(std::string(image_label));
    }
  }

  switch (System::GetRegion())
  {
      case  ConsoleRegion::NTSC_J:
      {
         struct retro_core_option_display option_display;
         option_display.visible = false;
         option_display.key = "duckstation_BIOS.PathNTSCU";
         g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
         option_display.key = "duckstation_BIOS.PathPAL";
         g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);

         break;
      }

      case  ConsoleRegion::NTSC_U:
      {
         struct retro_core_option_display option_display;
         option_display.visible = false;
         option_display.key = "duckstation_BIOS.PathNTSCJ";
         g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
         option_display.key = "duckstation_BIOS.PathPAL";
         g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);

         break;
      }

      case  ConsoleRegion::PAL:
      {
         struct retro_core_option_display option_display;
         option_display.visible = false;
         option_display.key = "duckstation_BIOS.PathNTSCU";
         g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
         option_display.key = "duckstation_BIOS.PathNTSCJ";
         g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);

         break;
      }
  }

  struct retro_core_option_display option_display;
  option_display.visible = false;
  if (g_settings.gpu_renderer == GPURenderer::Software)
  {
      option_display.key = "duckstation_GPU.UseSoftwareRendererForReadbacks";
      g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
  }
  else
  {
      option_display.key = "duckstation_GPU.UseThread";
      g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
  }

  return true;
}

void LibretroHostInterface::retro_run_frame()
{
  Assert(!System::IsShutdown());

  if (HasCoreVariablesChanged())
    UpdateSettings();

  UpdateControllers();

  System::RunFrame();

  const float aspect_ratio = m_display->GetDisplayAspectRatio();
  const auto [effective_width, effective_height] = g_gpu->GetEffectiveDisplayResolution();

  if (aspect_ratio != m_last_aspect_ratio || effective_width != m_last_width || effective_height != m_last_height)
  {
    m_last_aspect_ratio = aspect_ratio;
    m_last_width = effective_width;
    m_last_height = effective_height;
    UpdateGeometry();
  }

  m_display->Render();
}

unsigned LibretroHostInterface::retro_get_region()
{
  return System::IsPALRegion() ? RETRO_REGION_PAL : RETRO_REGION_NTSC;
}

size_t LibretroHostInterface::retro_serialize_size()
{
  return System::MAX_SAVE_STATE_SIZE;
}

bool LibretroHostInterface::retro_serialize(void* data, size_t size)
{
  std::unique_ptr<ByteStream> stream = ByteStream_CreateMemoryStream(data, static_cast<u32>(size));
  if (!System::SaveState(stream.get(), 0))
  {
    Log_ErrorPrintf("Failed to save state to memory stream");
    return false;
  }

  return true;
}

bool LibretroHostInterface::retro_unserialize(const void* data, size_t size)
{
  std::unique_ptr<ByteStream> stream = ByteStream_CreateReadOnlyMemoryStream(data, static_cast<u32>(size));
  if (!System::LoadState(stream.get(), false))
  {
    Log_ErrorPrintf("Failed to load save state from memory stream");
    return false;
  }

  return true;
}

void* LibretroHostInterface::retro_get_memory_data(unsigned id)
{
  switch (id)
  {
    case RETRO_MEMORY_SYSTEM_RAM:
      return System::IsShutdown() ? nullptr : Bus::g_ram;

    case RETRO_MEMORY_SAVE_RAM: {
      const MemoryCardType type = g_settings.memory_card_types[0];
      if (System::IsShutdown()  || type != MemoryCardType::Libretro) {
        return nullptr;
      }
      auto card = g_pad.GetMemoryCard(0);
      auto& data = card->GetData();
      return data.data();
      break;
    }

    default:
      return nullptr;
  }
}

size_t LibretroHostInterface::retro_get_memory_size(unsigned id)
{
  switch (id)
  {
    case RETRO_MEMORY_SYSTEM_RAM:
      return Bus::g_ram_size;

    case RETRO_MEMORY_SAVE_RAM: {
      const MemoryCardType type = g_settings.memory_card_types[0];
      if (System::IsShutdown()  || type != MemoryCardType::Libretro) {
        return 0;
      }
      return 128 * 1024;
    }

    default:
      return 0;
  }
}

void LibretroHostInterface::retro_cheat_reset()
{
  System::SetCheatList(nullptr);
}

void LibretroHostInterface::retro_cheat_set(unsigned index, bool enabled, const char* code)
{
  CheatList* cl = System::GetCheatList();
  if (!cl)
  {
    System::SetCheatList(std::make_unique<CheatList>());
    cl = System::GetCheatList();
  }

  CheatCode cc;
  cc.description = StringUtil::StdStringFromFormat("Cheat%u", index);
  cc.enabled = true;
  if (!CheatList::ParseLibretroCheat(&cc, code))
    Log_ErrorPrintf("Failed to parse cheat %u '%s'", index, code);

  cl->SetCode(index, std::move(cc));
}

bool LibretroHostInterface::AcquireHostDisplay()
{
  // start in software mode, switch to hardware later
  m_display = std::make_unique<LibretroHostDisplay>();
  return true;
}

void LibretroHostInterface::ReleaseHostDisplay()
{
  if (m_hw_render_display)
  {
    m_hw_render_display->DestroyRenderDevice();
    m_hw_render_display.reset();
    m_using_hardware_renderer = false;
  }

  if (m_display)
  {
    m_display->DestroyRenderDevice();
    m_display.reset();
  }
}

std::unique_ptr<AudioStream> LibretroHostInterface::CreateAudioStream(AudioBackend backend)
{
  return std::make_unique<LibretroAudioStream>();
}

void LibretroHostInterface::OnSystemDestroyed()
{
  HostInterface::OnSystemDestroyed();
}

bool LibretroHostInterface::HasCoreVariablesChanged()
{
  bool changed = false;
  return (g_retro_environment_callback(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &changed) && changed);
}

std::string LibretroHostInterface::GetBIOSDirectory()
{
  // Assume BIOS files are located in system directory.
  const char* system_directory = nullptr;
  if (!g_retro_environment_callback(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_directory) || !system_directory)
    return GetProgramDirectoryRelativePath("system");
  else
    return system_directory;
}

std::unique_ptr<ByteStream> LibretroHostInterface::OpenPackageFile(const char* path, u32 flags)
{
  Log_ErrorPrintf("Ignoring request for package file '%s'", path);
  return {};
}

void LibretroHostInterface::LoadSettings(SettingsInterface& si)
{
  HostInterface::LoadSettings(si);

  // turn percentage into fraction for overclock
  const u32 overclock_percent = static_cast<u32>(std::max(si.GetIntValue("CPU", "Overclock", 100), 1));
  Settings::CPUOverclockPercentToFraction(overclock_percent, &g_settings.cpu_overclock_numerator,
                                          &g_settings.cpu_overclock_denominator);
  g_settings.cpu_overclock_enable = (overclock_percent != 100);
  g_settings.UpdateOverclockActive();

  // convert msaa settings
  const std::string msaa = si.GetStringValue("GPU", "MSAA", "1");
  g_settings.gpu_multisamples = StringUtil::FromChars<u32>(msaa).value_or(1);
  g_settings.gpu_per_sample_shading = StringUtil::EndsWith(msaa, "-ssaa");

  // Ensure we don't use the standalone memcard directory in shared mode.
  for (u32 i = 0; i < NUM_CONTROLLER_AND_CARD_PORTS; i++)
    g_settings.memory_card_paths[i] = GetSharedMemoryCardPath(i);

  int show_multitap_prev = show_multitap;
  if (g_settings.multitap_mode != MultitapMode::Disabled)
    show_multitap = 1;
  else
    show_multitap = 0;
	

  if (show_multitap != show_multitap_prev)
  {
    unsigned i;
    struct retro_core_option_display option_display;
    char controller_multitap_options[8][49] = {
        "duckstation_Controller3.Type",
        "duckstation_Controller3.ForceAnalogOnReset",
        "duckstation_Controller3.AnalogDPadInDigitalMode",
        "duckstation_Controller3.AxisScale",
        "duckstation_Controller4.Type",
        "duckstation_Controller4.ForceAnalogOnReset",
        "duckstation_Controller4.AnalogDPadInDigitalMode",
        "duckstation_Controller4.AxisScale"
    };

    option_display.visible = show_multitap;

    for (i = 0; i < 8; i++)
    {
        option_display.key = controller_multitap_options[i];
        g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
    }
  }

}

std::vector<std::string> LibretroHostInterface::GetSettingStringList(const char* section, const char* key)
{
  return {};
}

void LibretroHostInterface::UpdateSettings()
{
  Settings old_settings(std::move(g_settings));
  LibretroSettingsInterface si;
  LoadSettings(si);
  ApplyGameSettings();
  FixIncompatibleSettings(false);

  if (System::IsValid())
  {
    if (g_settings.gpu_resolution_scale != old_settings.gpu_resolution_scale &&
        g_settings.gpu_renderer != GPURenderer::Software)
    {
      ReportMessage("Resolution changed, updating system AV info...");

      UpdateSystemAVInfo(true);

      if (!g_settings.IsUsingSoftwareRenderer())
      {
        if (!m_hw_render_callback_valid)
          RequestHardwareRendererContext();
        else if (!m_using_hardware_renderer)
          SwitchToHardwareRenderer();
      }

      // Don't let the base class mess with the GPU.
      old_settings.gpu_resolution_scale = g_settings.gpu_resolution_scale;
    }

    if (g_settings.gpu_renderer != old_settings.gpu_renderer)
    {
      ReportFormattedMessage("Switch to %s renderer pending, please restart the core to apply.",
                             Settings::GetRendererDisplayName(g_settings.gpu_renderer));
      g_settings.gpu_renderer = old_settings.gpu_renderer;
    }
  }

  CheckForSettingsChanges(old_settings);
}

void LibretroHostInterface::CheckForSettingsChanges(const Settings& old_settings)
{
  HostInterface::CheckForSettingsChanges(old_settings);

  if (g_settings.display_aspect_ratio != old_settings.display_aspect_ratio)
    UpdateGeometry();

  if (g_settings.log_level != old_settings.log_level)
    UpdateLogging();
}

void LibretroHostInterface::OnRunningGameChanged(const std::string& path, CDImage* image, const std::string& game_code,
                                                 const std::string& game_title)
{
  Log_InfoPrintf("Running game changed: %s (%s)", System::GetRunningCode().c_str(), System::GetRunningTitle().c_str());
  if (UpdateGameSettings())
    UpdateSettings();
}

void LibretroHostInterface::InitRumbleInterface()
{
  m_rumble_interface_valid = g_retro_environment_callback(RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE, &m_rumble_interface);
}

void LibretroHostInterface::UpdateControllers()
{
  g_retro_input_poll_callback();

  for (u32 i = 0; i < NUM_CONTROLLER_AND_CARD_PORTS; i++)
  {
    switch (g_settings.controller_types[i])
    {
      case ControllerType::None:
        break;

      case ControllerType::DigitalController:
        UpdateControllersDigitalController(i);
        break;

      case ControllerType::AnalogController:
        UpdateControllersAnalogController(i);
        break;

      case ControllerType::AnalogJoystick:
        UpdateControllersAnalogJoystick(i);
        break;

      case ControllerType::NeGcon:
        UpdateControllersNeGcon(i);
        break;

      default:
        ReportFormattedError("Unhandled controller type '%s'.",
                             Settings::GetControllerTypeDisplayName(g_settings.controller_types[i]));
        break;
    }
  }
}

void LibretroHostInterface::UpdateControllersDigitalController(u32 index)
{
  DigitalController* controller = static_cast<DigitalController*>(System::GetController(index));
  DebugAssert(controller);

  static constexpr std::array<std::pair<DigitalController::Button, u32>, 14> mapping = {
    {{DigitalController::Button::Left, RETRO_DEVICE_ID_JOYPAD_LEFT},
     {DigitalController::Button::Right, RETRO_DEVICE_ID_JOYPAD_RIGHT},
     {DigitalController::Button::Up, RETRO_DEVICE_ID_JOYPAD_UP},
     {DigitalController::Button::Down, RETRO_DEVICE_ID_JOYPAD_DOWN},
     {DigitalController::Button::Circle, RETRO_DEVICE_ID_JOYPAD_A},
     {DigitalController::Button::Cross, RETRO_DEVICE_ID_JOYPAD_B},
     {DigitalController::Button::Triangle, RETRO_DEVICE_ID_JOYPAD_X},
     {DigitalController::Button::Square, RETRO_DEVICE_ID_JOYPAD_Y},
     {DigitalController::Button::Start, RETRO_DEVICE_ID_JOYPAD_START},
     {DigitalController::Button::Select, RETRO_DEVICE_ID_JOYPAD_SELECT},
     {DigitalController::Button::L1, RETRO_DEVICE_ID_JOYPAD_L},
     {DigitalController::Button::L2, RETRO_DEVICE_ID_JOYPAD_L2},
     {DigitalController::Button::R1, RETRO_DEVICE_ID_JOYPAD_R},
     {DigitalController::Button::R2, RETRO_DEVICE_ID_JOYPAD_R2}}};

  if (m_supports_input_bitmasks)
  {
    const u16 active = g_retro_input_state_callback(index, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
    for (const auto& it : mapping)
      controller->SetButtonState(it.first, (active & (static_cast<u16>(1u) << it.second)) != 0u);
  }
  else
  {
    for (const auto& it : mapping)
    {
      const int16_t state = g_retro_input_state_callback(index, RETRO_DEVICE_JOYPAD, 0, it.second);
      controller->SetButtonState(it.first, state != 0);
    }
  }
}

void LibretroHostInterface::UpdateControllersAnalogController(u32 index)
{
  AnalogController* controller = static_cast<AnalogController*>(System::GetController(index));
  DebugAssert(controller);

  static constexpr std::array<std::pair<AnalogController::Button, u32>, 16> button_mapping = {
    {{AnalogController::Button::Left, RETRO_DEVICE_ID_JOYPAD_LEFT},
     {AnalogController::Button::Right, RETRO_DEVICE_ID_JOYPAD_RIGHT},
     {AnalogController::Button::Up, RETRO_DEVICE_ID_JOYPAD_UP},
     {AnalogController::Button::Down, RETRO_DEVICE_ID_JOYPAD_DOWN},
     {AnalogController::Button::Circle, RETRO_DEVICE_ID_JOYPAD_A},
     {AnalogController::Button::Cross, RETRO_DEVICE_ID_JOYPAD_B},
     {AnalogController::Button::Triangle, RETRO_DEVICE_ID_JOYPAD_X},
     {AnalogController::Button::Square, RETRO_DEVICE_ID_JOYPAD_Y},
     {AnalogController::Button::Start, RETRO_DEVICE_ID_JOYPAD_START},
     {AnalogController::Button::Select, RETRO_DEVICE_ID_JOYPAD_SELECT},
     {AnalogController::Button::L1, RETRO_DEVICE_ID_JOYPAD_L},
     {AnalogController::Button::L2, RETRO_DEVICE_ID_JOYPAD_L2},
     {AnalogController::Button::L3, RETRO_DEVICE_ID_JOYPAD_L3},
     {AnalogController::Button::R1, RETRO_DEVICE_ID_JOYPAD_R},
     {AnalogController::Button::R2, RETRO_DEVICE_ID_JOYPAD_R2},
     {AnalogController::Button::R3, RETRO_DEVICE_ID_JOYPAD_R3}}};

  static constexpr std::array<std::pair<AnalogController::Axis, std::pair<u32, u32>>, 4> axis_mapping = {
    {{AnalogController::Axis::LeftX, {RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X}},
     {AnalogController::Axis::LeftY, {RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y}},
     {AnalogController::Axis::RightX, {RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X}},
     {AnalogController::Axis::RightY, {RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y}}}};

  if (m_supports_input_bitmasks)
  {
    const u16 active = g_retro_input_state_callback(index, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
    for (const auto& it : button_mapping)
      controller->SetButtonState(it.first, (active & (static_cast<u16>(1u) << it.second)) != 0u);
  }
  else
  {
    for (const auto& it : button_mapping)
    {
      const int16_t state = g_retro_input_state_callback(index, RETRO_DEVICE_JOYPAD, 0, it.second);
      controller->SetButtonState(it.first, state != 0);
    }
  }

  for (const auto& it : axis_mapping)
  {
    const int16_t state = g_retro_input_state_callback(index, RETRO_DEVICE_ANALOG, it.second.first, it.second.second);
    controller->SetAxisState(static_cast<s32>(it.first), std::clamp(static_cast<float>(state) / 32767.0f, -1.0f, 1.0f));
  }

  if (m_rumble_interface_valid)
  {
    const u16 strong = static_cast<u16>(static_cast<u32>(controller->GetVibrationMotorStrength(0) * 65535.0f));
    const u16 weak = static_cast<u16>(static_cast<u32>(controller->GetVibrationMotorStrength(1) * 65535.0f));
    m_rumble_interface.set_rumble_state(index, RETRO_RUMBLE_STRONG, strong);
    m_rumble_interface.set_rumble_state(index, RETRO_RUMBLE_WEAK, weak);
  }
}

void LibretroHostInterface::UpdateControllersAnalogJoystick(u32 index)
{
  AnalogJoystick* controller = static_cast<AnalogJoystick*>(System::GetController(index));
  DebugAssert(controller);

  static constexpr std::array<std::pair<AnalogJoystick::Button, u32>, 16> button_mapping = {
    {{AnalogJoystick::Button::Left, RETRO_DEVICE_ID_JOYPAD_LEFT},
     {AnalogJoystick::Button::Right, RETRO_DEVICE_ID_JOYPAD_RIGHT},
     {AnalogJoystick::Button::Up, RETRO_DEVICE_ID_JOYPAD_UP},
     {AnalogJoystick::Button::Down, RETRO_DEVICE_ID_JOYPAD_DOWN},
     {AnalogJoystick::Button::Circle, RETRO_DEVICE_ID_JOYPAD_A},
     {AnalogJoystick::Button::Cross, RETRO_DEVICE_ID_JOYPAD_B},
     {AnalogJoystick::Button::Triangle, RETRO_DEVICE_ID_JOYPAD_X},
     {AnalogJoystick::Button::Square, RETRO_DEVICE_ID_JOYPAD_Y},
     {AnalogJoystick::Button::Start, RETRO_DEVICE_ID_JOYPAD_START},
     {AnalogJoystick::Button::Select, RETRO_DEVICE_ID_JOYPAD_SELECT},
     {AnalogJoystick::Button::L1, RETRO_DEVICE_ID_JOYPAD_L},
     {AnalogJoystick::Button::L2, RETRO_DEVICE_ID_JOYPAD_L2},
     {AnalogJoystick::Button::L3, RETRO_DEVICE_ID_JOYPAD_L3},
     {AnalogJoystick::Button::R1, RETRO_DEVICE_ID_JOYPAD_R},
     {AnalogJoystick::Button::R2, RETRO_DEVICE_ID_JOYPAD_R2},
     {AnalogJoystick::Button::R3, RETRO_DEVICE_ID_JOYPAD_R3}}};

  static constexpr std::array<std::pair<AnalogJoystick::Axis, std::pair<u32, u32>>, 4> axis_mapping = {
    {{AnalogJoystick::Axis::LeftX, {RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X}},
     {AnalogJoystick::Axis::LeftY, {RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y}},
     {AnalogJoystick::Axis::RightX, {RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X}},
     {AnalogJoystick::Axis::RightY, {RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y}}}};

  if (m_supports_input_bitmasks)
  {
    const u16 active = g_retro_input_state_callback(index, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
    for (const auto& it : button_mapping)
      controller->SetButtonState(it.first, (active & (static_cast<u16>(1u) << it.second)) != 0u);
  }
  else
  {
    for (const auto& it : button_mapping)
    {
      const int16_t state = g_retro_input_state_callback(index, RETRO_DEVICE_JOYPAD, 0, it.second);
      controller->SetButtonState(it.first, state != 0);
    }
  }

  for (const auto& it : axis_mapping)
  {
    const int16_t state = g_retro_input_state_callback(index, RETRO_DEVICE_ANALOG, it.second.first, it.second.second);
    controller->SetAxisState(static_cast<s32>(it.first), std::clamp(static_cast<float>(state) / 32767.0f, -1.0f, 1.0f));
  }
}

void LibretroHostInterface::UpdateControllersNeGcon(u32 index)
{
  NeGcon* controller = static_cast<NeGcon*>(System::GetController(index));
  DebugAssert(controller);

  static constexpr std::array<std::pair<NeGcon::Button, u32>, 8> button_mapping = {
    {{NeGcon::Button::Left, RETRO_DEVICE_ID_JOYPAD_LEFT},
     {NeGcon::Button::Right, RETRO_DEVICE_ID_JOYPAD_RIGHT},
     {NeGcon::Button::Up, RETRO_DEVICE_ID_JOYPAD_UP},
     {NeGcon::Button::Down, RETRO_DEVICE_ID_JOYPAD_DOWN},
     {NeGcon::Button::A, RETRO_DEVICE_ID_JOYPAD_A},
     {NeGcon::Button::B, RETRO_DEVICE_ID_JOYPAD_X},
     {NeGcon::Button::Start, RETRO_DEVICE_ID_JOYPAD_START},
     {NeGcon::Button::R, RETRO_DEVICE_ID_JOYPAD_R}}};

  static constexpr std::array<std::pair<NeGcon::Axis, std::pair<u32, u32>>, 4> axis_mapping = {
    {{NeGcon::Axis::Steering, {RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X}},
     {NeGcon::Axis::I, {RETRO_DEVICE_INDEX_ANALOG_BUTTON, RETRO_DEVICE_ID_JOYPAD_B}},
     {NeGcon::Axis::II, {RETRO_DEVICE_INDEX_ANALOG_BUTTON, RETRO_DEVICE_ID_JOYPAD_Y}},
     {NeGcon::Axis::L, {RETRO_DEVICE_INDEX_ANALOG_BUTTON, RETRO_DEVICE_ID_JOYPAD_L}}}};

  if (m_supports_input_bitmasks)
  {
    const u16 active = g_retro_input_state_callback(index, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
    for (const auto& it : button_mapping)
      controller->SetButtonState(it.first, (active & (static_cast<u16>(1u) << it.second)) != 0u);
  }
  else
  {
    for (const auto& it : button_mapping)
    {
      const int16_t state = g_retro_input_state_callback(index, RETRO_DEVICE_JOYPAD, 0, it.second);
      controller->SetButtonState(it.first, state != 0);
    }
  }

  for (const auto& it : axis_mapping)
  {
    const int16_t state = g_retro_input_state_callback(index, RETRO_DEVICE_ANALOG, it.second.first, it.second.second);
    controller->SetAxisState(static_cast<s32>(it.first), std::clamp(static_cast<float>(state) / 32767.0f, -1.0f, 1.0f));
  }

}

static std::optional<GPURenderer> RetroHwContextToRenderer(retro_hw_context_type type)
{
  switch (type)
  {
    case RETRO_HW_CONTEXT_OPENGL:
    case RETRO_HW_CONTEXT_OPENGL_CORE:
    case RETRO_HW_CONTEXT_OPENGLES3:
    case RETRO_HW_CONTEXT_OPENGLES_VERSION:
      return GPURenderer::HardwareOpenGL;

    case RETRO_HW_CONTEXT_VULKAN:
      return GPURenderer::HardwareVulkan;

#ifdef WIN32
    case RETRO_HW_CONTEXT_DIRECT3D:
      return GPURenderer::HardwareD3D11;
#endif

    default:
      return std::nullopt;
  }
}

static std::optional<GPURenderer> RenderAPIToRenderer(HostDisplay::RenderAPI api)
{
  switch (api)
  {
    case HostDisplay::RenderAPI::OpenGL:
    case HostDisplay::RenderAPI::OpenGLES:
      return GPURenderer::HardwareOpenGL;

    case HostDisplay::RenderAPI::Vulkan:
      return GPURenderer::HardwareVulkan;

#ifdef WIN32
    case HostDisplay::RenderAPI::D3D11:
      return GPURenderer::HardwareD3D11;
#endif

    default:
      return std::nullopt;
  }
}

bool LibretroHostInterface::RequestHardwareRendererContext()
{
  retro_variable renderer_variable{"duckstation_GPU.Renderer",
                                   Settings::GetRendererName(Settings::DEFAULT_GPU_RENDERER)};
  if (!g_retro_environment_callback(RETRO_ENVIRONMENT_GET_VARIABLE, &renderer_variable) || !renderer_variable.value)
    renderer_variable.value = Settings::GetRendererName(Settings::DEFAULT_GPU_RENDERER);

  GPURenderer renderer = Settings::ParseRendererName(renderer_variable.value).value_or(Settings::DEFAULT_GPU_RENDERER);
  unsigned preferred_renderer = 0;
  g_retro_environment_callback(RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER, &preferred_renderer);
  if (std::strcmp(renderer_variable.value, "Auto") == 0)
  {
    std::optional<GPURenderer> preferred_gpu_renderer =
      RetroHwContextToRenderer(static_cast<retro_hw_context_type>(preferred_renderer));
    if (preferred_gpu_renderer.has_value())
      renderer = preferred_gpu_renderer.value();
  }

  Log_InfoPrintf("Renderer = %s", Settings::GetRendererName(renderer));
  if (renderer == GPURenderer::Software)
  {
    m_hw_render_callback_valid = false;
    return false;
  }

  Log_InfoPrintf("Requesting hardware renderer context for %s", Settings::GetRendererName(renderer));

  m_hw_render_callback = {};
  m_hw_render_callback.context_reset = HardwareRendererContextReset;
  m_hw_render_callback.context_destroy = HardwareRendererContextDestroy;

  switch (renderer)
  {
#ifdef WIN32
    case GPURenderer::HardwareD3D11:
      m_hw_render_callback_valid = LibretroD3D11HostDisplay::RequestHardwareRendererContext(&m_hw_render_callback);
      break;
#endif

    case GPURenderer::HardwareVulkan:
      m_hw_render_callback_valid = LibretroVulkanHostDisplay::RequestHardwareRendererContext(&m_hw_render_callback);
      break;

    case GPURenderer::HardwareOpenGL:
    {
      const bool prefer_gles =
        (preferred_renderer == RETRO_HW_CONTEXT_OPENGLES2 || preferred_renderer == RETRO_HW_CONTEXT_OPENGLES_VERSION);
      m_hw_render_callback_valid =
        LibretroOpenGLHostDisplay::RequestHardwareRendererContext(&m_hw_render_callback, prefer_gles);
    }
    break;

    default:
      Log_ErrorPrintf("Unhandled renderer %s", Settings::GetRendererName(renderer));
      m_hw_render_callback_valid = false;
      break;
  }

  return m_hw_render_callback_valid;
}

void LibretroHostInterface::HardwareRendererContextReset()
{
  Log_InfoPrintf("Hardware context reset, type = %u",
                 static_cast<unsigned>(g_libretro_host_interface.m_hw_render_callback.context_type));

  g_libretro_host_interface.m_hw_render_callback_valid = true;
  g_libretro_host_interface.SwitchToHardwareRenderer();
}

void LibretroHostInterface::SwitchToHardwareRenderer()
{
  struct retro_system_av_info avi;
  g_libretro_host_interface.GetSystemAVInfo(&avi, true);

  WindowInfo wi;
  wi.type = WindowInfo::Type::Libretro;
  wi.display_connection = &g_libretro_host_interface.m_hw_render_callback;
  wi.surface_width = avi.geometry.base_width;
  wi.surface_height = avi.geometry.base_height;
  wi.surface_scale = 1.0f;

  // use the existing device if we just resized the window
  std::optional<GPURenderer> renderer;
  std::unique_ptr<HostDisplay> display = std::move(m_hw_render_display);
  if (display)
  {
    Log_InfoPrintf("Using existing hardware display");
    renderer = RenderAPIToRenderer(display->GetRenderAPI());
    if (!display->ChangeRenderWindow(wi) || !display->CreateResources())
    {
      Log_ErrorPrintf("Failed to recreate resources after reinit");
      display->DestroyRenderDevice();
      display.reset();
    }
  }

  if (!display)
  {
    renderer = RetroHwContextToRenderer(m_hw_render_callback.context_type);
    if (!renderer.has_value())
    {
      Log_ErrorPrintf("Unknown context type %u", static_cast<unsigned>(m_hw_render_callback.context_type));
      return;
    }

    switch (renderer.value())
    {
      case GPURenderer::HardwareOpenGL:
        display = std::make_unique<LibretroOpenGLHostDisplay>();
        break;

      case GPURenderer::HardwareVulkan:
        display = std::make_unique<LibretroVulkanHostDisplay>();
        break;

#ifdef WIN32
      case GPURenderer::HardwareD3D11:
        display = std::make_unique<LibretroD3D11HostDisplay>();
        break;
#endif

      default:
        Log_ErrorPrintf("Unhandled renderer '%s'", Settings::GetRendererName(renderer.value()));
        return;
    }
    if (!display || !display->CreateRenderDevice(wi, {}, g_settings.gpu_use_debug_device, false) ||
        !display->InitializeRenderDevice(GetShaderCacheBasePath(), g_settings.gpu_use_debug_device, false))
    {
      Log_ErrorPrintf("Failed to create hardware host display");
      return;
    }
  }

  std::swap(display, g_libretro_host_interface.m_display);
  System::RecreateGPU(renderer.value());
  display->DestroyRenderDevice();
  m_using_hardware_renderer = true;
}

void LibretroHostInterface::HardwareRendererContextDestroy()
{
  Log_InfoPrintf("Hardware context destroyed");

  // switch back to software
  if (g_libretro_host_interface.m_using_hardware_renderer)
    g_libretro_host_interface.SwitchToSoftwareRenderer();

  if (g_libretro_host_interface.m_hw_render_display)
  {
    g_libretro_host_interface.m_hw_render_display->DestroyRenderDevice();
    g_libretro_host_interface.m_hw_render_display.reset();
  }

  g_libretro_host_interface.m_hw_render_callback_valid = false;
}

void LibretroHostInterface::SwitchToSoftwareRenderer()
{
  Log_InfoPrintf("Switching to software renderer");

  // keep the hw renderer around in case we need it later
  // but keep it active until we've recreated the GPU so we can save the state
  std::unique_ptr<HostDisplay> save_display;
  if (m_using_hardware_renderer)
  {
    save_display = std::move(m_display);
    m_using_hardware_renderer = false;
  }

  m_display = std::make_unique<LibretroHostDisplay>();
  System::RecreateGPU(GPURenderer::Software, false);

  if (save_display)
  {
    save_display->DestroyResources();
    m_hw_render_display = std::move(save_display);
  }
}

bool LibretroHostInterface::DiskControlSetEjectState(bool ejected)
{
  if (System::IsShutdown())
    return false;

  if (ejected)
  {
    if (!System::HasMedia())
      return false;

    System::RemoveMedia();
  }
  else
  {
    if (System::HasMedia())
      return false;

    if (P_THIS->m_disk_control_info.has_sub_images)
    {
      if (!System::InsertMedia(P_THIS->m_disk_control_info.sub_images_parent_path.c_str()))
        return false;

      if (!System::SwitchMediaSubImage(P_THIS->m_disk_control_info.image_index))
        return false;
    }
    else if (!System::InsertMedia(P_THIS->m_disk_control_info.image_paths[P_THIS->m_disk_control_info.image_index].c_str()))
      return false;
  }

  return true;
}

bool LibretroHostInterface::DiskControlGetEjectState()
{
  if (System::IsShutdown())
    return false;

  return !System::HasMedia();
}

unsigned LibretroHostInterface::DiskControlGetImageIndex()
{
  return (unsigned)P_THIS->m_disk_control_info.image_index;
}

bool LibretroHostInterface::DiskControlSetImageIndex(unsigned index)
{
  if (System::IsShutdown() ||
      System::HasMedia() ||
      (index >= P_THIS->m_disk_control_info.image_count))
    return false;

  P_THIS->m_disk_control_info.image_index = (u32)index;
  return true;
}

unsigned LibretroHostInterface::DiskControlGetNumImages()
{
  return (unsigned)P_THIS->m_disk_control_info.image_count;
}

bool LibretroHostInterface::DiskControlReplaceImageIndex(unsigned index, const retro_game_info* info)
{
#ifdef _MSC_VER
#define CASE_COMPARE _stricmp
#else
#define CASE_COMPARE strcasecmp
#endif

  if (System::IsShutdown() ||
      System::HasMedia() ||
      (index >= P_THIS->m_disk_control_info.image_count))
    return false;

  /* Multi-image content cannot be modified */
  if (P_THIS->m_disk_control_info.has_sub_images)
    return false;

  if (!info)
  {
    /* Remove specified image */
    P_THIS->m_disk_control_info.image_count--;

    if (index < P_THIS->m_disk_control_info.image_index)
      P_THIS->m_disk_control_info.image_index--;

    P_THIS->m_disk_control_info.image_paths.erase(
        P_THIS->m_disk_control_info.image_paths.begin() + index);
    P_THIS->m_disk_control_info.image_labels.erase(
        P_THIS->m_disk_control_info.image_labels.begin() + index);
    return true;
  }

  if (!info->path)
    return false;

  const char *extension = std::strrchr(info->path, '.');
  if (!extension)
    return false;

  /* We cannot 'insert' an M3U file
   * > New image must be 'single disk' content */
  if (CASE_COMPARE(extension, ".m3u") == 0)
    return false;

  const std::string_view image_label = FileSystem::GetFileTitleFromPath(info->path);
  if (image_label.empty())
    return false;

  P_THIS->m_disk_control_info.image_paths[index]  = info->path;
  P_THIS->m_disk_control_info.image_labels[index] = std::string(image_label);
  return true;
}

bool LibretroHostInterface::DiskControlAddImageIndex()
{
  if (System::IsShutdown())
    return false;

  /* Multi-image content cannot be modified */
  if (P_THIS->m_disk_control_info.has_sub_images)
    return false;

  P_THIS->m_disk_control_info.image_count++;
  P_THIS->m_disk_control_info.image_paths.push_back("");
  P_THIS->m_disk_control_info.image_labels.push_back("");
  return true;
}

bool LibretroHostInterface::DiskControlSetInitialImage(unsigned index, const char* path)
{
  /* Note: 'path' is ignored, since we cannot
   * determine the actual set path until after
   * content is loaded by the core emulation
   * code (at which point it is too late to
   * compare it with the value supplied here) */
  P_THIS->m_disk_control_info.initial_image_index = index;
  return true;
}

bool LibretroHostInterface::DiskControlGetImagePath(unsigned index, char* path, size_t len)
{
  if ((index >= P_THIS->m_disk_control_info.image_count) ||
      (index >= P_THIS->m_disk_control_info.image_paths.size()) ||
      P_THIS->m_disk_control_info.image_paths[index].empty())
    return false;

  StringUtil::Strlcpy(path, P_THIS->m_disk_control_info.image_paths[index].c_str(), len);
  return true;
}

bool LibretroHostInterface::DiskControlGetImageLabel(unsigned index, char* label, size_t len)
{
  if ((index >= P_THIS->m_disk_control_info.image_count) ||
      (index >= P_THIS->m_disk_control_info.image_labels.size()) ||
      P_THIS->m_disk_control_info.image_labels[index].empty())
    return false;

  StringUtil::Strlcpy(label, P_THIS->m_disk_control_info.image_labels[index].c_str(), len);
  return true;
}

void LibretroHostInterface::InitDiskControlInterface()
{
  unsigned version = 0;
  if (g_retro_environment_callback(RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION, &version) && version >= 1)
  {
    retro_disk_control_ext_callback ext_cb = {
      &LibretroHostInterface::DiskControlSetEjectState, &LibretroHostInterface::DiskControlGetEjectState,
      &LibretroHostInterface::DiskControlGetImageIndex, &LibretroHostInterface::DiskControlSetImageIndex,
      &LibretroHostInterface::DiskControlGetNumImages,  &LibretroHostInterface::DiskControlReplaceImageIndex,
      &LibretroHostInterface::DiskControlAddImageIndex, &LibretroHostInterface::DiskControlSetInitialImage,
      &LibretroHostInterface::DiskControlGetImagePath,  &LibretroHostInterface::DiskControlGetImageLabel};
    if (g_retro_environment_callback(RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE, &ext_cb))
      return;
  }

  retro_disk_control_callback cb = {
    &LibretroHostInterface::DiskControlSetEjectState, &LibretroHostInterface::DiskControlGetEjectState,
    &LibretroHostInterface::DiskControlGetImageIndex, &LibretroHostInterface::DiskControlSetImageIndex,
    &LibretroHostInterface::DiskControlGetNumImages,  &LibretroHostInterface::DiskControlReplaceImageIndex,
    &LibretroHostInterface::DiskControlAddImageIndex};
  if (!g_retro_environment_callback(RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE, &cb))
    Log_WarningPrint("Failed to set disk control interface");
}
