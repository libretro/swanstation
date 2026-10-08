#include "host_display.h"
#include "common/align.h"
#include "common/string_util.h"
#include "common/timer.h"
#include "host_interface.h"
#include "stb_image.h"
#include "stb_image_resize.h"
#include <cerrno>
#include <cmath>
#include <cstring>
#include <vector>

HostDisplayTexture::~HostDisplayTexture() = default;

HostDisplay::~HostDisplay() = default;

uint32_t HostDisplay::GetDisplayPixelFormatSize(HostDisplayPixelFormat format)
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

bool HostDisplay::SetDisplayPixels(HostDisplayPixelFormat format, uint32_t width, uint32_t height, const void* buffer, uint32_t pitch)
{
  void* map_ptr;
  uint32_t map_pitch;
  if (!BeginSetDisplayPixels(format, width, height, &map_ptr, &map_pitch))
    return false;

  if (pitch == map_pitch)
  {
    std::memcpy(map_ptr, buffer, height * map_pitch);
  }
  else
  {
    const uint32_t copy_size = width * GetDisplayPixelFormatSize(format);
    const uint8_t* src_ptr   = static_cast<const uint8_t*>(buffer);
    uint8_t* dst_ptr         = static_cast<uint8_t*>(map_ptr);
    for (uint32_t i = 0; i < height; i++)
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

bool HostDisplay::SetSoftwareCursor(const void* pixels, uint32_t width, uint32_t height, uint32_t stride, float scale /*= 1.0f*/)
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

void HostDisplay::CalculateDrawRect(int32_t window_width, int32_t window_height, float* out_left, float* out_top,
                                    float* out_width, float* out_height, float* out_left_padding,
                                    float* out_top_padding, float* out_scale, float* out_x_scale,
                                    bool apply_aspect_ratio /* = true */) const
{
  const float window_ratio = static_cast<float>(window_width) / static_cast<float>(window_height);
  const float display_aspect_ratio = m_display_aspect_ratio;
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

    if (out_left_padding)
      *out_left_padding = 0.0f;
    if (out_top_padding)
      *out_top_padding =
            std::max<float>((static_cast<float>(window_height) - (display_height * scale)) / 2.0f, 0.0f);
  }
  else
  {
    // align in middle horizontally
    scale = static_cast<float>(window_height) / display_height;

    if (out_left_padding)
      *out_left_padding =
            std::max<float>((static_cast<float>(window_width) - (display_width * scale)) / 2.0f, 0.0f);

    if (out_top_padding)
      *out_top_padding = 0.0f;
  }

  *out_width = active_width * scale;
  *out_height = active_height * scale;
  *out_left = active_left * scale;
  *out_top = active_top * scale;
  if (out_scale)
    *out_scale = scale;
}

std::tuple<int32_t, int32_t, int32_t, int32_t> HostDisplay::CalculateDrawRect(int32_t window_width, int32_t window_height, int32_t top_margin,
                                                              bool apply_aspect_ratio /* = true */) const
{
  float left, top, width, height, left_padding, top_padding;
  CalculateDrawRect(window_width, window_height - top_margin, &left, &top, &width, &height, &left_padding, &top_padding,
                    nullptr, nullptr, apply_aspect_ratio);

  return std::make_tuple(static_cast<int32_t>(left + left_padding), static_cast<int32_t>(top + top_padding) + top_margin,
                         static_cast<int32_t>(width), static_cast<int32_t>(height));
}

std::tuple<float, float> HostDisplay::ConvertWindowCoordinatesToDisplayCoordinates(int32_t window_x, int32_t window_y,
                                                                                   int32_t window_width, int32_t window_height,
                                                                                   int32_t top_margin) const
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
