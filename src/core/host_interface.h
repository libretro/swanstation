#pragma once
#include "common/string.h"
#include "common/timer.h"
#include "settings.h"
#include "system.h"
#include "types.h"
#include <libretro.h>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

enum class LogLevel : uint8_t;

class LibretroAudioStream;
class ByteStream;
class CDImage;
class HostDisplay;
class GameList;

struct SystemBootParameters;

namespace BIOS {
struct ImageInfo;
}

namespace GameSettings {
struct Entry;
}

// HostInterface used to be an abstract base class with a single concrete
// subclass (LibretroHostInterface). The two-class layering predates the
// libretro-only build of swanstation - upstream DuckStation has multiple
// frontends (Qt, NoGui, Android) - but in this fork, the only consumer
// is the libretro frontend, so the indirection bought nothing except a
// vtable on every method that used to be virtual.
//
// The two classes have been merged here. Methods that used to be on
// the base (BootSystem, GetBIOSImage, AddOSDMessage, the
// translation/error/OSD helpers, ...) are kept as plain non-virtual
// member functions; methods that used to be libretro-only (retro_*,
// the controller update helpers, hardware renderer setup, the disk
// control callbacks) move in alongside them. Data members likewise.
class HostInterface
{
public:

  struct DiskControlInfo
  {
    bool has_sub_images;
    bool ejected;
    uint32_t initial_image_index;
    uint32_t image_index;
    uint32_t image_count;
    std::string sub_images_parent_path;
    std::vector<std::string> image_paths;
    std::vector<std::string> image_labels;
  };

  HostInterface();
  ~HostInterface();

  /// Access to host display.
  ALWAYS_INLINE HostDisplay* GetDisplay() const { return m_display.get(); }

  /// Access to host audio stream.
  ALWAYS_INLINE LibretroAudioStream* GetAudioStream() const { return m_audio_stream.get(); }

  ALWAYS_INLINE uint32_t GetResolutionScale() const
  {
    return (g_settings.gpu_downsample_mode == GPUDownsampleMode::Box) ? 1u : g_settings.gpu_resolution_scale;
  }

  /// Initializes the emulator frontend.
  bool Initialize();

  /// Shuts down the emulator frontend.
  void Shutdown();

  bool BootSystem(std::shared_ptr<SystemBootParameters> parameters);
  void ResetSystem();
  void DestroySystem();

  void ReportError(const char* message);
  void ReportMessage(const char* message);
  bool ConfirmMessage(const char* message);

  void ReportFormattedError(const char* format, ...) printflike(2, 3);
  void ReportFormattedMessage(const char* format, ...) printflike(2, 3);

  /// Adds OSD messages, duration is in seconds.
  void AddOSDMessage(std::string message, float duration = 2.0f);
  void AddFormattedOSDMessage(float duration, const char* format, ...) printflike(3, 4);

  /// Returns a path relative to the user directory.
  std::string GetUserDirectoryRelativePath(const char* format, ...) const printflike(2, 3);

  /// Displays a loading screen with the logo, rendered with ImGui. Use when executing possibly-time-consuming tasks
  /// such as compiling shaders when starting up.
  void DisplayLoadingScreen(const char* message, int progress_min = -1, int progress_max = -1, int progress_value = -1);

  /// Retrieves information about specified game from game list.
  void GetGameInfo(const char* path, CDImage* image, std::string* code, std::string* title);

  /// Returns the default path to a memory card.
  std::string GetSharedMemoryCardPath(uint32_t slot) const;

  /// Returns the default path to a memory card for a specific game.
  std::string GetGameMemoryCardPath(const char* game_code, uint32_t slot) const;

  /// Returns the path to the shader cache directory.
  std::string GetShaderCacheBasePath() const;

  /// Returns a setting value from the configuration.
  std::string GetStringSettingValue(const char* section, const char* key, const char* default_value = "");

