#include "host_display.h"
#include "common/align.h"
#include "common/assert.h"
#include "common/string_util.h"
#include "common/timer.h"
#include "host_interface.h"
#include "stb_image.h"
#include "stb_image_resize.h"
#include "stb_image_write.h"
#include <cerrno>
#include <cmath>
#include <cstring>
#include <thread>
#include <vector>

HostDisplayTexture::~HostDisplayTexture() = default;

HostDisplay::~HostDisplay() = default;

u32 HostDisplay::GetDisplayPixelFormatSize(HostDisplayPixelFormat format)
{
  switch (format)
  {
    case HostDisplayPixelFormat::RGBA8:
    case HostDisplayPixelFormat::BGRA8:
      return 4;

    case HostDisplayPixelFormat::RGBA5551:
    case HostDisplayPixelFormat::RGB565:
      return 2;

    default:
      break;
  }
  return 0;
}

bool HostDisplay::SetDisplayPixels(HostDisplayPixelFormat format, u32 width, u32 height, const void* buffer, u32 pitch)
{
  void* map_ptr;
  u32 map_pitch;
  if (!BeginSetDisplayPixels(format, width, height, &map_ptr, &map_pitch))
    return false;

  if (pitch == map_pitch)
  {
    std::memcpy(map_ptr, buffer, height * map_pitch);
  }
  else
  {
    const u32 copy_size = width * GetDisplayPixelFormatSize(format);
    DebugAssert(pitch >= copy_size && map_pitch >= copy_size);

    const u8* src_ptr = static_cast<const u8*>(buffer);
    u8* dst_ptr = static_cast<u8*>(map_ptr);
    for (u32 i = 0; i < height; i++)
    {
      std::memcpy(dst_ptr, src_ptr, copy_size);
      src_ptr += pitch;
      dst_ptr += map_pitch;
    }
  }

  EndSetDisplayPixels();
  return true;
}

void HostDisplay::SetSoftwareCursor(std::unique_ptr<HostDisplayTexture> texture, float scale /*= 1.0f*/)
{
  m_cursor_texture = std::move(texture);
  m_cursor_texture_scale = scale;
}

bool HostDisplay::SetSoftwareCursor(const void* pixels, u32 width, u32 height, u32 stride, float scale /*= 1.0f*/)
{
  std::unique_ptr<HostDisplayTexture> tex =
    CreateTexture(width, height, 1, 1, 1, HostDisplayPixelFormat::RGBA8, pixels, stride, false);
  if (!tex)
    return false;

  SetSoftwareCursor(std::move(tex), scale);
  return true;
}

void HostDisplay::ClearSoftwareCursor()
{
  m_cursor_texture.reset();
  m_cursor_texture_scale = 1.0f;
}

