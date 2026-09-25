#include "core/host_interface.h"
#include "common/byte_stream.h"
#include "common/file_system.h"
#include "common/log.h"
#include "common/make_array.h"
#include "common/platform.h"
#include "common/string_util.h"
#include "core/analog_controller.h"
#include "core/analog_joystick.h"
#include "core/bus.h"
#include "core/cheats.h"
#include "core/digital_controller.h"
#include "core/gpu.h"
#include "core/namco_guncon.h"
#include "core/negcon.h"
#include "core/negcon_rumble.h"
#include "core/pad.h"
#include "core/playstation_mouse.h"
#include "core/system.h"
#include "libretro_audio_stream.h"
#include "libretro_game_settings.h"
#include "libretro_host_display.h"
#include "core/gpu_hw_opengl.h"
#include "libretro_settings_interface.h"
#include "core/gpu_hw_vulkan.h"
#include "version.h"
#include <array>
#include <cstring>
#include <tuple>
#include <utility>

#include <compat/strl.h>
#include <file/file_path.h>
#include <streams/file_stream.h>
#include <vfs/vfs_hybrid.h>

Log_SetChannel(HostInterface);

#ifdef WIN32
#include "core/gpu_hw_d3d11.h"
#ifdef USE_D3D12
#include "core/gpu_hw_d3d12.h"
#endif
#endif

RETRO_API unsigned retro_api_version(void)
{
  return RETRO_API_VERSION;
}

RETRO_API void retro_init(void)
{
  g_host_interface_storage.Initialize();
}

RETRO_API void retro_deinit(void)
{
  g_host_interface_storage.Shutdown();
}

RETRO_API void retro_get_system_info(struct retro_system_info* info)
{
  std::memset(info, 0, sizeof(*info));

#if defined(_DEBUGFAST)
  info->library_name = "SwanStation DebugFast";
#elif defined(_DEBUG)
  info->library_name = "SwanStation Debug";
#else
  info->library_name = "SwanStation";
#endif

#ifndef GIT_VERSION
#define GIT_VERSION "undefined"
#endif
  info->library_version = "1.0.0 " GIT_VERSION;
  info->valid_extensions = "exe|psexe|cue|bin|img|iso|chd|pbp|ecm|mds|psf|m3u";
  info->need_fullpath = true;
  info->block_extract = false;
}

RETRO_API void retro_get_system_av_info(struct retro_system_av_info* info)
{
  g_host_interface_storage.retro_get_system_av_info(info);
}

RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device)
{
  g_host_interface_storage.retro_set_controller_port_device(port, device);
  g_host_interface_storage.UpdateCoreOptionsDisplay(true);
}

RETRO_API void retro_reset(void)
{
  g_host_interface_storage.ResetSystem();
}

RETRO_API void retro_run(void)
{
  g_host_interface_storage.retro_run_frame();
}

RETRO_API size_t retro_serialize_size(void)
{
  return g_host_interface_storage.retro_serialize_size();
}

RETRO_API bool retro_serialize(void* data, size_t size)
{
  return g_host_interface_storage.retro_serialize(data, size);
}

RETRO_API bool retro_unserialize(const void* data, size_t size)
{
  return g_host_interface_storage.retro_unserialize(data, size);
}

RETRO_API void retro_cheat_reset(void)
{
  g_host_interface_storage.retro_cheat_reset();
}

RETRO_API void retro_cheat_set(unsigned index, bool enabled, const char* code)
{
  g_host_interface_storage.retro_cheat_set(index, enabled, code);
}

RETRO_API bool retro_load_game(const struct retro_game_info* game)
{
  return g_host_interface_storage.retro_load_game(game);
}

RETRO_API bool retro_load_game_special(unsigned game_type, const struct retro_game_info* info, size_t num_info)
{
  return false;
}

RETRO_API void retro_unload_game(void)
{
  g_host_interface_storage.DestroySystem();
}

RETRO_API unsigned retro_get_region(void)
{
  return g_host_interface_storage.retro_get_region();
}

RETRO_API void* retro_get_memory_data(unsigned id)
{
  return g_host_interface_storage.retro_get_memory_data(id);
}

RETRO_API size_t retro_get_memory_size(unsigned id)
{
  return g_host_interface_storage.retro_get_memory_size(id);
}

RETRO_API void retro_set_environment(retro_environment_t f)
{
  struct retro_log_callback log_cb;
  g_retro_environment_callback = f;
  g_host_interface_storage.retro_set_environment();

  // Local-first VFS dispatch with frontend fallback (VFS API v4,
  // degrading to v3/v1 frontends automatically): local files keep a
  // real fd and stay mappable (RETRO_VFS_FILE_ACCESS_HINT_FREQUENT_ACCESS /
  // filestream_get_mapped_ptr), while URI-shaped paths - and, on
  // sandboxed platforms, local open failures - fall through to the
  // frontend interface.  Replaces the wholesale filestream_vfs_init()
  // pattern, which routed every read through the frontend and made
  // mappings unreachable.  No-op when the frontend has no VFS.
  log_cb.log = NULL;
  g_retro_environment_callback(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log_cb);
  vfs_hybrid_init(g_retro_environment_callback, log_cb.log);
}

RETRO_API void retro_set_video_refresh(retro_video_refresh_t f)
{
  g_retro_video_refresh_callback = f;
}

RETRO_API void retro_set_audio_sample(retro_audio_sample_t f)
{
  g_retro_audio_sample_callback = f;
}

RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t f)
{
  g_retro_audio_sample_batch_callback = f;
}

RETRO_API void retro_set_input_poll(retro_input_poll_t f)
{
  g_retro_input_poll_callback = f;
}

RETRO_API void retro_set_input_state(retro_input_state_t f)
{
  g_retro_input_state_callback = f;
}

HostInterface g_host_interface_storage;
HostInterface* g_host_interface;
#define P_THIS (&g_host_interface_storage)

#define RETRO_DEVICE_PS_CONTROLLER RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 0)
#define RETRO_DEVICE_PS_DUALSHOCK RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 0)
#define RETRO_DEVICE_PS_ANALOG_JOYSTICK RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 1)
#define RETRO_DEVICE_PS_NEGCON RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 2)
#define RETRO_DEVICE_PS_NEGCON_RUMBLE RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 3)
#define RETRO_DEVICE_PS_GUNCON RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_LIGHTGUN, 0)
#define RETRO_DEVICE_PS_MOUSE RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_MOUSE, 0)

retro_environment_t g_retro_environment_callback;
retro_video_refresh_t g_retro_video_refresh_callback;
retro_audio_sample_t g_retro_audio_sample_callback;
retro_audio_sample_batch_t g_retro_audio_sample_batch_callback;
retro_input_poll_t g_retro_input_poll_callback;
retro_input_state_t g_retro_input_state_callback;

bool g_retro_skip_video_this_frame = false;
bool g_retro_skip_audio_this_frame = false;

static retro_log_callback s_libretro_log_callback = {};
static bool s_libretro_log_callback_valid = false;
static bool s_libretro_log_callback_registered = false;
static bool libretro_supports_option_categories = false;
static bool analog_pressed = false;
static bool port_allowed = false;
static unsigned libretro_msg_interface_version = 0;
static int analog_index = -1;

static void LibretroLogCallback(void* pUserParam, const char* channelName, const char* functionName, LogLevel level,
                                const char* message)
{
  static constexpr std::array<retro_log_level, static_cast<std::size_t>(LogLevel::Count)> levels = {
    {RETRO_LOG_ERROR, RETRO_LOG_ERROR, RETRO_LOG_WARN, RETRO_LOG_INFO, RETRO_LOG_INFO, RETRO_LOG_INFO, RETRO_LOG_INFO,
     RETRO_LOG_DEBUG, RETRO_LOG_DEBUG, RETRO_LOG_DEBUG}};

  s_libretro_log_callback.log(levels[static_cast<std::size_t>(level)], "[%s] %s\n",
                              (level <= LogLevel::Perf) ? functionName : channelName, message);
}

static void ResetLibretroLogging()
{
  if (s_libretro_log_callback_registered)
    Log::UnregisterCallback(LibretroLogCallback, nullptr);
  s_libretro_log_callback = {};
  s_libretro_log_callback_registered = false;
  s_libretro_log_callback_valid = false;
}

HostInterface::HostInterface()
{
  g_host_interface = this;
}

HostInterface::~HostInterface()
{
  if (System::IsValid())
  {
    DestroySystem();
  }

  // should be cleaned up by the context destroy, but just in case
  if (m_hw_render_display)
  {
    m_hw_render_display->DestroyRenderDevice();
    m_hw_render_display.reset();
  }

  g_host_interface = nullptr;
}

#include "libretro_core_options.h"

void HostInterface::retro_set_environment()
{
  libretro_supports_option_categories = false;
  libretro_set_core_options(g_retro_environment_callback, &libretro_supports_option_categories);

  // Allow the core to be started with no content, which boots into the BIOS.
  bool support_no_game = true;
  g_retro_environment_callback(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &support_no_game);

  retro_core_options_update_display_callback opts_update_display_cb = {UpdateCoreOptionsDisplayCallback};
  g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK, &opts_update_display_cb);

  static const struct retro_controller_description pads[] = {
      { "Digital Controller (Gamepad)", RETRO_DEVICE_JOYPAD },
      { "Analog Controller (DualShock)", RETRO_DEVICE_PS_DUALSHOCK },
      { "Analog Joystick", RETRO_DEVICE_PS_ANALOG_JOYSTICK },
      { "NeGcon", RETRO_DEVICE_PS_NEGCON },
      { "NeGcon Rumble", RETRO_DEVICE_PS_NEGCON_RUMBLE },
      { "Namco GunCon", RETRO_DEVICE_PS_GUNCON },
      { "PlayStation Mouse", RETRO_DEVICE_PS_MOUSE },
      { NULL, 0 },
  };

  static const struct retro_controller_description pads_mt[] = {
      { "Digital Controller (Gamepad)", RETRO_DEVICE_JOYPAD },
      { "Analog Controller (DualShock)", RETRO_DEVICE_PS_DUALSHOCK },
      { "Analog Joystick", RETRO_DEVICE_PS_ANALOG_JOYSTICK },
      { "NeGcon", RETRO_DEVICE_PS_NEGCON },
      { "NeGcon Rumble", RETRO_DEVICE_PS_NEGCON_RUMBLE },
      { "PlayStation Mouse", RETRO_DEVICE_PS_MOUSE },
      { NULL, 0 },
  };

  static const struct retro_controller_description pads_mt2[] = {
      { "Digital Controller (Gamepad)", RETRO_DEVICE_JOYPAD },
      { "Analog Controller (DualShock)", RETRO_DEVICE_PS_DUALSHOCK },
      { "Analog Joystick", RETRO_DEVICE_PS_ANALOG_JOYSTICK },
      { "PlayStation Mouse", RETRO_DEVICE_PS_MOUSE },
      { NULL, 0 },
  };

  static const struct retro_controller_info ports[] = {
   	  { pads, 8 },
   	  { pads, 8 },
   	  { pads_mt, 7 },
   	  { pads_mt, 7 },
   	  { pads_mt2, 5 },
   	  { pads_mt2, 5 },
   	  { pads_mt2, 5 },
   	  { pads_mt2, 5 },
      { NULL, 0 },
  };

  g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void *)ports);

  InitLogging();
}

