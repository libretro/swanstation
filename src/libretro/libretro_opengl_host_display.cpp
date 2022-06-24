#include "libretro_opengl_host_display.h"
#include "common/align.h"
#include "common/assert.h"
#include "common/log.h"
#include "core/gpu.h"
#include "libretro_host_interface.h"
#include <libretro.h>
#include <array>
#include <tuple>
Log_SetChannel(LibretroOpenGLHostDisplay);

class LibretroOpenGLHostDisplayTexture : public HostDisplayTexture
{
public:
  LibretroOpenGLHostDisplayTexture(GL::Texture texture, HostDisplayPixelFormat format)
    : m_texture(std::move(texture)), m_format(format)
  {
  }
  ~LibretroOpenGLHostDisplayTexture() override = default;

  void* GetHandle() const override { return reinterpret_cast<void*>(static_cast<uintptr_t>(m_texture.GetGLId())); }
  u32 GetWidth() const override { return m_texture.GetWidth(); }
  u32 GetHeight() const override { return m_texture.GetHeight(); }
  u32 GetLayers() const override { return 1; }
  u32 GetLevels() const override { return 1; }
  u32 GetSamples() const override { return m_texture.GetSamples(); }
  HostDisplayPixelFormat GetFormat() const override { return m_format; }

  GLuint GetGLID() const { return m_texture.GetGLId(); }

private:
  GL::Texture m_texture;
  HostDisplayPixelFormat m_format;
};

LibretroOpenGLHostDisplay::LibretroOpenGLHostDisplay() = default;

LibretroOpenGLHostDisplay::~LibretroOpenGLHostDisplay() = default;

HostDisplay::RenderAPI LibretroOpenGLHostDisplay::GetRenderAPI() const
{
  return m_is_gles ? HostDisplay::RenderAPI::OpenGLES : HostDisplay::RenderAPI::OpenGL;
}

void* LibretroOpenGLHostDisplay::GetRenderDevice() const
{
  return nullptr;
}

void* LibretroOpenGLHostDisplay::GetRenderContext() const
{
  return nullptr;
}

static constexpr std::array<std::tuple<GLenum, GLenum, GLenum>, static_cast<u32>(HostDisplayPixelFormat::Count)>
  s_display_pixel_format_mapping = {{
    {},                                                  // Unknown
    {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE},               // RGBA8
    {GL_RGBA8, GL_BGRA, GL_UNSIGNED_BYTE},               // BGRA8
    {GL_RGB565, GL_RGB, GL_UNSIGNED_SHORT_5_6_5},        // RGB565
    {GL_RGB5_A1, GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV} // RGBA5551
  }};

std::unique_ptr<HostDisplayTexture> LibretroOpenGLHostDisplay::CreateTexture(u32 width, u32 height, u32 layers,
                                                                             u32 levels, u32 samples,
                                                                             HostDisplayPixelFormat format,
                                                                             const void* data, u32 data_stride,
                                                                             bool dynamic /* = false */)
{
  if (layers != 1 || levels != 1)
    return {};

  const auto [gl_internal_format, gl_format, gl_type] = s_display_pixel_format_mapping[static_cast<u32>(format)];

  // TODO: Set pack width
  Assert(!data || data_stride == (width * sizeof(u32)));

  GL::Texture tex;
  if (!tex.Create(width, height, samples, gl_internal_format, gl_format, gl_type, data, data_stride))
    return {};

  return std::make_unique<LibretroOpenGLHostDisplayTexture>(std::move(tex), format);
}

