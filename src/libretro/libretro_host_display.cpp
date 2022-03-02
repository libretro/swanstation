#include "libretro_host_display.h"
#include "common/align.h"
#include "common/assert.h"
#include "common/log.h"
#include "libretro_host_interface.h"
#include <libretro.h>
#include <array>
#include <tuple>
Log_SetChannel(LibretroHostDisplay);

static retro_pixel_format GetRetroPixelFormat(HostDisplayPixelFormat format)
{
  switch (format)
  {
    case HostDisplayPixelFormat::BGRA8:
      return RETRO_PIXEL_FORMAT_XRGB8888;

    case HostDisplayPixelFormat::RGB565:
      return RETRO_PIXEL_FORMAT_RGB565;

    case HostDisplayPixelFormat::RGBA5551:
      return RETRO_PIXEL_FORMAT_0RGB1555;

    default:
      return RETRO_PIXEL_FORMAT_UNKNOWN;
  }
}

LibretroHostDisplay::LibretroHostDisplay()
{
  retro_pixel_format pf = RETRO_PIXEL_FORMAT_RGB565;
  if (!g_retro_environment_callback(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &pf))
    Log_ErrorPrint("Failed to set pixel format to RGB565");
  else
    m_current_pixel_format = pf;
}

LibretroHostDisplay::~LibretroHostDisplay() = default;

bool LibretroHostDisplay::CheckPixelFormat(retro_pixel_format new_format)
{
  if (new_format == RETRO_PIXEL_FORMAT_UNKNOWN || m_current_pixel_format == new_format)
    return true;

  if (!g_retro_environment_callback(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &new_format))
  {
    Log_ErrorPrintf("g_retro_environment_callback(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, %u) failed",
                    static_cast<unsigned>(new_format));
    return false;
  }

  if (!g_libretro_host_interface.UpdateSystemAVInfo(false))
    return false;

  m_current_pixel_format = new_format;
  return true;
}

HostDisplay::RenderAPI LibretroHostDisplay::GetRenderAPI() const
{
  return RenderAPI::None;
}

void* LibretroHostDisplay::GetRenderDevice() const
{
  return nullptr;
}

void* LibretroHostDisplay::GetRenderContext() const
{
  return nullptr;
}

bool LibretroHostDisplay::HasRenderDevice() const
{
  return true;
}

bool LibretroHostDisplay::HasRenderSurface() const
{
  return true;
}

bool LibretroHostDisplay::CreateRenderDevice(const WindowInfo& wi, std::string_view adapter_name, bool debug_device,
                                             bool threaded_presentation)
{
  m_window_info = wi;
  return true;
}

bool LibretroHostDisplay::InitializeRenderDevice(std::string_view shader_cache_directory, bool debug_device,
                                                 bool threaded_presentation)
{
  return true;
}

bool LibretroHostDisplay::MakeRenderContextCurrent()
{
  return true;
}

bool LibretroHostDisplay::DoneRenderContextCurrent()
{
  return true;
}

void LibretroHostDisplay::DestroyRenderDevice()
{
  ClearSoftwareCursor();
}

void LibretroHostDisplay::DestroyRenderSurface() {}

bool LibretroHostDisplay::CreateResources()
{
  return true;
}

void LibretroHostDisplay::DestroyResources() {}

bool LibretroHostDisplay::ChangeRenderWindow(const WindowInfo& wi)
{
  m_window_info = wi;
  return true;
}

void LibretroHostDisplay::ResizeRenderWindow(s32 new_window_width, s32 new_window_height)
{
  m_window_info.surface_width = new_window_width;
  m_window_info.surface_height = new_window_height;
}

bool LibretroHostDisplay::SupportsFullscreen() const
{
  return false;
}

bool LibretroHostDisplay::IsFullscreen()
{
  return false;
}

bool LibretroHostDisplay::SetFullscreen(bool fullscreen, u32 width, u32 height, float refresh_rate)
{
  return false;
}

HostDisplay::AdapterAndModeList LibretroHostDisplay::GetAdapterAndModeList()
{
  return {};
}

bool LibretroHostDisplay::SetPostProcessingChain(const std::string_view& config)
{
  return false;
}

std::unique_ptr<HostDisplayTexture> LibretroHostDisplay::CreateTexture(u32 width, u32 height, u32 layers, u32 levels,
                                                                       u32 samples, HostDisplayPixelFormat format,
                                                                       const void* data, u32 data_stride,
                                                                       bool dynamic)
{
  return nullptr;
}

void LibretroHostDisplay::UpdateTexture(HostDisplayTexture* texture, u32 x, u32 y, u32 width, u32 height,
                                        const void* data, u32 data_stride)
{
}

bool LibretroHostDisplay::DownloadTexture(const void* texture_handle, HostDisplayPixelFormat texture_format, u32 x,
                                          u32 y, u32 width, u32 height, void* out_data, u32 out_data_stride)
{
  return false;
}