void HostInterface::InitInterfaces()
{
  InitRumbleInterface();
  InitDiskControlInterface();

  libretro_msg_interface_version = 0;
  g_retro_environment_callback(RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION, &libretro_msg_interface_version);

  unsigned dummy = 0;
  m_supports_input_bitmasks = g_retro_environment_callback(RETRO_ENVIRONMENT_GET_INPUT_BITMASKS, &dummy);
}

void HostInterface::InitLogging()
{
  ResetLibretroLogging();

  s_libretro_log_callback_valid =
    g_retro_environment_callback(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &s_libretro_log_callback) &&
    s_libretro_log_callback.log;

  if (s_libretro_log_callback_valid)
  {
    Log::RegisterCallback(LibretroLogCallback, nullptr);
    s_libretro_log_callback_registered = true;
  }
}

bool HostInterface::Initialize()
{
  /* Reset disk control info struct */
  P_THIS->m_disk_control_info.has_sub_images      = false;
  P_THIS->m_disk_control_info.initial_image_index = 0;
  P_THIS->m_disk_control_info.image_index         = 0;
  P_THIS->m_disk_control_info.image_count         = 0;
  P_THIS->m_disk_control_info.sub_images_parent_path.clear();
  P_THIS->m_disk_control_info.image_paths.clear();
  P_THIS->m_disk_control_info.image_labels.clear();

  InitLogging();
  InitInterfaces();
  LoadSettings();
  FixIncompatibleSettings(true);
  UpdateLogging();

  return true;
}

void HostInterface::Shutdown()
{
  libretro_supports_option_categories = false;

  // Used to delegate to a base-class HostInterface::Shutdown that did
  // exactly this; folded in here after the inheritance was removed.
  if (!System::IsShutdown())
    System::Shutdown();

  /* Reset disk control info struct */
  P_THIS->m_disk_control_info.has_sub_images      = false;
  P_THIS->m_disk_control_info.initial_image_index = 0;
  P_THIS->m_disk_control_info.image_index         = 0;
  P_THIS->m_disk_control_info.image_count         = 0;
  P_THIS->m_disk_control_info.sub_images_parent_path.clear();
  P_THIS->m_disk_control_info.image_paths.clear();
  P_THIS->m_disk_control_info.image_labels.clear();

  ResetLibretroLogging();
}

void HostInterface::ReportError(const char* message)
{
  AddFormattedOSDMessage(10.0f, "ERROR: %s", message);
  Log_ErrorPrint(message);
}

void HostInterface::ReportMessage(const char* message)
{
  AddOSDMessage(message, 5.0f);
  Log_InfoPrint(message);
}

bool HostInterface::ConfirmMessage(const char* message)
{
  Log_InfoPrintf("Confirm: %s", message);
  return false;
}

void HostInterface::GetGameInfo(const char* path, CDImage* image, std::string* code, std::string* title)
{
  // Just use the filename for now... we don't have the game list. Unless we can pull this from the frontend somehow?
  *title = FileSystem::GetFileTitleFromPath(path);

  if (image)
    *code = System::GetGameCodeForImage(image, true);
  else
    code->clear();
}

static const char* GetSaveDirectory()
{
  const char* save_directory = nullptr;
  if (!g_retro_environment_callback(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &save_directory) || !save_directory)
    save_directory = "saves";

  return save_directory;
}

std::string HostInterface::GetSharedMemoryCardPath(uint32_t slot) const
{
  return StringUtil::StdStringFromFormat("%s" FS_OSPATH_SEPARATOR_STR "duckstation_shared_card_%d.mcd",
                                         GetSaveDirectory(), slot + 1);
}

std::string HostInterface::GetGameMemoryCardPath(const char* game_code, uint32_t slot) const
{
  return StringUtil::StdStringFromFormat("%s" FS_OSPATH_SEPARATOR_STR "%s_%d.mcd", GetSaveDirectory(), game_code,
                                         slot + 1);
}

std::string HostInterface::GetShaderCacheBasePath() const
{
  // Use the system directory, and failing that, the downloads directory.
  const char* cache_directory_ptr = nullptr;
  if (!g_retro_environment_callback(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &cache_directory_ptr) || !cache_directory_ptr)
  {
    cache_directory_ptr = nullptr;
    if (!g_retro_environment_callback(RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY, &cache_directory_ptr) ||
        !cache_directory_ptr)
    {
      Log_WarningPrint("No shader cache directory available, startup will be slower.");
      return std::string();
    }
  }

  // Use a directory named "swanstation" in the system/downloads directory.
  std::string shader_cache_path = StringUtil::StdStringFromFormat(
    "%s" FS_OSPATH_SEPARATOR_STR "swanstation" FS_OSPATH_SEPARATOR_STR, cache_directory_ptr);
  if (   !path_is_directory(shader_cache_path.c_str())
      && !path_mkdir(shader_cache_path.c_str()))
  {
    Log_ErrorPrintf("Failed to create shader cache directory: '%s'", shader_cache_path.c_str());
    return std::string();
  }

  Log_InfoPrintf("Shader cache directory: '%s'", shader_cache_path.c_str());
  return shader_cache_path;
}