  /// Returns a boolean setting from the configuration.
  bool GetBoolSettingValue(const char* section, const char* key, bool default_value = false);

  /// Returns an integer setting from the configuration.
  int GetIntSettingValue(const char* section, const char* key, int default_value = 0);

  /// Returns a float setting from the configuration.
  float GetFloatSettingValue(const char* section, const char* key, float default_value = 0.0f);

  /// Translates a string to the current language.
  TinyString TranslateString(const char* context, const char* str, const char* disambiguation = nullptr,
                             int n = -1) const;
  std::string TranslateStdString(const char* context, const char* str, const char* disambiguation = nullptr,
                                 int n = -1) const;

  /// Returns the path to the directory to search for BIOS images.
  std::string GetBIOSDirectory();

  /// Loads the BIOS image for the specified region.
  std::optional<std::vector<uint8_t>> GetBIOSImage(ConsoleRegion region);

  void OnRunningGameChanged(const std::string& path, CDImage* image, const std::string& game_code,
                            const std::string& game_title);

  bool UpdateSystemAVInfo(bool use_resolution_scale);
  bool UpdateCoreOptionsDisplay(bool controller);

  // Called by frontend
  void retro_set_environment();
  void retro_get_system_av_info(struct retro_system_av_info* info);
  bool retro_load_game(const struct retro_game_info* game);
  void retro_set_controller_port_device(uint32_t port, uint32_t device);
  void retro_run_frame();
  unsigned retro_get_region();
  size_t retro_serialize_size();
  bool retro_serialize(void* data, size_t size);
  bool retro_unserialize(const void* data, size_t size);
  void* retro_get_memory_data(unsigned id);
  size_t retro_get_memory_size(unsigned id);
  void retro_cheat_reset();
  void retro_cheat_set(unsigned index, bool enabled, const char* code);

  // Display + settings hook-ups (used to be virtual, now plain).
  void AcquireHostDisplay();
  void ReleaseHostDisplay();
  void OnControllerTypeChanged(uint32_t slot);

  /// Checks and fixes up any incompatible settings.
  void FixIncompatibleSettings(bool display_osd_messages);

  /// Checks for settings changes, std::move() the old settings away for comparing beforehand.
  void CheckForSettingsChanges(const Settings& old_settings);

private:
  /// Switches the GPU renderer by saving state, recreating the display window, and restoring state (if needed).
  void RecreateSystem();

  /// Updates software cursor state, based on controllers.
  void UpdateSoftwareCursor();

  std::optional<std::vector<uint8_t>> FindBIOSImageInDirectory(ConsoleRegion region, const char* directory);
  bool HasCoreVariablesChanged();
  void InitInterfaces();
  void InitLogging();
  void InitDiskControlInterface();
  void InitRumbleInterface();

  void LoadSettings();
  void UpdateSettings();
  void UpdateControllers();
  void UpdateControllersDigitalController(uint32_t index);
  void UpdateControllersAnalogController(uint32_t index);
  void UpdateControllersAnalogJoystick(uint32_t index);
  void UpdateControllersNeGcon(uint32_t index);
  void UpdateControllersNeGconRumble(uint32_t index);
  void UpdateControllersNamcoGunCon(uint32_t index);
  void UpdateControllersPlayStationMouse(uint32_t index);
  void GetSystemAVInfo(struct retro_system_av_info* info, bool use_resolution_scale);
  void UpdateGeometry();
  void UpdateLogging();

  bool UpdateGameSettings();
  void ApplyGameSettings();

  static bool RETRO_CALLCONV UpdateCoreOptionsDisplayCallback();

  // Hardware renderer setup.
  bool RequestHardwareRendererContext();
  void SwitchToHardwareRenderer();
  void SwitchToSoftwareRenderer();

  static void HardwareRendererContextReset();
  static void HardwareRendererContextDestroy();