void LibretroOpenGLHostDisplay::UpdateTexture(HostDisplayTexture* texture, u32 x, u32 y, u32 width, u32 height,
                                              const void* texture_data, u32 texture_data_stride)
{
  LibretroOpenGLHostDisplayTexture* tex = static_cast<LibretroOpenGLHostDisplayTexture*>(texture);
  const auto [gl_internal_format, gl_format, gl_type] =
    s_display_pixel_format_mapping[static_cast<u32>(texture->GetFormat())];
  GLint alignment;
  if (texture_data_stride & 1)
    alignment = 1;
  else if (texture_data_stride & 2)
    alignment = 2;
  else
    alignment = 4;

  GLint old_texture_binding = 0, old_alignment = 0, old_row_length = 0;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_texture_binding);
  glBindTexture(GL_TEXTURE_2D, tex->GetGLID());

  glGetIntegerv(GL_UNPACK_ALIGNMENT, &old_alignment);
  glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);

  glGetIntegerv(GL_UNPACK_ROW_LENGTH, &old_row_length);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, texture_data_stride / GetDisplayPixelFormatSize(texture->GetFormat()));

  glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height, gl_format, gl_type, texture_data);

  glPixelStorei(GL_UNPACK_ROW_LENGTH, old_row_length);

  glPixelStorei(GL_UNPACK_ALIGNMENT, old_alignment);
  glBindTexture(GL_TEXTURE_2D, old_texture_binding);
}

bool LibretroOpenGLHostDisplay::DownloadTexture(const void* texture_handle, HostDisplayPixelFormat texture_format,
                                                u32 x, u32 y, u32 width, u32 height, void* out_data,
                                                u32 out_data_stride)
{
  GLint alignment;
  if (out_data_stride & 1)
    alignment = 1;
  else if (out_data_stride & 2)
    alignment = 2;
  else
    alignment = 4;

  GLint old_alignment = 0, old_row_length = 0;
  glGetIntegerv(GL_PACK_ALIGNMENT, &old_alignment);
  glPixelStorei(GL_PACK_ALIGNMENT, alignment);

  glGetIntegerv(GL_PACK_ROW_LENGTH, &old_row_length);
  glPixelStorei(GL_PACK_ROW_LENGTH, out_data_stride / GetDisplayPixelFormatSize(texture_format));

  const GLuint texture = static_cast<GLuint>(reinterpret_cast<uintptr_t>(texture_handle));
  const auto [gl_internal_format, gl_format, gl_type] =
    s_display_pixel_format_mapping[static_cast<u32>(texture_format)];

  GL::Texture::GetTextureSubImage(texture, 0, x, y, 0, width, height, 1, gl_format, gl_type, height * out_data_stride,
                                  out_data);

  glPixelStorei(GL_PACK_ALIGNMENT, old_alignment);
  glPixelStorei(GL_PACK_ROW_LENGTH, old_row_length);
  return true;
}

bool LibretroOpenGLHostDisplay::SupportsDisplayPixelFormat(HostDisplayPixelFormat format) const
{
  return (std::get<0>(s_display_pixel_format_mapping[static_cast<u32>(format)]) != static_cast<GLenum>(0));
}

bool LibretroOpenGLHostDisplay::BeginSetDisplayPixels(HostDisplayPixelFormat format, u32 width, u32 height,
                                                      void** out_buffer, u32* out_pitch)
{
  const u32 pixel_size = GetDisplayPixelFormatSize(format);
  const u32 stride = Common::AlignUpPow2(width * pixel_size, 4);
  const u32 size_required = stride * height * pixel_size;

  const u32 buffer_size = Common::AlignUpPow2(size_required * 2, 4 * 1024 * 1024);
  if (!m_display_pixels_texture_pbo || m_display_pixels_texture_pbo->GetSize() < buffer_size)
  {
    m_display_pixels_texture_pbo.reset();
    m_display_pixels_texture_pbo = GL::StreamBuffer::Create(GL_PIXEL_UNPACK_BUFFER, buffer_size);
    if (!m_display_pixels_texture_pbo)
      return false;
  }

  const auto map = m_display_pixels_texture_pbo->Map(GetDisplayPixelFormatSize(format), size_required);
  m_display_texture_format = format;
  m_display_pixels_texture_pbo_map_offset = map.buffer_offset;
  m_display_pixels_texture_pbo_map_size = size_required;
  *out_buffer = map.pointer;
  *out_pitch = stride;

  glBindTexture(GL_TEXTURE_2D, m_display_pixels_texture_id);
  SetDisplayTexture(reinterpret_cast<void*>(static_cast<uintptr_t>(m_display_pixels_texture_id)), format, width, height,
                    0, 0, width, height);
  return true;
}