std::string HostInterface::GetStringSettingValue(const char* section, const char* key,
                                                         const char* default_value /*= ""*/)
{
  TinyString name;
  name.Format("swanstation_%s_%s", section, key);
  retro_variable var{name, default_value};
  if (g_retro_environment_callback(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    return var.value;
  return default_value;
}

void HostInterface::DisplayLoadingScreen(const char* message, int progress_min /*= -1*/,
		                                 int progress_max /*= -1*/, int progress_value /*= -1*/)
{
  // Render progress feedback through the libretro frontend so that
  // long-running setup steps (shader precompilation, texture preload)
  // don't look like the core has hung.
  //
  // The DuckStation HostInterface contract is documented at the
  // top of host_interface.h: this is called repeatedly during
  // bounded long operations with (current, total) progress counters,
  // and is throttled by the caller to ~10 Hz. Callers in
  // swanstation are:
  //
  //   - GPU_HW::ShaderCompileProgressTracker::Increment (every
  //     compiled shader, throttled to >=100ms)
  //   - texture_replacements.cpp preload loop (every replacement
  //     texture)
  //   - HostInterfaceProgressCallback (legacy generic progress)
  //
  // For the libretro target, the cleanest mapping is
  // RETRO_ENVIRONMENT_SET_MESSAGE_EXT with type =
  // RETRO_MESSAGE_TYPE_PROGRESS, which RetroArch renders as a
  // dedicated progress widget in the upper-right corner rather
  // than a generic OSD line. When the frontend's message interface
  // version is too old to support EXT (libretro_msg_interface_version
  // == 0, i.e. cores running on RA builds pre-2020 or other
  // frontends that haven't adopted v1), fall back to plain
  // SET_MESSAGE with the formatted progress text in the message
  // body, so the user still sees "Compiling Shaders 47/112"
  // instead of a frozen window.
  if (!message || !*message)
    return;

  // Compute progress percentage. The DisplayLoadingScreen contract
  // passes progress_min as the starting count (typically 0) and
  // progress_max as the total; -1 across the trio means
  // 'indeterminate', which RetroArch represents with progress = -1
  // on SET_MESSAGE_EXT.
  int progress_pct = -1;
  if (progress_max > 0 && progress_value >= 0 && progress_value <= progress_max)
  {
    progress_pct = static_cast<int>(
      (static_cast<long>(progress_value) * 100l) / static_cast<long>(progress_max));
    if (progress_pct < 0)
      progress_pct = 0;
    else if (progress_pct > 100)
      progress_pct = 100;
  }

  // Build the displayed string. Including the percentage in the
  // text body matters even when SET_MESSAGE_EXT is available,
  // because the libretro spec explicitly says progress is a hint
  // - some frontends (and RetroArch with certain notification
  // styles) drop the progress widget and only show the text.
  char text[256];
  if (progress_max > 0 && progress_value >= 0)
  {
    std::snprintf(text, sizeof(text), "%s %d/%d", message, progress_value, progress_max);
  }
  else
  {
    std::snprintf(text, sizeof(text), "%s", message);
  }
  text[sizeof(text) - 1] = '\0';

  if (libretro_msg_interface_version >= 1)
  {
    retro_message_ext msg = {};
    msg.msg = text;
    // 1000 ms duration with ~10 Hz update cadence means each frame
    // of the progress is overwritten by the next one well before
    // it expires, and the widget stays visible smoothly. The last
    // call (after CompileShaders returns) ages out naturally after
    // one second.
    msg.duration = 1000u;
    // Priority 3 matches what the libretro header documents as
    // 'frontend-generated notifications' tier; shader compilation
    // is roughly that level of importance and we don't want to
    // outrank game-relevant messages with lower priority later.
    msg.priority = 3u;
    msg.level = RETRO_LOG_INFO;
    msg.target = RETRO_MESSAGE_TARGET_OSD;
    msg.type = RETRO_MESSAGE_TYPE_PROGRESS;
    msg.progress = static_cast<int8_t>(progress_pct);
    g_retro_environment_callback(RETRO_ENVIRONMENT_SET_MESSAGE_EXT, &msg);
    return;
  }

  // Fallback for frontends without SET_MESSAGE_EXT. The text
  // already contains 'N/M', so the user gets the same information
  // (minus the progress widget).
  retro_message legacy = {};
  legacy.msg = text;
  // SET_MESSAGE measures duration in frames; assume the
  // ~60 fps NTSC refresh rate if the system isn't running yet (we're
  // most often called from CompileShaders during boot, before
  // System::GetVerticalFrequency() can return a real value).
  legacy.frames = 60u;
  g_retro_environment_callback(RETRO_ENVIRONMENT_SET_MESSAGE, &legacy);
}


void HostInterface::AddOSDMessage(std::string message, float duration /*= 2.0f*/)
{
  if (!g_settings.display_show_osd_messages)
    return;

  retro_message msg = {};
  msg.msg = message.c_str();
  msg.frames = static_cast<uint32_t>(duration * (System::IsShutdown() ? 60.0f : System::GetVerticalFrequency()));
  g_retro_environment_callback(RETRO_ENVIRONMENT_SET_MESSAGE, &msg);
}

void HostInterface::retro_get_system_av_info(struct retro_system_av_info* info)
{
  const bool use_resolution_scale = (g_settings.gpu_renderer != GPURenderer::Software);
  GetSystemAVInfo(info, use_resolution_scale);
  m_last_aspect_ratio        = info->geometry.aspect_ratio;
  m_last_vertical_frequency  = static_cast<float>(info->timing.fps);
}

void HostInterface::GetSystemAVInfo(struct retro_system_av_info* info, bool use_resolution_scale)
{
  // base reflects the current output texture size, which CAN change
  // with downsample mode (Box pre-scales internal rendering down to
  // 1x so the frontend texture is base_width = display * 1, while
  // Disabled / Adaptive keep the upscaled output at display *
  // gpu_resolution_scale). base changes are picked up by
  // UpdateGeometry via RETRO_ENVIRONMENT_SET_GEOMETRY which the
  // libretro spec guarantees will NOT reinitialise drivers.
  //
  // max reflects the upper bound on the texture handed to the
  // frontend across the entire session. Critically, max is keyed
  // ONLY off gpu_resolution_scale, not the downsample-aware
  // GetResolutionScale() helper. Box mode could in principle hand
  // back a smaller texture (VRAM_WIDTH * 1) than max
  // (VRAM_WIDTH * gpu_resolution_scale), and that is perfectly
  // legal libretro behaviour - the frontend allocates the larger
  // backing texture, the core fills only the lower-left
  // base_width x base_height region. Locking max to
  // gpu_resolution_scale means toggling downsample mode does NOT
  // change max and therefore does NOT require
  // RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO (which forces a libretro
  // hw context reinit and a multi-second freeze). Only an actual
  // gpu_resolution_scale change needs SET_SYSTEM_AV_INFO; downsample
  // mode changes route through SET_GEOMETRY instead.
  const uint32_t base_scale = use_resolution_scale ? GetResolutionScale() : 1u;
  const uint32_t max_scale = use_resolution_scale ? std::max<uint32_t>(g_settings.gpu_resolution_scale, 1u) : 1u;

  std::memset(info, 0, sizeof(*info));

  info->geometry.base_width = (m_display ? m_display->GetDisplayWidth() : GPU_MAX_DISPLAY_WIDTH) * base_scale;
  info->geometry.base_height = (m_display ? m_display->GetDisplayHeight() : GPU_MAX_DISPLAY_HEIGHT) * base_scale;
  info->geometry.aspect_ratio = (m_display ? m_display->GetDisplayAspectRatio() : (g_gpu ? g_gpu->GetDisplayAspectRatio() : g_settings.GetDisplayAspectRatioValue()));
  info->geometry.max_width = VRAM_WIDTH * max_scale;
  info->geometry.max_height = VRAM_HEIGHT * max_scale;

  info->timing.fps = (System::IsValid()) ? System::GetVerticalFrequency() : 60.0;
  info->timing.sample_rate = static_cast<double>(LibretroAudioStream::SAMPLE_RATE);
}

bool HostInterface::UpdateSystemAVInfo(bool use_resolution_scale)
{
  struct retro_system_av_info avi;
  GetSystemAVInfo(&avi, use_resolution_scale);
  if (!g_retro_environment_callback(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &avi))
    return false;

  m_display->ResizeRenderWindow(avi.geometry.base_width, avi.geometry.base_height);
  m_last_aspect_ratio        = avi.geometry.aspect_ratio;
  m_last_vertical_frequency  = static_cast<float>(avi.timing.fps);
  return true;
}

void HostInterface::UpdateGeometry()
{
  struct retro_system_av_info avi;
  const bool use_resolution_scale = (g_settings.gpu_renderer != GPURenderer::Software);
  GetSystemAVInfo(&avi, use_resolution_scale);

  g_retro_environment_callback(RETRO_ENVIRONMENT_SET_GEOMETRY, &avi.geometry);

  m_display->ResizeRenderWindow(avi.geometry.base_width, avi.geometry.base_height);
  m_last_aspect_ratio = avi.geometry.aspect_ratio;
}

void HostInterface::UpdateLogging()
{
  Log::SetFilterLevel(g_settings.log_level);
}

bool HostInterface::UpdateGameSettings()
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

void HostInterface::ApplyGameSettings()
{
  if (!g_settings.apply_game_settings || !m_game_settings)
    return;

  m_game_settings->ApplySettings(System::GetState() == System::State::Starting);
}

bool HostInterface::retro_load_game(const struct retro_game_info* game)
{
  std::shared_ptr<SystemBootParameters> bp = std::make_shared<SystemBootParameters>();
  // A null game (or null path) means the frontend started the core with no
  // content; boot into the BIOS by leaving the filename empty. System::Boot
  // already handles an empty filename as a BIOS boot.
  if (game && game->path)
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

      {},
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
      {
        DestroySystem();
        return false;
      }

      P_THIS->m_disk_control_info.has_sub_images         = true;
      P_THIS->m_disk_control_info.image_index            = System::GetMediaSubImageIndex();
      P_THIS->m_disk_control_info.image_count            = System::GetMediaSubImageCount();
      P_THIS->m_disk_control_info.sub_images_parent_path = parent_path;

      for (uint32_t i = 0; i < P_THIS->m_disk_control_info.image_count; i++)
      {
        const std::string& sub_image_path = System::GetMediaSubImagePath(i);
        if (sub_image_path.empty())
        {
          DestroySystem();
          return false;
        }

        const std::string& sub_image_label = System::GetMediaSubImageTitle(i);
        if (sub_image_label.empty())
        {
          DestroySystem();
          return false;
        }

        P_THIS->m_disk_control_info.image_paths.push_back(sub_image_path);
        P_THIS->m_disk_control_info.image_labels.push_back(sub_image_label);
      }
    }
    else
    {
      const std::string& image_path = System::GetMediaFileName();
      if (image_path.empty())
      {
        DestroySystem();
        return false;
      }

      const std::string_view image_label = FileSystem::GetFileTitleFromPath(image_path);
      if (image_label.empty())
      {
        DestroySystem();
        return false;
      }

      P_THIS->m_disk_control_info.has_sub_images = false;
      P_THIS->m_disk_control_info.image_index    = 0;
      P_THIS->m_disk_control_info.image_count    = 1;
      P_THIS->m_disk_control_info.sub_images_parent_path.clear();

      P_THIS->m_disk_control_info.image_paths.push_back(image_path);
      P_THIS->m_disk_control_info.image_labels.push_back(std::string(image_label));
    }
  }

  struct retro_core_option_display option_display;
  option_display.visible = false;
  switch (System::GetRegion())
  {
      case  ConsoleRegion::NTSC_J:
      {
         option_display.key = "swanstation_BIOS_PathNTSCU";
         g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
         option_display.key = "swanstation_BIOS_PathPAL";
         g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);

         break;
      }

      case  ConsoleRegion::NTSC_U:
      {
         option_display.key = "swanstation_BIOS_PathNTSCJ";
         g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
         option_display.key = "swanstation_BIOS_PathPAL";
         g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);

         break;
      }

      case  ConsoleRegion::PAL:
      {
         option_display.key = "swanstation_BIOS_PathNTSCU";
         g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
         option_display.key = "swanstation_BIOS_PathNTSCJ";
         g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);

         break;
      }
  }

  return true;
}

void HostInterface::retro_set_controller_port_device(uint32_t port, uint32_t device)
{
  if (retropad_device[port] != device)
  {
    controller_dirty = true;
    retropad_device[port] = device;
  }
}