bool LibretroHostDisplay::SupportsDisplayPixelFormat(HostDisplayPixelFormat format) const
{
  // For when we can change the pixel format.
  // return (GetRetroPixelFormat(format) != RETRO_PIXEL_FORMAT_UNKNOWN);
  return (GetRetroPixelFormat(format) == m_current_pixel_format);
}

bool LibretroHostDisplay::BeginSetDisplayPixels(HostDisplayPixelFormat format, u32 width, u32 height, void** out_buffer,
                                                u32* out_pitch)
{
  const retro_pixel_format retro_pf = GetRetroPixelFormat(format);
  if (!CheckPixelFormat(retro_pf))
    return false;

  const u32 full_width = (static_cast<u32>(m_display_active_left) + static_cast<u32>(m_display_width - m_display_active_left - m_display_active_width) + width);
  const u32 full_height = (static_cast<u32>(m_display_active_top) + static_cast<u32>(m_display_height - m_display_active_top - m_display_active_height) + height);
  m_software_fb.data = nullptr;
  m_software_fb.width = full_width;
  m_software_fb.height = full_height;
  m_software_fb.pitch = 0;
  m_software_fb.format = RETRO_PIXEL_FORMAT_UNKNOWN;
  m_software_fb.access_flags = RETRO_MEMORY_ACCESS_WRITE;
  m_software_fb.memory_flags = 0;
  const u32 pixel_size = GetDisplayPixelFormatSize(format);

  if (g_retro_environment_callback(RETRO_ENVIRONMENT_GET_CURRENT_SOFTWARE_FRAMEBUFFER, &m_software_fb) &&
      m_software_fb.format == retro_pf)
  {
    const u32 active_left = static_cast<u32>(m_display_active_left);
    const u32 active_right = static_cast<u32>(m_display_width - m_display_active_left - m_display_active_width);
    const u32 active_top = static_cast<u32>(m_display_active_top);
    const u32 active_bottom = static_cast<u32>(m_display_height - m_display_active_top - m_display_active_height);
    const u32 pitch = static_cast<u32>(m_software_fb.pitch);
    u8* pixel_start = reinterpret_cast<u8*>(m_software_fb.data);

    if (active_top > 0)
    {
      std::memset(pixel_start, 0, pixel_size * active_top * pitch);
      pixel_start += pixel_size * active_top * pitch;
    }

    if (active_left > 0 || active_right > 0)
    {
      const u32 left_pixel_size = pixel_size * active_left;
      const u32 right_pixel_size = pixel_size * active_right;
      u8* pixel_end = pixel_start + ((active_left + width) * pixel_size);
      for (u32 i = 0; i < height; i++)
      {
        std::memset(pixel_start, 0, left_pixel_size);
        std::memset(pixel_end, 0, right_pixel_size);
        pixel_start += pitch;
        pixel_end += pitch;
      }
    }

    if (active_bottom > 0)
      std::memset(pixel_start, 0, pixel_size * active_bottom * pitch);

    m_frame_buffer_pitch = m_software_fb.pitch;
    SetDisplayTexture(m_software_fb.data, format, m_software_fb.width, m_software_fb.height, 0, 0, m_software_fb.width,
                      m_software_fb.height);
    *out_buffer = reinterpret_cast<u8*>(m_software_fb.data) + (active_left * pixel_size) + (active_top * pitch);
    *out_pitch = static_cast<u32>(m_software_fb.pitch);
    return true;
  }

  const u32 pitch = Common::AlignUpPow2(full_width * GetDisplayPixelFormatSize(format), 4);
  const u32 required_size = full_height * pitch;
  if (m_frame_buffer.size() != (required_size / 4))
  {
    m_frame_buffer.clear();
    m_frame_buffer.resize(required_size / 4);
  }

  m_frame_buffer_pitch = pitch;
  SetDisplayTexture(m_frame_buffer.data(), format, full_width, full_height, 0, 0, full_width, full_height);
  *out_buffer = reinterpret_cast<u8*>(m_frame_buffer.data()) + (static_cast<u32>(m_display_active_top) * pitch) + (static_cast<u32>(m_display_active_left) * pixel_size);
  *out_pitch = pitch;
  return true;
}

void LibretroHostDisplay::EndSetDisplayPixels()
{
  // noop
}

void LibretroHostDisplay::SetVSync(bool enabled)
{
  // The libretro frontend controls this.
  Log_DevPrintf("Ignoring SetVSync(%u)", BoolToUInt32(enabled));
}

bool LibretroHostDisplay::Render()
{
  if (HasDisplayTexture())
  {
    g_retro_video_refresh_callback(m_display_texture_handle, m_display_texture_view_width,
                                   m_display_texture_view_height, m_frame_buffer_pitch);

    if (m_display_texture_handle == m_software_fb.data)
      ClearDisplayTexture();
  }
  else
  {
    g_retro_video_refresh_callback(nullptr, 0, 0, 0);
  }

  return true;
}

bool LibretroHostDisplay::RenderScreenshot(u32 width, u32 height, std::vector<u32>* out_pixels, u32* out_stride,
                                         HostDisplayPixelFormat* out_format)
{
  return false;
}