  // Disk control callbacks
  static bool RETRO_CALLCONV DiskControlSetEjectState(bool ejected);
  static bool RETRO_CALLCONV DiskControlGetEjectState();
  static unsigned RETRO_CALLCONV DiskControlGetImageIndex();
  static bool RETRO_CALLCONV DiskControlSetImageIndex(unsigned index);
  static unsigned RETRO_CALLCONV DiskControlGetNumImages();
  static bool RETRO_CALLCONV DiskControlReplaceImageIndex(unsigned index, const retro_game_info* info);
  static bool RETRO_CALLCONV DiskControlAddImageIndex();
  static bool RETRO_CALLCONV DiskControlSetInitialImage(unsigned index, const char* path);
  static bool RETRO_CALLCONV DiskControlGetImagePath(unsigned index, char* path, size_t len);
  static bool RETRO_CALLCONV DiskControlGetImageLabel(unsigned index, char* label, size_t len);

  std::unique_ptr<HostDisplay> m_display;
  std::unique_ptr<LibretroAudioStream> m_audio_stream;
  std::string m_user_directory;

  std::unique_ptr<GameSettings::Entry> m_game_settings;
  float m_last_aspect_ratio = 4.0f / 3.0f;
  // Tracks the most recently advertised vertical refresh so retro_run_frame
  // can re-issue RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO when the PSX changes
  // CRTC mode (e.g., NTSC <-> PAL). RETRO_ENVIRONMENT_SET_GEOMETRY only
  // updates the geometry struct and the frontend never learns about the
  // new fps; an audio resampler driven by the stale ratio drifts.
  float m_last_vertical_frequency = 60.0f;

  std::array<uint32_t, NUM_CONTROLLER_AND_CARD_PORTS> retropad_device = {RETRO_DEVICE_JOYPAD};

  bool controller_dirty = false;

  retro_hw_render_callback m_hw_render_callback = {};
  std::unique_ptr<HostDisplay> m_hw_render_display;
  bool m_hw_render_callback_valid = false;
  bool m_using_hardware_renderer = false;

  retro_rumble_interface m_rumble_interface = {};
  bool m_rumble_interface_valid = false;
  bool m_supports_input_bitmasks = false;

  DiskControlInfo m_disk_control_info = {};
};

#define TRANSLATABLE(context, str) str

// The single HostInterface instance (storage) and the global pointer
// to it. The pointer is set in HostInterface's constructor and cleared
// in its destructor; pre-merge there used to be one each named
// g_libretro_host_interface (storage, of type LibretroHostInterface) and
// g_host_interface (pointer, of type HostInterface*). Both names had to
// exist because the abstract-base / concrete-subclass split required a
// separate concrete-typed singleton for the libretro entry-point
// callbacks to dispatch into. With the inheritance gone, that need
// goes away too, but the storage variable keeps the same role: every
// libretro retro_* function in libretro_host_interface.cpp dispatches
// into it.
extern HostInterface g_host_interface_storage;
extern HostInterface* g_host_interface;

// libretro callbacks
extern retro_environment_t g_retro_environment_callback;
extern retro_video_refresh_t g_retro_video_refresh_callback;
extern retro_audio_sample_t g_retro_audio_sample_callback;
extern retro_audio_sample_batch_t g_retro_audio_sample_batch_callback;
extern retro_input_poll_t g_retro_input_poll_callback;
extern retro_input_state_t g_retro_input_state_callback;

// Per-frame A/V skip flags driven by RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE.
// When the frontend is running ahead via single-instance runahead (or fast-
// forwarding past audio for the same reason), it tells us per-frame to skip
// the video and/or audio output callbacks. We still simulate everything so
// the next "live" frame is correct - we just drop the frontend-side output
// for the discarded frames, avoiding audio glitches and wasted GPU work.
// Both default to false; see retro_run_frame() for how they're refreshed.
extern bool g_retro_skip_video_this_frame;
extern bool g_retro_skip_audio_this_frame;