void HostInterface::retro_run_frame()
{
  // Refresh the per-frame A/V skip flags. If the frontend is doing
  // single-instance runahead (or any equivalent skip-frame mechanism) it
  // calls retro_run with AV bits cleared; we still simulate the frame but
  // suppress audio output and video output to the frontend for that frame.
  // Failure or unsupported callback => assume both enabled (the spec says
  // "the core should assume that the frontend will not skip any steps").
  {
    int flags = 0;
    if (g_retro_environment_callback(RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE, &flags))
    {
      g_retro_skip_video_this_frame = (flags & RETRO_AV_ENABLE_VIDEO) == 0;
      g_retro_skip_audio_this_frame = (flags & RETRO_AV_ENABLE_AUDIO) == 0;
    }
    else
    {
      g_retro_skip_video_this_frame = false;
      g_retro_skip_audio_this_frame = false;
    }
  }

  if (HasCoreVariablesChanged() || controller_dirty)
  {
    controller_dirty = false;
    UpdateSettings();
  }

  UpdateControllers();

  System::RunFrame();

  // Detect post-frame timing changes from the PSX side. The CRTC config
  // can change mid-game (NTSC <-> PAL detection, interlace toggle,
  // overclock change), and that updates both the aspect ratio and the
  // vertical refresh that the SPU/audio resampling are pegged to.
  //
  // Aspect-only changes go through SET_GEOMETRY (cheap, no pipeline
  // teardown). A change in vertical refresh requires SET_SYSTEM_AV_INFO
  // because SET_GEOMETRY does not carry the timing struct - if we
  // don't re-issue full av_info, the frontend keeps resampling audio
  // at the stale fps ratio and the audio drifts.
  const float aspect_ratio        = m_display->GetDisplayAspectRatio();
  const float vertical_frequency  = (System::IsValid()) ? System::GetVerticalFrequency() : m_last_vertical_frequency;

  if (vertical_frequency != m_last_vertical_frequency)
  {
    UpdateSystemAVInfo(g_settings.gpu_renderer != GPURenderer::Software);
  }
  else if (aspect_ratio != m_last_aspect_ratio)
  {
    UpdateGeometry();
  }

  m_display->Render();

  if (g_settings.audio_fast_hook)
    m_audio_stream->UploadToFrontend();
}

unsigned HostInterface::retro_get_region()
{
  return System::IsPALRegion() ? RETRO_REGION_PAL : RETRO_REGION_NTSC;
}

size_t HostInterface::retro_serialize_size()
{
  return System::MAX_SAVE_STATE_SIZE;
}

bool HostInterface::retro_serialize(void* data, size_t size)
{
  std::unique_ptr<ByteStream> stream = ByteStream_CreateMemoryStream(data, static_cast<uint32_t>(size));
  return System::SaveState(stream.get());
}

bool HostInterface::retro_unserialize(const void* data, size_t size)
{
  // Ask the frontend whether this load is for runahead / rewind / netplay
  // rollback or a normal disk load. The runahead flavours guarantee the
  // state was produced by the same binary in the same address space, so
  // we can take the cheap "memory state" path which marks JIT blocks
  // invalidated rather than throwing the entire CPU code cache away and
  // recompiling the dispatcher from scratch (System::DoState's
  // is_memory_state argument). The cache stays warm across the runahead
  // window, which is what makes per-frame state reload affordable.
  retro_savestate_context ctx = RETRO_SAVESTATE_CONTEXT_NORMAL;
  bool is_memory_state = false;
  if (g_retro_environment_callback(RETRO_ENVIRONMENT_GET_SAVESTATE_CONTEXT, &ctx))
  {
    is_memory_state = (ctx == RETRO_SAVESTATE_CONTEXT_RUNAHEAD_SAME_INSTANCE ||
                       ctx == RETRO_SAVESTATE_CONTEXT_RUNAHEAD_SAME_BINARY);
  }

  std::unique_ptr<ByteStream> stream = ByteStream_CreateReadOnlyMemoryStream(data, static_cast<uint32_t>(size));
  return System::LoadState(stream.get(), is_memory_state);
}

void* HostInterface::retro_get_memory_data(unsigned id)
{
  switch (id)
  {
    case RETRO_MEMORY_SYSTEM_RAM:
      if (!System::IsShutdown())
        return Bus::g_ram;
      break;
    case RETRO_MEMORY_SAVE_RAM:
    {
      const MemoryCardType type = g_settings.memory_card_types[0];
      if (System::IsShutdown()  || type != MemoryCardType::Libretro)
        break;
      MemoryCard* const card               = g_pad.GetMemoryCard(0);
      MemoryCardImage::DataArray& data     = card->GetData();
      return data.data();
    }

    default:
      break;
  }

  return nullptr;
}

size_t HostInterface::retro_get_memory_size(unsigned id)
{
  switch (id)
  {
    case RETRO_MEMORY_SYSTEM_RAM:
      return Bus::g_ram_size;

    case RETRO_MEMORY_SAVE_RAM:
    {
      const MemoryCardType type = g_settings.memory_card_types[0];
      if (System::IsShutdown()  || type != MemoryCardType::Libretro)
        break;
      return 128 * 1024;
    }
    default:
      break;
  }
  return 0;
}

void HostInterface::retro_cheat_reset()
{
  System::SetCheatList(nullptr);
}

void HostInterface::retro_cheat_set(unsigned index, bool enabled, const char* code)
{
  CheatList* cl = System::GetCheatList();
  if (!cl)
  {
    System::SetCheatList(std::make_unique<CheatList>());
    cl = System::GetCheatList();
  }

  CheatCode cc;
  cc.description = StringUtil::StdStringFromFormat("Cheat%u", index);
  cc.enabled     = true;
  if (!CheatList::ParseLibretroCheat(&cc, code))
    Log_ErrorPrintf("Failed to parse cheat %u '%s'", index, code);

  cl->SetCode(index, std::move(cc));
}

void HostInterface::AcquireHostDisplay()
{
  WindowInfo wi;
  // start in software mode, switch to hardware later
  struct retro_system_av_info avi;
  g_host_interface_storage.GetSystemAVInfo(&avi, false);

  wi.surface_width  = avi.geometry.base_width;
  wi.surface_height = avi.geometry.base_height;

  m_display         = std::make_unique<LibretroHostDisplay>();
  m_display->CreateRenderDevice(wi, {}, false, false);
  m_display->InitializeRenderDevice({}, false, false);
}

void HostInterface::ReleaseHostDisplay()
{
  if (m_hw_render_display)
  {
    m_hw_render_display->DestroyRenderDevice();
    m_hw_render_display.reset();
  }

  m_display->DestroyRenderDevice();
  m_display.reset();
}

void HostInterface::OnControllerTypeChanged(uint32_t slot) {}