void LibretroOpenGLHostDisplay::EndSetDisplayPixels()
{
  const u32 width = static_cast<u32>(m_display_texture_view_width);
  const u32 height = static_cast<u32>(m_display_texture_view_height);

  const auto [gl_internal_format, gl_format, gl_type] =
    s_display_pixel_format_mapping[static_cast<u32>(m_display_texture_format)];

  glBindTexture(GL_TEXTURE_2D, m_display_pixels_texture_id);

  m_display_pixels_texture_pbo->Unmap(m_display_pixels_texture_pbo_map_size);
  m_display_pixels_texture_pbo->Bind();
  glTexImage2D(GL_TEXTURE_2D, 0, gl_internal_format, width, height, 0, gl_format, gl_type,
               reinterpret_cast<void*>(static_cast<uintptr_t>(m_display_pixels_texture_pbo_map_offset)));
  m_display_pixels_texture_pbo->Unbind();

  m_display_pixels_texture_pbo_map_offset = 0;
  m_display_pixels_texture_pbo_map_size = 0;

  glBindTexture(GL_TEXTURE_2D, 0);
}

bool LibretroOpenGLHostDisplay::SetDisplayPixels(HostDisplayPixelFormat format, u32 width, u32 height,
                                                 const void* buffer, u32 pitch)
{
  glBindTexture(GL_TEXTURE_2D, m_display_pixels_texture_id);

  const auto [gl_internal_format, gl_format, gl_type] = s_display_pixel_format_mapping[static_cast<u32>(format)];
  const u32 pixel_size = GetDisplayPixelFormatSize(format);
  const bool is_packed_tightly = (pitch == (pixel_size * width));

  // If we have GLES3, we can set row_length.
  if (!is_packed_tightly)
    glPixelStorei(GL_UNPACK_ROW_LENGTH, pitch / pixel_size);

  glTexImage2D(GL_TEXTURE_2D, 0, gl_internal_format, width, height, 0, gl_format, gl_type, buffer);

  if (!is_packed_tightly)
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

  glBindTexture(GL_TEXTURE_2D, 0);

  SetDisplayTexture(reinterpret_cast<void*>(static_cast<uintptr_t>(m_display_pixels_texture_id)), format, width, height,
                    0, 0, width, height);
  return true;
}

const char* LibretroOpenGLHostDisplay::GetGLSLVersionString() const
{
  if (GetRenderAPI() == RenderAPI::OpenGLES)
  {
    if (GLAD_GL_ES_VERSION_3_0)
      return "#version 300 es";
    else
      return "#version 100";
  }
  else
  {
    if (GLAD_GL_VERSION_3_3)
      return "#version 330";
    else
      return "#version 130";
  }
}

std::string LibretroOpenGLHostDisplay::GetGLSLVersionHeader() const
{
  std::string header = GetGLSLVersionString();
  header += "\n\n";
  if (GetRenderAPI() == RenderAPI::OpenGLES)
  {
    header += "precision highp float;\n";
    header += "precision highp int;\n\n";
  }

  return header;
}

static void APIENTRY GLDebugCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length,
                                     const GLchar* message, const void* userParam)
{
  switch (severity)
  {
    case GL_DEBUG_SEVERITY_HIGH_KHR:
      Log_ErrorPrint(message);
      break;
    case GL_DEBUG_SEVERITY_MEDIUM_KHR:
      Log_WarningPrint(message);
      break;
    case GL_DEBUG_SEVERITY_LOW_KHR:
      Log_InfoPrint(message);
      break;
    case GL_DEBUG_SEVERITY_NOTIFICATION:
      // Log_DebugPrint(message);
      break;
  }
}