void HostDisplay::CalculateDrawRect(s32 window_width, s32 window_height, float* out_left, float* out_top,
                                    float* out_width, float* out_height, float* out_left_padding,
                                    float* out_top_padding, float* out_scale, float* out_x_scale,
                                    bool apply_aspect_ratio /* = true */) const
{
  const float window_ratio = static_cast<float>(window_width) / static_cast<float>(window_height);
  const float display_aspect_ratio = m_display_stretch ? window_ratio : m_display_aspect_ratio;
  const float x_scale =
    apply_aspect_ratio ?
      (display_aspect_ratio / (static_cast<float>(m_display_width) / static_cast<float>(m_display_height))) :
      1.0f;
  const float display_width = static_cast<float>(m_display_width) * x_scale;
  const float display_height = static_cast<float>(m_display_height);
  const float active_left = static_cast<float>(m_display_active_left) * x_scale;
  const float active_top = static_cast<float>(m_display_active_top);
  const float active_width = static_cast<float>(m_display_active_width) * x_scale;
  const float active_height = static_cast<float>(m_display_active_height);
  if (out_x_scale)
    *out_x_scale = x_scale;

  // now fit it within the window
  float scale;
  if ((display_width / display_height) >= window_ratio)
  {
    // align in middle vertically
    scale = static_cast<float>(window_width) / display_width;
    if (m_display_integer_scaling)
      scale = std::max(std::floor(scale), 1.0f);

    if (out_left_padding)
    {
      if (m_display_integer_scaling)
        *out_left_padding = std::max<float>((static_cast<float>(window_width) - display_width * scale) / 2.0f, 0.0f);
      else
        *out_left_padding = 0.0f;
    }
    if (out_top_padding)
    {
      *out_top_padding =
            std::max<float>((static_cast<float>(window_height) - (display_height * scale)) / 2.0f, 0.0f);
    }
  }
  else
  {
    // align in middle horizontally
    scale = static_cast<float>(window_height) / display_height;
    if (m_display_integer_scaling)
      scale = std::max(std::floor(scale), 1.0f);

    if (out_left_padding)
    {
      *out_left_padding =
            std::max<float>((static_cast<float>(window_width) - (display_width * scale)) / 2.0f, 0.0f);
    }

    if (out_top_padding)
    {
      if (m_display_integer_scaling)
        *out_top_padding = std::max<float>((static_cast<float>(window_height) - (display_height * scale)) / 2.0f, 0.0f);
      else
        *out_top_padding = 0.0f;
    }
  }

  *out_width = active_width * scale;
  *out_height = active_height * scale;
  *out_left = active_left * scale;
  *out_top = active_top * scale;
  if (out_scale)
    *out_scale = scale;
}

std::tuple<s32, s32, s32, s32> HostDisplay::CalculateDrawRect(s32 window_width, s32 window_height, s32 top_margin,
                                                              bool apply_aspect_ratio /* = true */) const
{
  float left, top, width, height, left_padding, top_padding;
  CalculateDrawRect(window_width, window_height - top_margin, &left, &top, &width, &height, &left_padding, &top_padding,
                    nullptr, nullptr, apply_aspect_ratio);

  return std::make_tuple(static_cast<s32>(left + left_padding), static_cast<s32>(top + top_padding) + top_margin,
                         static_cast<s32>(width), static_cast<s32>(height));
}

std::tuple<s32, s32, s32, s32> HostDisplay::CalculateSoftwareCursorDrawRect() const
{
  return CalculateSoftwareCursorDrawRect(m_mouse_position_x, m_mouse_position_y);
}

std::tuple<s32, s32, s32, s32> HostDisplay::CalculateSoftwareCursorDrawRect(s32 cursor_x, s32 cursor_y) const
{
  const float scale = m_window_info.surface_scale * m_cursor_texture_scale;
  const u32 cursor_extents_x = static_cast<u32>(static_cast<float>(m_cursor_texture->GetWidth()) * scale * 0.5f);
  const u32 cursor_extents_y = static_cast<u32>(static_cast<float>(m_cursor_texture->GetHeight()) * scale * 0.5f);

  const s32 out_left = cursor_x - cursor_extents_x;
  const s32 out_top = cursor_y - cursor_extents_y;
  const s32 out_width = cursor_extents_x * 2u;
  const s32 out_height = cursor_extents_y * 2u;

  return std::tie(out_left, out_top, out_width, out_height);
}

std::tuple<float, float> HostDisplay::ConvertWindowCoordinatesToDisplayCoordinates(s32 window_x, s32 window_y,
                                                                                   s32 window_width, s32 window_height,
                                                                                   s32 top_margin) const
{
  float left, top, width, height, left_padding, top_padding;
  float scale, x_scale;
  CalculateDrawRect(window_width, window_height - top_margin, &left, &top, &width, &height, &left_padding, &top_padding,
                    &scale, &x_scale);

  // convert coordinates to active display region, then to full display region
  const float scaled_display_x = static_cast<float>(window_x) - left_padding;
  const float scaled_display_y = static_cast<float>(window_y) - top_padding + static_cast<float>(top_margin);

  // scale back to internal resolution
  const float display_x = scaled_display_x / scale / x_scale;
  const float display_y = scaled_display_y / scale;

  return std::make_tuple(display_x, display_y);
}