bool HostInterface::UpdateCoreOptionsDisplay(bool controller)
{
  LibretroSettingsInterface si;

  static CPUExecutionMode cpu_execution_mode_prev;
  static CPUFastmemMode cpu_fastmem_mode_prev;
  static bool hardware_renderer_prev;
  static bool pgxp_enable_prev;
  static bool support_perspective_prev;
  static MultitapMode multitap_mode_prev;
  static bool vram_rewrite_replacements_prev;
  static bool cdrom_preload_enable_prev;
  static DisplayAspectRatio aspect_ratio_prev;
  static bool pgxp_depth_buffer_enable_prev;

  const CPUExecutionMode cpu_execution_mode =
    Settings::ParseCPUExecutionMode(
      si.GetStringValue("CPU", "ExecutionMode", Settings::GetCPUExecutionModeName(Settings::DEFAULT_CPU_EXECUTION_MODE)).c_str())
      .value_or(Settings::DEFAULT_CPU_EXECUTION_MODE);
  const CPUFastmemMode cpu_fastmem_mode =
    Settings::ParseCPUFastmemMode(
      si.GetStringValue("CPU", "FastmemMode", Settings::GetCPUFastmemModeName(Settings::DEFAULT_CPU_FASTMEM_MODE)).c_str())
      .value_or(Settings::DEFAULT_CPU_FASTMEM_MODE);
  const bool cpu_recompiler = (cpu_execution_mode == CPUExecutionMode::Recompiler);
  const bool cpu_fastmem_rewrite = (cpu_recompiler && cpu_fastmem_mode == CPUFastmemMode::MMap);

  const GPURenderer gpu_renderer =
    Settings::ParseRendererName(
      si.GetStringValue("GPU", "Renderer", Settings::GetRendererName(Settings::DEFAULT_GPU_RENDERER)).c_str())
      .value_or(Settings::DEFAULT_GPU_RENDERER);
  const bool hardware_renderer = (gpu_renderer != GPURenderer::Software);
  const bool pgxp_enable = (hardware_renderer && si.GetBoolValue("GPU", "PGXPEnable", false));
  const bool support_perspective = (pgxp_enable && gpu_renderer != GPURenderer::HardwareOpenGL);

  const MultitapMode multitap_mode =
    Settings::ParseMultitapModeName(si.GetStringValue("ControllerPorts", "MultitapMode", Settings::GetMultitapModeName(Settings::DEFAULT_MULTITAP_MODE)).c_str())
      .value_or(Settings::DEFAULT_MULTITAP_MODE);

  const bool vram_rewrite_replacements = (hardware_renderer && si.GetBoolValue("TextureReplacements", "EnableVRAMWriteReplacements", false));
  const bool cdrom_preload_enable = si.GetBoolValue("CDROM", "LoadImageToRAM", false);

  const DisplayAspectRatio aspect_ratio =
    Settings::ParseDisplayAspectRatio(
      si.GetStringValue("Display", "AspectRatio", Settings::GetDisplayAspectRatioName(Settings::DEFAULT_DISPLAY_ASPECT_RATIO)).c_str())
      .value_or(Settings::DEFAULT_DISPLAY_ASPECT_RATIO);
  const bool custom_aspect_ratio = (aspect_ratio == DisplayAspectRatio::Custom);

  const bool pgxp_depth_buffer_enable = (pgxp_enable && si.GetBoolValue("GPU", "PGXPDepthBuffer", false));

  if (!controller)
  {
    if (cpu_execution_mode == cpu_execution_mode_prev && cpu_fastmem_mode == cpu_fastmem_mode_prev && hardware_renderer == hardware_renderer_prev &&
        pgxp_enable == pgxp_enable_prev && support_perspective == support_perspective_prev && multitap_mode == multitap_mode_prev &&
        vram_rewrite_replacements == vram_rewrite_replacements_prev && cdrom_preload_enable == cdrom_preload_enable_prev &&
        aspect_ratio == aspect_ratio_prev && pgxp_depth_buffer_enable == pgxp_depth_buffer_enable_prev
        )
    {
      return false;
    }
  }

  cpu_execution_mode_prev = cpu_execution_mode;
  cpu_fastmem_mode_prev = cpu_fastmem_mode;
  hardware_renderer_prev = hardware_renderer;
  pgxp_enable_prev = pgxp_enable;
  support_perspective_prev = support_perspective;
  multitap_mode_prev = multitap_mode;
  vram_rewrite_replacements_prev = vram_rewrite_replacements;
  cdrom_preload_enable_prev = cdrom_preload_enable;
  aspect_ratio_prev = aspect_ratio;
  pgxp_depth_buffer_enable_prev = pgxp_depth_buffer_enable;

  struct retro_core_option_display option_display;

  option_display.visible = cpu_recompiler;
  option_display.key = "swanstation_CPU_RecompilerICache";
  g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
  option_display.key = "swanstation_CPU_RecompilerBlockLinking";
  g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
  option_display.key = "swanstation_CPU_FastmemMode";
  g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);

  option_display.visible = cpu_fastmem_rewrite;
  option_display.key = "swanstation_CPU_FastmemRewrite";
  g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);

  option_display.visible = hardware_renderer;
  option_display.key = "swanstation_GPU_UseSoftwareRendererForReadbacks";
  g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
  option_display.key = "swanstation_GPU_MSAA";
  g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
  option_display.key = "swanstation_GPU_TrueColor";
  g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
  option_display.key = "swanstation_GPU_ScaledDithering";
  g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
  option_display.key = "swanstation_GPU_ChromaSmoothing24Bit";
  g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
  option_display.key = "swanstation_GPU_TextureFilter";
  g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
  option_display.key = "swanstation_GPU_DownsampleMode";
  g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
  option_display.key = "swanstation_GPU_ResolutionScale";
  g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
  option_display.key = "swanstation_TextureReplacements_EnableVRAMWriteReplacements";
  g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
  option_display.key = "swanstation_GPU_PGXPEnable";
  g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);

  option_display.visible = !hardware_renderer;
  option_display.key = "swanstation_GPU_UseThread";
  g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);

  option_display.visible = pgxp_enable;
  option_display.key = "swanstation_GPU_PGXPCulling";
  g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
  option_display.key = "swanstation_GPU_PGXPTextureCorrection";
  g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
  option_display.key = "swanstation_GPU_PGXPDepthBuffer";
  g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
  option_display.key = "swanstation_GPU_PGXPVertexCache";
  g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
  option_display.key = "swanstation_GPU_PGXPCPU";
  g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
  option_display.key = "swanstation_GPU_PGXPPreserveProjFP";
  g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
  option_display.key = "swanstation_GPU_PGXPTolerance";
  g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);

  option_display.visible = support_perspective;
  option_display.key = "swanstation_GPU_PGXPColorCorrection";
  g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);

  option_display.visible = vram_rewrite_replacements;
  option_display.key = "swanstation_TextureReplacements_PreloadTextures";
  g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);

  option_display.visible = !cdrom_preload_enable;
  option_display.key = "swanstation_CDROM_PreCacheCHD";
  g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);

  option_display.visible = pgxp_depth_buffer_enable;
  option_display.key = "swanstation_GPU_PGXPDepthClearThreshold";
  g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);

  for (uint32_t i = 0; i < NUM_CONTROLLER_AND_CARD_PORTS; i++)
  {
    if (multitap_mode == MultitapMode::Port1Only || multitap_mode == MultitapMode::Port2Only)
      port_allowed = (i < 5);
    else if (multitap_mode == MultitapMode::BothPorts)
      port_allowed = true;
    else
      port_allowed = (i < 2);

    const uint32_t active_controller = retropad_device[i];
    const bool analog_active = (port_allowed && (active_controller == RETRO_DEVICE_PS_DUALSHOCK || active_controller == RETRO_DEVICE_PS_ANALOG_JOYSTICK ||
                                active_controller == RETRO_DEVICE_PS_NEGCON || active_controller == RETRO_DEVICE_PS_NEGCON_RUMBLE));
    const bool dualshock_active = (port_allowed && active_controller == RETRO_DEVICE_PS_DUALSHOCK);
    const bool negcon_active = (port_allowed && (active_controller == RETRO_DEVICE_PS_NEGCON || active_controller == RETRO_DEVICE_PS_NEGCON_RUMBLE));
    const bool guncon_active = (port_allowed && active_controller == RETRO_DEVICE_PS_GUNCON);

    option_display.visible = analog_active;
    option_display.key = (TinyString::FromFormat("swanstation_Controller%u_AxisScale", (i + 1)));
    g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
    option_display.key = (TinyString::FromFormat("swanstation_Controller%u_VibrationBias", (i + 1)));
    g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);

    option_display.visible = dualshock_active;
    option_display.key = (TinyString::FromFormat("swanstation_Controller%u_AnalogDPadInDigitalMode", (i + 1)));
    g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
    option_display.key = (TinyString::FromFormat("swanstation_Controller%u_ForceAnalog", (i + 1)));
    g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);

    option_display.visible = negcon_active;
    option_display.key = (TinyString::FromFormat("swanstation_Controller%u_SteeringDeadzone", (i + 1)));
    g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
    option_display.key = (TinyString::FromFormat("swanstation_Controller%u_TwistResponse", (i + 1)));
    g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);

    option_display.visible = guncon_active;
    option_display.key = (TinyString::FromFormat("swanstation_Controller%u_XScale", (i + 1)));
    g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
    option_display.key = (TinyString::FromFormat("swanstation_Controller%u_YScale", (i + 1)));
    g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
  }

  const bool guncon_aspect = (retropad_device[0] == RETRO_DEVICE_PS_GUNCON || retropad_device[1] == RETRO_DEVICE_PS_GUNCON);
  const bool show_custom_ar = (!guncon_aspect && custom_aspect_ratio);

  option_display.visible = guncon_aspect;
  option_display.key = "swanstation_Controller_ShowCrosshair";
  g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);

  option_display.visible = !guncon_aspect;
  option_display.key = "swanstation_Display_AspectRatio";
  g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);

  option_display.visible = show_custom_ar;
  option_display.key = "swanstation_Display_CustomAspectRatioNumerator";
  g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
  option_display.key = "swanstation_Display_CustomAspectRatioDenominator";
  g_retro_environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);

  return true;
}
bool HostInterface::HasCoreVariablesChanged()
{
  bool changed = false;
  return (g_retro_environment_callback(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &changed) && changed);
}

std::string HostInterface::GetBIOSDirectory()
{
  // Assume BIOS files are located in system directory.
  const char* system_directory = nullptr;
  if (!g_retro_environment_callback(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_directory))
    return {};
  return system_directory;
}

void HostInterface::LoadSettings()
{
  LibretroSettingsInterface si;
  g_settings.Load(si);

  // turn percentage into fraction for overclock
  const uint32_t overclock_percent = static_cast<uint32_t>(std::max(si.GetIntValue("CPU", "Overclock", 100), 1));
  Settings::CPUOverclockPercentToFraction(overclock_percent, &g_settings.cpu_overclock_numerator,
                                          &g_settings.cpu_overclock_denominator);
  g_settings.cpu_overclock_enable = (overclock_percent != 100);
  g_settings.UpdateOverclockActive();

  // convert msaa settings
  const std::string msaa = si.GetStringValue("GPU", "MSAA", "1");
  g_settings.gpu_multisamples = StringUtil::FromChars<uint32_t>(msaa).value_or(1);
  g_settings.gpu_per_sample_shading = StringUtil::EndsWith(msaa, "-ssaa");

  for (uint32_t i = 0; i < NUM_CONTROLLER_AND_CARD_PORTS; i++)
  {
    // workaround to make sure controller specific settings don't require a re-init
    switch (retropad_device[i])
    {
      case RETRO_DEVICE_JOYPAD:
      case RETRO_DEVICE_PS_CONTROLLER:
        g_settings.controller_types[i] = ControllerType::DigitalController;
        break;

      case RETRO_DEVICE_PS_DUALSHOCK:
        g_settings.controller_types[i] = ControllerType::AnalogController;
        break;

      case RETRO_DEVICE_PS_ANALOG_JOYSTICK:
        g_settings.controller_types[i] = ControllerType::AnalogJoystick;
        break;

      case RETRO_DEVICE_PS_NEGCON:
        g_settings.controller_types[i] = ControllerType::NeGcon;
        break;

      case RETRO_DEVICE_PS_NEGCON_RUMBLE:
        g_settings.controller_types[i] = ControllerType::NeGconRumble;
        break;

      case RETRO_DEVICE_PS_GUNCON:
        g_settings.controller_types[i] = ControllerType::NamcoGunCon;
        break;

      case RETRO_DEVICE_PS_MOUSE:
        g_settings.controller_types[i] = ControllerType::PlayStationMouse;
        break;

      case RETRO_DEVICE_NONE:
      default:
        g_settings.controller_types[i] = ControllerType::None;
        break;
    }
    // Ensure we don't use the standalone memcard directory in shared mode.
    g_settings.memory_card_paths[i] = GetSharedMemoryCardPath(i);
  }
}