bool LibretroOpenGLHostDisplay::HasRenderDevice() const
{
  return true;
}

bool LibretroOpenGLHostDisplay::HasRenderSurface() const
{
  return true;
}

static bool TryDesktopVersions(retro_hw_render_callback* cb)
{
  static constexpr std::array<std::tuple<u32, u32>, 11> desktop_versions_to_try = {
    {/*{4, 6}, {4, 5}, {4, 4}, {4, 3}, {4, 2}, {4, 1}, {4, 0}, */ {3, 3}, {3, 2}, {3, 1}, {3, 0}}};

  for (const auto& [major, minor] : desktop_versions_to_try)
  {
    if (major > 3 || (major == 3 && minor >= 2))
    {
      cb->context_type = RETRO_HW_CONTEXT_OPENGL_CORE;
      cb->version_major = major;
      cb->version_minor = minor;
    }
    else
    {
      cb->context_type = RETRO_HW_CONTEXT_OPENGL;
      cb->version_major = 0;
      cb->version_minor = 0;
    }

    if (g_retro_environment_callback(RETRO_ENVIRONMENT_SET_HW_RENDER, cb))
      return true;
  }

  return false;
}

static bool TryESVersions(retro_hw_render_callback* cb)
{
  static constexpr std::array<std::tuple<u32, u32>, 4> es_versions_to_try = {{{3, 2}, {3, 1}, {3, 0}}};

  for (const auto& [major, minor] : es_versions_to_try)
  {
    if (major >= 3 && minor > 0)
    {
      cb->context_type = RETRO_HW_CONTEXT_OPENGLES_VERSION;
      cb->version_major = major;
      cb->version_minor = minor;
    }
    else
    {
      cb->context_type = RETRO_HW_CONTEXT_OPENGLES3;
      cb->version_major = 0;
      cb->version_minor = 0;
    }

    if (g_retro_environment_callback(RETRO_ENVIRONMENT_SET_HW_RENDER, cb))
      return true;
  }

  return false;
}

bool LibretroOpenGLHostDisplay::RequestHardwareRendererContext(retro_hw_render_callback* cb, bool prefer_gles)
{
  // Prefer a desktop OpenGL context where possible. If we can't get this, try OpenGL ES.
  cb->cache_context = false;
  cb->bottom_left_origin = true;

  if (!prefer_gles)
  {
    if (TryDesktopVersions(cb) || TryESVersions(cb))
      return true;
  }
  else
  {
    if (TryESVersions(cb) || TryDesktopVersions(cb))
      return true;
  }

  Log_ErrorPrint("Failed to set any GL HW renderer");
  return false;
}

bool LibretroOpenGLHostDisplay::CreateRenderDevice(const WindowInfo& wi, std::string_view adapter_name,
                                                   bool debug_device, bool threaded_presentation)
{
  Assert(wi.type == WindowInfo::Type::Libretro);

  // gross - but can't do much because of the GLADloadproc below.
  static retro_hw_render_callback* cb;
  cb = static_cast<retro_hw_render_callback*>(wi.display_connection);

  m_window_info = wi;
  m_is_gles = (cb->context_type == RETRO_HW_CONTEXT_OPENGLES3 || cb->context_type == RETRO_HW_CONTEXT_OPENGLES_VERSION);

  const GLADloadproc get_proc_address = [](const char* sym) -> void* {
    return reinterpret_cast<void*>(cb->get_proc_address(sym));
  };

  // Load GLAD.
  const auto load_result = m_is_gles ? gladLoadGLES2Loader(get_proc_address) : gladLoadGLLoader(get_proc_address);
  if (!load_result)
  {
    Log_ErrorPrintf("Failed to load GL functions");
    return false;
  }

  return true;
}

bool LibretroOpenGLHostDisplay::InitializeRenderDevice(std::string_view shader_cache_directory, bool debug_device,
                                                       bool threaded_presentation)
{
  glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, reinterpret_cast<GLint*>(&m_uniform_buffer_alignment));

  if (debug_device && GLAD_GL_KHR_debug)
  {
    if (GetRenderAPI() == RenderAPI::OpenGLES)
      glDebugMessageCallbackKHR(GLDebugCallback, nullptr);
    else
      glDebugMessageCallback(GLDebugCallback, nullptr);

    glEnable(GL_DEBUG_OUTPUT);
  }

  if (!CreateResources())
    return false;
  return true;
}

bool LibretroOpenGLHostDisplay::MakeRenderContextCurrent()
{
  return true;
}

bool LibretroOpenGLHostDisplay::DoneRenderContextCurrent()
{
  return true;
}

void LibretroOpenGLHostDisplay::DestroyRenderDevice()
{
  ClearSoftwareCursor();
  DestroyResources();
}

void LibretroOpenGLHostDisplay::ResizeRenderWindow(s32 new_window_width, s32 new_window_height)
{
  m_window_info.surface_width = static_cast<u32>(new_window_width);
  m_window_info.surface_height = static_cast<u32>(new_window_height);
}

bool LibretroOpenGLHostDisplay::ChangeRenderWindow(const WindowInfo& new_wi)
{
  m_window_info = new_wi;
  return true;
}

void LibretroOpenGLHostDisplay::DestroyRenderSurface() {}

bool LibretroOpenGLHostDisplay::CreateResources()
{
  static constexpr char fullscreen_quad_vertex_shader[] = R"(
uniform vec4 u_src_rect;
out vec2 v_tex0;

void main()
{
  vec2 pos = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
  v_tex0 = u_src_rect.xy + pos * u_src_rect.zw;
  gl_Position = vec4(pos * vec2(2.0f, -2.0f) + vec2(-1.0f, 1.0f), 0.0f, 1.0f);
}
)";

  static constexpr char display_fragment_shader[] = R"(
uniform sampler2D samp0;

in vec2 v_tex0;
out vec4 o_col0;

void main()
{
  o_col0 = vec4(texture(samp0, v_tex0).rgb, 1.0);
}
)";

  static constexpr char cursor_fragment_shader[] = R"(
uniform sampler2D samp0;

in vec2 v_tex0;
out vec4 o_col0;