void HostInterface::UpdateSettings()
{
  Settings old_settings(std::move(g_settings));
  LoadSettings();
  ApplyGameSettings();

  if (System::IsValid())
  {
    if (g_settings.gpu_renderer != old_settings.gpu_renderer)
    {
      ReportFormattedMessage("Renderer switch pending, please restart the core to apply.");
      g_settings.gpu_renderer = old_settings.gpu_renderer;
    }
  }

  FixIncompatibleSettings(false);

  if (System::IsValid())
  {
    const bool resolution_scale_changed =
      (g_settings.gpu_resolution_scale != old_settings.gpu_resolution_scale);
    const bool downsample_mode_changed =
      (g_settings.gpu_downsample_mode != old_settings.gpu_downsample_mode);

    if ((resolution_scale_changed || downsample_mode_changed) &&
        g_settings.gpu_renderer != GPURenderer::Software)
    {
      // RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO is heavy: most libretro
      // frontends respond to it by tearing down the hardware
      // rendering context (swapchain, FBOs, the lot) and asking the
      // core to rebuild from scratch. On Vulkan with the new
      // dimensioned batch pipeline cache, that is catastrophic - it
      // wipes every filter / true_color / scaled_dithering sub-cube
      // the worker has built up and forces a several-second cold-
      // cache warm-up.
      //
      // We only need the heavy path when geometry.max_width /
      // max_height ACTUALLY change. Per libretro spec
      // SET_SYSTEM_AV_INFO is the only way to grow those.
      // SET_GEOMETRY explicitly ignores max changes but updates base
      // dimensions and aspect ratio without driver reinit.
      //
      // GetSystemAVInfo locks max to gpu_resolution_scale, so only
      // a true resolution_scale change moves max. Downsample mode
      // moves base only (Box -> base / N, otherwise base * N). The
      // SET_GEOMETRY path is correct - and crucial - for any
      // downsample-only change.
      if (resolution_scale_changed)
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
      }
      else
      {
        // Downsample-only change. Just update base / aspect via
        // SET_GEOMETRY; no driver reinit, no swapchain rebuild, no
        // batch pipeline cache wipe. GPU_HW_Vulkan::UpdateSettings
        // handles the downsample resource swap in <2 ms via
        // CreateDownsampleResources / DestroyDownsampleResources.
        UpdateGeometry();
      }

      // Don't let the base class mess with the GPU.
      old_settings.gpu_resolution_scale = g_settings.gpu_resolution_scale;
    }

    if (g_settings.memory_card_types[0] != old_settings.memory_card_types[0])
    {
      ReportFormattedMessage("Changing memory card 1 type will apply on core reload, to prevent save loss.");
      g_settings.memory_card_types[0] = old_settings.memory_card_types[0];
    }

    if (g_settings.gpu_use_software_renderer_for_readbacks != old_settings.gpu_use_software_renderer_for_readbacks)
    {
      if (g_settings.gpu_use_software_renderer_for_readbacks)
         ReportFormattedMessage("Enabling of software renderer for readbacks pending. Please restart the core to apply.");
      else
         ReportFormattedMessage("Disabling of software renderer for readbacks pending. Please restart the core to apply.");

      g_settings.gpu_use_software_renderer_for_readbacks = old_settings.gpu_use_software_renderer_for_readbacks;
    }

    if (g_settings.audio_fast_hook != old_settings.audio_fast_hook)
    {
      ReportFormattedMessage("Changing audio hook will apply on core reload.");
      g_settings.audio_fast_hook = old_settings.audio_fast_hook;
    }
  }

  CheckForSettingsChanges(old_settings);
}

void HostInterface::OnRunningGameChanged(const std::string& path, CDImage* image, const std::string& game_code,
                                                 const std::string& game_title)
{
  if (UpdateGameSettings())
    UpdateSettings();
}

void HostInterface::InitRumbleInterface()
{
  m_rumble_interface_valid = g_retro_environment_callback(RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE, &m_rumble_interface);
}

void HostInterface::UpdateControllers()
{
  g_retro_input_poll_callback();

  for (uint32_t i = 0; i < NUM_CONTROLLER_AND_CARD_PORTS; i++)
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

      case ControllerType::NeGconRumble:
        UpdateControllersNeGconRumble(i);
        break;

      case ControllerType::NamcoGunCon:
        UpdateControllersNamcoGunCon(i);
        break;

      case ControllerType::PlayStationMouse:
        UpdateControllersPlayStationMouse(i);
        break;

      default:
        break;
    }
  }
}