void main()
{
  o_col0 = texture(samp0, v_tex0);
}
)";

  if (!m_display_program.Compile(GetGLSLVersionHeader() + fullscreen_quad_vertex_shader, {},
                                 GetGLSLVersionHeader() + display_fragment_shader) ||
      !m_cursor_program.Compile(GetGLSLVersionHeader() + fullscreen_quad_vertex_shader, {},
                                GetGLSLVersionHeader() + cursor_fragment_shader))
  {
    Log_ErrorPrintf("Failed to compile display shaders");
    return false;
  }

  if (GetRenderAPI() != RenderAPI::OpenGLES)
  {
    m_display_program.BindFragData(0, "o_col0");
    m_cursor_program.BindFragData(0, "o_col0");
  }

  if (!m_display_program.Link() || !m_cursor_program.Link())
  {
    Log_ErrorPrintf("Failed to link display programs");
    return false;
  }

  m_display_program.Bind();
  m_display_program.RegisterUniform("u_src_rect");
  m_display_program.RegisterUniform("samp0");
  m_display_program.Uniform1i(1, 0);
  m_cursor_program.Bind();
  m_cursor_program.RegisterUniform("u_src_rect");
  m_cursor_program.RegisterUniform("samp0");
  m_cursor_program.Uniform1i(1, 0);

  glGenVertexArrays(1, &m_display_vao);

  // samplers
  glGenSamplers(1, &m_display_nearest_sampler);
  glSamplerParameteri(m_display_nearest_sampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glSamplerParameteri(m_display_nearest_sampler, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glGenSamplers(1, &m_display_linear_sampler);
  glSamplerParameteri(m_display_linear_sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glSamplerParameteri(m_display_linear_sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  return true;
}

void LibretroOpenGLHostDisplay::DestroyResources()
{
  if (m_display_pixels_texture_id != 0)
  {
    glDeleteTextures(1, &m_display_pixels_texture_id);
    m_display_pixels_texture_id = 0;
  }

  if (m_display_vao != 0)
  {
    glDeleteVertexArrays(1, &m_display_vao);
    m_display_vao = 0;
  }
  if (m_display_linear_sampler != 0)
  {
    glDeleteSamplers(1, &m_display_linear_sampler);
    m_display_linear_sampler = 0;
  }
  if (m_display_nearest_sampler != 0)
  {
    glDeleteSamplers(1, &m_display_nearest_sampler);
    m_display_nearest_sampler = 0;
  }

  m_cursor_program.Destroy();
  m_display_program.Destroy();
}

bool LibretroOpenGLHostDisplay::Render()
{
  const GLuint fbo = static_cast<GLuint>(
    static_cast<retro_hw_render_callback*>(m_window_info.display_connection)->get_current_framebuffer());
  const u32 resolution_scale = g_libretro_host_interface.GetResolutionScale();
  const u32 display_width = static_cast<u32>(m_display_width) * resolution_scale;
  const u32 display_height = static_cast<u32>(m_display_height) * resolution_scale;

  glEnable(GL_SCISSOR_TEST);
  glScissor(0, 0, display_width, display_height);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
  glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glDisable(GL_SCISSOR_TEST);

  if (HasDisplayTexture())
  {
    const auto [left, top, width, height] = CalculateDrawRect(display_width, display_height, 0, false);
    RenderDisplay(left, top, width, height, m_display_texture_handle, m_display_texture_width, m_display_texture_height,
                  m_display_texture_view_x, m_display_texture_view_y, m_display_texture_view_width,
                  m_display_texture_view_height, m_display_linear_filtering);
  }

  g_retro_video_refresh_callback(RETRO_HW_FRAME_BUFFER_VALID, display_width, display_height, 0);

  GL::Program::ResetLastProgram();
  return true;
}

void LibretroOpenGLHostDisplay::RenderDisplay(s32 left, s32 bottom, s32 width, s32 height, void* texture_handle,
                                              u32 texture_width, s32 texture_height, s32 texture_view_x,
                                              s32 texture_view_y, s32 texture_view_width, s32 texture_view_height,
                                              bool linear_filter)
{
  glViewport(left, bottom, width, height);
  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);
  glDisable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(reinterpret_cast<uintptr_t>(texture_handle)));
  m_display_program.Bind();

  const float position_adjust = m_display_linear_filtering ? 0.5f : 0.0f;
  const float size_adjust = m_display_linear_filtering ? 1.0f : 0.0f;
  const float flip_adjust = (texture_view_height < 0) ? -1.0f : 1.0f;
  m_display_program.Uniform4f(
    0, (static_cast<float>(texture_view_x) + position_adjust) / static_cast<float>(texture_width),
    (static_cast<float>(texture_view_y) + (position_adjust * flip_adjust)) / static_cast<float>(texture_height),
    (static_cast<float>(texture_view_width) - size_adjust) / static_cast<float>(texture_width),
    (static_cast<float>(texture_view_height) - (size_adjust * flip_adjust)) / static_cast<float>(texture_height));
  glBindSampler(0, linear_filter ? m_display_linear_sampler : m_display_nearest_sampler);
  glBindVertexArray(m_display_vao);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindSampler(0, 0);
}