void HostInterface::UpdateControllersDigitalController(uint32_t index)
{
  DigitalController* controller = static_cast<DigitalController*>(System::GetController(index));

  static constexpr std::array<std::pair<DigitalController::Button, uint32_t>, 14> mapping = {
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
    const uint16_t active = g_retro_input_state_callback(index, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
    for (const auto& it : mapping)
      controller->SetButtonState(it.first, (active & (static_cast<uint16_t>(1u) << it.second)) != 0u);
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

void HostInterface::UpdateControllersAnalogController(uint32_t index)
{
  AnalogController* controller = static_cast<AnalogController*>(System::GetController(index));

  static constexpr std::array<std::pair<AnalogController::Button, uint32_t>, 16> button_mapping = {
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

  static constexpr std::array<std::pair<AnalogController::Axis, std::pair<uint32_t, uint32_t>>, 4> axis_mapping = {
    {{AnalogController::Axis::LeftX, {RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X}},
     {AnalogController::Axis::LeftY, {RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y}},
     {AnalogController::Axis::RightX, {RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X}},
     {AnalogController::Axis::RightY, {RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y}}}};

  // Read all 16 RETRO_DEVICE_ID_JOYPAD_* buttons into a single uint16_t
  // bitmask so we can drive both SetButtonState() and the analog-mode
  // combo-press detection below from one source. The bitmask path
  // costs one callback into the libretro frontend; the fallback path
  // costs sixteen, but that's still strictly fewer than the previous
  // code, which made sixteen-or-one for the loop and then eight more
  // unconditionally for PadCombo_L1..PadCombo_Select.
  uint16_t active;
  if (m_supports_input_bitmasks)
  {
    active = g_retro_input_state_callback(index, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
  }
  else
  {
    active = 0u;
    for (uint32_t id = 0; id < 16u; id++)
    {
      if (g_retro_input_state_callback(index, RETRO_DEVICE_JOYPAD, 0, id) != 0)
        active |= static_cast<uint16_t>(1u << id);
    }
  }

  for (const auto& it : button_mapping)
    controller->SetButtonState(it.first, (active & (static_cast<uint16_t>(1u) << it.second)) != 0u);

  for (const auto& it : axis_mapping)
  {
    const int16_t state = g_retro_input_state_callback(index, RETRO_DEVICE_ANALOG, it.second.first, it.second.second);
    controller->SetAxisState(static_cast<int32_t>(it.first), std::clamp(static_cast<float>(state) / 32767.0f, -1.0f, 1.0f));
  }

  if (m_rumble_interface_valid && g_settings.controller_enable_rumble)
  {
    const uint16_t strong = static_cast<uint16_t>(static_cast<uint32_t>(controller->GetVibrationMotorStrength(0) * 65535.0f));
    const uint16_t weak = static_cast<uint16_t>(static_cast<uint32_t>(controller->GetVibrationMotorStrength(1) * 65535.0f));
    m_rumble_interface.set_rumble_state(index, RETRO_RUMBLE_STRONG, strong);
    m_rumble_interface.set_rumble_state(index, RETRO_RUMBLE_WEAK, weak);
  }

  // Derive the analog-mode combo-press flags from the bitmask we
  // already have, instead of re-querying the frontend eight more
  // times. Compiler will fold the shifts at compile time since the
  // RETRO_DEVICE_ID_JOYPAD_* values are constants.
  const bool PadCombo_L1     = (active & (1u << RETRO_DEVICE_ID_JOYPAD_L))      != 0u;
  const bool PadCombo_R1     = (active & (1u << RETRO_DEVICE_ID_JOYPAD_R))      != 0u;
  const bool PadCombo_L2     = (active & (1u << RETRO_DEVICE_ID_JOYPAD_L2))     != 0u;
  const bool PadCombo_R2     = (active & (1u << RETRO_DEVICE_ID_JOYPAD_R2))     != 0u;
  const bool PadCombo_L3     = (active & (1u << RETRO_DEVICE_ID_JOYPAD_L3))     != 0u;
  const bool PadCombo_R3     = (active & (1u << RETRO_DEVICE_ID_JOYPAD_R3))     != 0u;
  const bool PadCombo_Start  = (active & (1u << RETRO_DEVICE_ID_JOYPAD_START))  != 0u;
  const bool PadCombo_Select = (active & (1u << RETRO_DEVICE_ID_JOYPAD_SELECT)) != 0u;
  int analog_press_status = 0;

  // Check if we're allowed to press the analog button, and then set the selected combo.
  if (!analog_pressed)
  {
    switch (g_settings.controller_analog_combo)
    {
      case 1:
        analog_press_status = (PadCombo_L1 && PadCombo_R1 && PadCombo_L3 && PadCombo_R3);
        break;

      case 2:
        analog_press_status = (PadCombo_L1 && PadCombo_R1 && PadCombo_L2 && PadCombo_R2 && PadCombo_Start && PadCombo_Select);
        break;

      case 3:
        analog_press_status = (PadCombo_L1 && PadCombo_R1 && PadCombo_Select);
        break;

      case 4:
        analog_press_status = (PadCombo_L1 && PadCombo_R1 && PadCombo_Start);
        break;

      case 5:
        analog_press_status = (PadCombo_L1 && PadCombo_R1 && PadCombo_L3);
        break;

      case 6:
        analog_press_status = (PadCombo_L1 && PadCombo_R1 && PadCombo_R3);
        break;

      case 7:
        analog_press_status = (PadCombo_L2 && PadCombo_R2 && PadCombo_Select);
        break;

      case 8:
        analog_press_status = (PadCombo_L2 && PadCombo_R2 && PadCombo_Start);
        break;

      case 9:
        analog_press_status = (PadCombo_L2 && PadCombo_R2 && PadCombo_L3);
        break;

      case 10:
        analog_press_status = (PadCombo_L2 && PadCombo_R2 && PadCombo_R3);
        break;

      case 11:
        analog_press_status = (PadCombo_L3 && PadCombo_R3);
        break;
    }
  }

  // Workaround for the fact it will otherwise spam the analog button.
  if (analog_press_status)
  {
    analog_index = index;
    analog_pressed = true;
    controller->SetButtonState(AnalogController::Button::Analog, (analog_press_status));
    analog_press_status = 0;
  }

  // Check if all possible combo buttons are released and the index matches the player slot.
  // Also make sure having another DualShock plugged in doesn't prematurely clear the button block.
  if (((uint32_t)analog_index == index) && analog_pressed &&
       !PadCombo_L1 && !PadCombo_R1 && !PadCombo_L2 && !PadCombo_R2 && !PadCombo_L3 && !PadCombo_R3 && !PadCombo_Start && !PadCombo_Select)
  {
    analog_pressed = false;
    analog_index = -1;
  }
}

void HostInterface::UpdateControllersAnalogJoystick(uint32_t index)
{
  AnalogJoystick* controller = static_cast<AnalogJoystick*>(System::GetController(index));

  static constexpr std::array<std::pair<AnalogJoystick::Button, uint32_t>, 16> button_mapping = {
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

  static constexpr std::array<std::pair<AnalogJoystick::Axis, std::pair<uint32_t, uint32_t>>, 4> axis_mapping = {
    {{AnalogJoystick::Axis::LeftX, {RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X}},
     {AnalogJoystick::Axis::LeftY, {RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y}},
     {AnalogJoystick::Axis::RightX, {RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X}},
     {AnalogJoystick::Axis::RightY, {RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y}}}};

  if (m_supports_input_bitmasks)
  {
    const uint16_t active = g_retro_input_state_callback(index, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
    for (const auto& it : button_mapping)
      controller->SetButtonState(it.first, (active & (static_cast<uint16_t>(1u) << it.second)) != 0u);
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
    controller->SetAxisState(static_cast<int32_t>(it.first), std::clamp(static_cast<float>(state) / 32767.0f, -1.0f, 1.0f));
  }
}

void HostInterface::UpdateControllersNeGcon(uint32_t index)
{
  NeGcon* controller = static_cast<NeGcon*>(System::GetController(index));

  static constexpr std::array<std::pair<NeGcon::Button, uint32_t>, 8> button_mapping = {
    {{NeGcon::Button::Left, RETRO_DEVICE_ID_JOYPAD_LEFT},
     {NeGcon::Button::Right, RETRO_DEVICE_ID_JOYPAD_RIGHT},
     {NeGcon::Button::Up, RETRO_DEVICE_ID_JOYPAD_UP},
     {NeGcon::Button::Down, RETRO_DEVICE_ID_JOYPAD_DOWN},
     {NeGcon::Button::A, RETRO_DEVICE_ID_JOYPAD_A},
     {NeGcon::Button::B, RETRO_DEVICE_ID_JOYPAD_X},
     {NeGcon::Button::Start, RETRO_DEVICE_ID_JOYPAD_START},
     {NeGcon::Button::R, RETRO_DEVICE_ID_JOYPAD_R}}};

  static constexpr std::array<std::pair<NeGcon::Axis, std::pair<uint32_t, uint32_t>>, 4> axis_mapping = {
    {{NeGcon::Axis::Steering, {RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X}},
     {NeGcon::Axis::I, {RETRO_DEVICE_INDEX_ANALOG_BUTTON, RETRO_DEVICE_ID_JOYPAD_B}},
     {NeGcon::Axis::II, {RETRO_DEVICE_INDEX_ANALOG_BUTTON, RETRO_DEVICE_ID_JOYPAD_Y}},
     {NeGcon::Axis::L, {RETRO_DEVICE_INDEX_ANALOG_BUTTON, RETRO_DEVICE_ID_JOYPAD_L}}}};

  if (m_supports_input_bitmasks)
  {
    const uint16_t active = g_retro_input_state_callback(index, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
    for (const auto& it : button_mapping)
      controller->SetButtonState(it.first, (active & (static_cast<uint16_t>(1u) << it.second)) != 0u);
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
    int16_t state = g_retro_input_state_callback(index, RETRO_DEVICE_ANALOG, it.second.first, it.second.second);
    if (state == 0 && it.second.second == RETRO_DEVICE_ID_JOYPAD_B)
    {
        state = g_retro_input_state_callback(index, RETRO_DEVICE_ANALOG, it.second.first, RETRO_DEVICE_ID_JOYPAD_R2);
    }
    else if (state == 0 && it.second.second == RETRO_DEVICE_ID_JOYPAD_Y)
    {
        state = g_retro_input_state_callback(index, RETRO_DEVICE_ANALOG, it.second.first, RETRO_DEVICE_ID_JOYPAD_L2);
    }

    controller->SetAxisState(static_cast<int32_t>(it.first), std::clamp(static_cast<float>(state) / 32767.0f, -1.0f, 1.0f));
  }

}

void HostInterface::UpdateControllersNeGconRumble(uint32_t index)
{
  NeGconRumble* controller = static_cast<NeGconRumble*>(System::GetController(index));

  static constexpr std::array<std::pair<NeGconRumble::Button, uint32_t>, 8> button_mapping = {
    {{NeGconRumble::Button::Left, RETRO_DEVICE_ID_JOYPAD_LEFT},
     {NeGconRumble::Button::Right, RETRO_DEVICE_ID_JOYPAD_RIGHT},
     {NeGconRumble::Button::Up, RETRO_DEVICE_ID_JOYPAD_UP},
     {NeGconRumble::Button::Down, RETRO_DEVICE_ID_JOYPAD_DOWN},
     {NeGconRumble::Button::A, RETRO_DEVICE_ID_JOYPAD_A},
     {NeGconRumble::Button::B, RETRO_DEVICE_ID_JOYPAD_X},
     {NeGconRumble::Button::Start, RETRO_DEVICE_ID_JOYPAD_START},
     {NeGconRumble::Button::R, RETRO_DEVICE_ID_JOYPAD_R}}};

  static constexpr std::array<std::pair<NeGconRumble::Axis, std::pair<uint32_t, uint32_t>>, 4> axis_mapping = {
    {{NeGconRumble::Axis::Steering, {RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X}},
     {NeGconRumble::Axis::I, {RETRO_DEVICE_INDEX_ANALOG_BUTTON, RETRO_DEVICE_ID_JOYPAD_B}},
     {NeGconRumble::Axis::II, {RETRO_DEVICE_INDEX_ANALOG_BUTTON, RETRO_DEVICE_ID_JOYPAD_Y}},
     {NeGconRumble::Axis::L, {RETRO_DEVICE_INDEX_ANALOG_BUTTON, RETRO_DEVICE_ID_JOYPAD_L}}}};

  // Same pattern as UpdateControllersAnalogController - read once into
  // a uint16_t bitmask (one libretro callback when the frontend supports it,
  // eight buttons via the fallback loop when not), then drive both the
  // SetButtonState pass and the analog-mode-toggle detection from it
  // without re-querying.
  uint16_t active;
  if (m_supports_input_bitmasks)
  {
    active = g_retro_input_state_callback(index, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
  }
  else
  {
    active = 0u;
    for (uint32_t id = 0; id < 16u; id++)
    {
      if (g_retro_input_state_callback(index, RETRO_DEVICE_JOYPAD, 0, id) != 0)
        active |= static_cast<uint16_t>(1u << id);
    }
  }

  for (const auto& it : button_mapping)
    controller->SetButtonState(it.first, (active & (static_cast<uint16_t>(1u) << it.second)) != 0u);

  for (const auto& it : axis_mapping)
  {
    int16_t state = g_retro_input_state_callback(index, RETRO_DEVICE_ANALOG, it.second.first, it.second.second);
    if (state == 0 && it.second.second == RETRO_DEVICE_ID_JOYPAD_B)
    {
        state = g_retro_input_state_callback(index, RETRO_DEVICE_ANALOG, it.second.first, RETRO_DEVICE_ID_JOYPAD_R2);
    }
    else if (state == 0 && it.second.second == RETRO_DEVICE_ID_JOYPAD_Y)
    {
        state = g_retro_input_state_callback(index, RETRO_DEVICE_ANALOG, it.second.first, RETRO_DEVICE_ID_JOYPAD_L2);
    }

    controller->SetAxisState(static_cast<int32_t>(it.first), std::clamp(static_cast<float>(state) / 32767.0f, -1.0f, 1.0f));
  }

  if (m_rumble_interface_valid && g_settings.controller_enable_rumble)
  {
    const uint16_t strong = static_cast<uint16_t>(static_cast<uint32_t>(controller->GetVibrationMotorStrength(0) * 65535.0f));
    const uint16_t weak = static_cast<uint16_t>(static_cast<uint32_t>(controller->GetVibrationMotorStrength(1) * 65535.0f));
    m_rumble_interface.set_rumble_state(index, RETRO_RUMBLE_STRONG, strong);
    m_rumble_interface.set_rumble_state(index, RETRO_RUMBLE_WEAK, weak);
  }

  // All this is retained from UpdateControllersAnalogController because we can't map the Analog button normally due to input spam...
  const bool Analog_Select = (active & (1u << RETRO_DEVICE_ID_JOYPAD_SELECT)) != 0u;
  int analog_press_status = 0;

  // Check if we're allowed to press the analog button, and then set the selected combo.
  if (!analog_pressed)
    analog_press_status = Analog_Select;

  // Workaround for the fact it will otherwise spam the analog button.
  if (analog_press_status)
  {
    analog_index = index;
    analog_pressed = true;
    controller->SetButtonState(NeGconRumble::Button::Analog, (analog_press_status));
    analog_press_status = 0;
  }

  // Check if all possible combo buttons are released and the index matches the player slot.
  // Also make sure having another DualShock plugged in doesn't prematurely clear the button block.
  if (((uint32_t)analog_index == index) && analog_pressed && !Analog_Select)
  {
    analog_pressed = false;
    analog_index = -1;
  }

}

void HostInterface::UpdateControllersNamcoGunCon(uint32_t index)
{
  NamcoGunCon* controller = static_cast<NamcoGunCon*>(System::GetController(index));

  static constexpr std::array<std::pair<NamcoGunCon::Button, uint32_t>, 4> button_mapping = {
    {{NamcoGunCon::Button::Trigger, RETRO_DEVICE_ID_LIGHTGUN_TRIGGER},
     {NamcoGunCon::Button::ShootOffscreen, RETRO_DEVICE_ID_LIGHTGUN_RELOAD},
     {NamcoGunCon::Button::A, RETRO_DEVICE_ID_LIGHTGUN_AUX_A},
     {NamcoGunCon::Button::B, RETRO_DEVICE_ID_LIGHTGUN_AUX_B}}};

  for (const auto& it : button_mapping)
  {
    const int16_t state = g_retro_input_state_callback(index, RETRO_DEVICE_LIGHTGUN, 0, it.second);
    controller->SetButtonState(it.first, state != 0);
  }

  // Mouse range is between -32767 & 32767
  const int16_t gun_x = g_retro_input_state_callback(index, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_SCREEN_X);
  const int16_t gun_y = g_retro_input_state_callback(index, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_SCREEN_Y);
  const bool offscreen =
    g_retro_input_state_callback(index, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_IS_OFFSCREEN) != 0;
  const int32_t pos_x = offscreen ? 0 : (((static_cast<int32_t>(gun_x) + 0x7FFF) * m_display->GetWindowWidth())  / 0xFFFF);
  const int32_t pos_y = offscreen ? 0 : (((static_cast<int32_t>(gun_y) + 0x7FFF) * m_display->GetWindowHeight()) / 0xFFFF);

  // The cached display mouse position is what NamcoGunCon::UpdatePosition
  // reads during a SIO Transfer to compute the gun's beam-tick / scanline
  // pair, so the host position is part of the PSX-visible input fingerprint
  // that runahead has to track. A position change with no button change
  // would otherwise leave s_runahead_replay_pending false and the replayed
  // simulation would aim at last frame's pixel.
  if (pos_x != m_display->GetMousePositionX() || pos_y != m_display->GetMousePositionY())
    System::SetRunaheadReplayFlag();
  m_display->SetMousePosition(pos_x, pos_y);
  // Cache the normalized 16-bit gun state for the HW renderers. They
  // use it to draw the cursor overlay at their own render-target
  // resolution; reading input again from inside Render() would be
  // a second input_state call without an intervening poll, which is
  // undefined per the libretro spec.
  m_display->SetLightgunState(gun_x, gun_y, offscreen);

}

void HostInterface::UpdateControllersPlayStationMouse(uint32_t index)
{
  PlayStationMouse* controller = static_cast<PlayStationMouse*>(System::GetController(index));

  static constexpr std::array<std::pair<PlayStationMouse::Button, uint32_t>, 2> button_mapping = {
    {{PlayStationMouse::Button::Left, RETRO_DEVICE_ID_MOUSE_LEFT},
     {PlayStationMouse::Button::Right, RETRO_DEVICE_ID_MOUSE_RIGHT}}};

  for (const auto& it : button_mapping)
  {
    const int16_t state = g_retro_input_state_callback(index, RETRO_DEVICE_MOUSE, 0, it.second);
    controller->SetButtonState(it.first, state != 0);
  }

  const int16_t mouse_x = g_retro_input_state_callback(index, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X);
  const int16_t mouse_y = g_retro_input_state_callback(index, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y);
  const int32_t pos_x = (m_display->GetMousePositionX() + mouse_x);
  const int32_t pos_y = (m_display->GetMousePositionY() + mouse_y);

  // PlayStationMouse::UpdatePosition reads the cached display position
  // during a SIO Transfer to compute the per-frame delta the PSX sees,
  // so a non-zero raw mouse delta is a PSX-visible input change.
  // Runahead has to know about it or the replay will see an outdated
  // mouse motion.
  if (mouse_x != 0 || mouse_y != 0)
    System::SetRunaheadReplayFlag();
  m_display->SetMousePosition(pos_x, pos_y);

}

bool HostInterface::UpdateCoreOptionsDisplayCallback()
{
  return P_THIS->UpdateCoreOptionsDisplay(false);
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
    case RETRO_HW_CONTEXT_D3D11:
      return GPURenderer::HardwareD3D11;
#ifdef USE_D3D12
    case RETRO_HW_CONTEXT_D3D12:
      return GPURenderer::HardwareD3D12;
#endif
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
#ifdef USE_D3D12
    case HostDisplay::RenderAPI::D3D12:
      return GPURenderer::HardwareD3D12;
#endif
#endif

    default:
      return std::nullopt;
  }
}

bool HostInterface::RequestHardwareRendererContext()
{
  retro_variable renderer_variable{"swanstation_GPU_Renderer",
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
#ifdef USE_D3D12
    case GPURenderer::HardwareD3D12:
      m_hw_render_callback_valid = LibretroD3D12HostDisplay::RequestHardwareRendererContext(&m_hw_render_callback);
      break;
#endif
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

void HostInterface::HardwareRendererContextReset()
{
  Log_InfoPrintf("Hardware context reset, type = %u",
                 static_cast<unsigned>(g_host_interface_storage.m_hw_render_callback.context_type));

  g_host_interface_storage.m_hw_render_callback_valid = true;
  g_host_interface_storage.SwitchToHardwareRenderer();
}

void HostInterface::SwitchToHardwareRenderer()
{
  struct retro_system_av_info avi;
  g_host_interface_storage.GetSystemAVInfo(&avi, true);

  WindowInfo wi;
  wi.display_connection = &g_host_interface_storage.m_hw_render_callback;
  wi.surface_width      = avi.geometry.base_width;
  wi.surface_height     = avi.geometry.base_height;

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
#ifdef USE_D3D12
      case GPURenderer::HardwareD3D12:
        display = std::make_unique<LibretroD3D12HostDisplay>();
        break;
#endif
#endif

      default:
        Log_ErrorPrintf("Unhandled renderer '%s'", Settings::GetRendererName(renderer.value()));
        return;
    }
    if (!display || !display->CreateRenderDevice(wi, {}, false, false) ||
        !display->InitializeRenderDevice(GetShaderCacheBasePath(), false, false))
    {
      Log_ErrorPrintf("Failed to create hardware host display");
      return;
    }
  }

  std::swap(display, g_host_interface_storage.m_display);
  System::RecreateGPU(renderer.value());
  display->DestroyRenderDevice();
  m_using_hardware_renderer = true;
}

void HostInterface::HardwareRendererContextDestroy()
{
  Log_InfoPrintf("Hardware context destroyed");

  // switch back to software
  if (g_host_interface_storage.m_using_hardware_renderer)
    g_host_interface_storage.SwitchToSoftwareRenderer();

  if (g_host_interface_storage.m_hw_render_display)
  {
    g_host_interface_storage.m_hw_render_display->DestroyRenderDevice();
    g_host_interface_storage.m_hw_render_display.reset();
  }

  g_host_interface_storage.m_hw_render_callback_valid = false;
}

void HostInterface::SwitchToSoftwareRenderer()
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

  struct retro_system_av_info avi;
  g_host_interface_storage.GetSystemAVInfo(&avi, false);

  WindowInfo wi;
  wi.surface_width = avi.geometry.base_width;
  wi.surface_height = avi.geometry.base_height;

  m_display = std::make_unique<LibretroHostDisplay>();
  m_display->CreateRenderDevice(wi, {}, false, false);
  m_display->InitializeRenderDevice({}, false, false);
  System::RecreateGPU(GPURenderer::Software, false);

  if (save_display)
  {
    save_display->DestroyResources();
    m_hw_render_display = std::move(save_display);
  }
}

bool HostInterface::DiskControlSetEjectState(bool ejected)
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

bool HostInterface::DiskControlGetEjectState()
{
  if (System::IsShutdown())
    return false;

  return !System::HasMedia();
}

unsigned HostInterface::DiskControlGetImageIndex()
{
  return (unsigned)P_THIS->m_disk_control_info.image_index;
}

bool HostInterface::DiskControlSetImageIndex(unsigned index)
{
  if (System::IsShutdown() ||
      System::HasMedia() ||
      (index >= P_THIS->m_disk_control_info.image_count))
    return false;

  P_THIS->m_disk_control_info.image_index = (uint32_t)index;
  return true;
}

unsigned HostInterface::DiskControlGetNumImages()
{
  return (unsigned)P_THIS->m_disk_control_info.image_count;
}

bool HostInterface::DiskControlReplaceImageIndex(unsigned index, const retro_game_info* info)
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

bool HostInterface::DiskControlAddImageIndex()
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

bool HostInterface::DiskControlSetInitialImage(unsigned index, const char* path)
{
  /* Note: 'path' is ignored, since we cannot
   * determine the actual set path until after
   * content is loaded by the core emulation
   * code (at which point it is too late to
   * compare it with the value supplied here) */
  P_THIS->m_disk_control_info.initial_image_index = index;
  return true;
}

bool HostInterface::DiskControlGetImagePath(unsigned index, char* path, size_t len)
{
  if ((index >= P_THIS->m_disk_control_info.image_count) ||
      (index >= P_THIS->m_disk_control_info.image_paths.size()) ||
      P_THIS->m_disk_control_info.image_paths[index].empty())
    return false;

  strlcpy(path, P_THIS->m_disk_control_info.image_paths[index].c_str(), len);
  return true;
}

bool HostInterface::DiskControlGetImageLabel(unsigned index, char* label, size_t len)
{
  if ((index >= P_THIS->m_disk_control_info.image_count) ||
      (index >= P_THIS->m_disk_control_info.image_labels.size()) ||
      P_THIS->m_disk_control_info.image_labels[index].empty())
    return false;

  strlcpy(label, P_THIS->m_disk_control_info.image_labels[index].c_str(), len);
  return true;
}

void HostInterface::InitDiskControlInterface()
{
  unsigned version = 0;
  if (g_retro_environment_callback(RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION, &version) && version >= 1)
  {
    retro_disk_control_ext_callback ext_cb = {
      &HostInterface::DiskControlSetEjectState, &HostInterface::DiskControlGetEjectState,
      &HostInterface::DiskControlGetImageIndex, &HostInterface::DiskControlSetImageIndex,
      &HostInterface::DiskControlGetNumImages,  &HostInterface::DiskControlReplaceImageIndex,
      &HostInterface::DiskControlAddImageIndex, &HostInterface::DiskControlSetInitialImage,
      &HostInterface::DiskControlGetImagePath,  &HostInterface::DiskControlGetImageLabel};
    if (g_retro_environment_callback(RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE, &ext_cb))
      return;
  }

  retro_disk_control_callback cb = {
    &HostInterface::DiskControlSetEjectState, &HostInterface::DiskControlGetEjectState,
    &HostInterface::DiskControlGetImageIndex, &HostInterface::DiskControlSetImageIndex,
    &HostInterface::DiskControlGetNumImages,  &HostInterface::DiskControlReplaceImageIndex,
    &HostInterface::DiskControlAddImageIndex};
  if (!g_retro_environment_callback(RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE, &cb))
    Log_WarningPrint("Failed to set disk control interface");
}
