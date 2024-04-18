#pragma once

// GLAD has to come first so that Qt doesn't pull in the system GL headers, which are incompatible with glad.
#include <glad.h>

// Hack to prevent Apple's glext.h headers from getting included via qopengl.h, since we still want to use glad.
#ifdef __APPLE__
#define __glext_h_
#endif

#include "common/gl/program.h"
#include "common/gl/shader_cache.h"
#include "common/gl/stream_buffer.h"
#include "common/gl/texture.h"
#include "core/host_display.h"
#include "glad.h"
#include "gpu_hw.h"
#include "texture_replacements.h"
#include <array>
#include <memory>
#include <tuple>
#include <string>
#include <libretro.h>

class LibretroOpenGLHostDisplay final : public HostDisplay
{
public:
  LibretroOpenGLHostDisplay();
  ~LibretroOpenGLHostDisplay();

  static bool RequestHardwareRendererContext(retro_hw_render_callback* cb, bool prefer_gles);

  RenderAPI GetRenderAPI() const override;
  void* GetRenderDevice() const override;
  void* GetRenderContext() const override;

  bool CreateRenderDevice(const WindowInfo& wi, std::string_view adapter_name, bool debug_device,
                          bool threaded_presentation) override;
  bool InitializeRenderDevice(std::string_view shader_cache_directory, bool debug_device,
                              bool threaded_presentation) override;
  void DestroyRenderDevice() override;

  void ResizeRenderWindow(s32 new_window_width, s32 new_window_height) override;

  bool ChangeRenderWindow(const WindowInfo& new_wi) override;

  std::unique_ptr<HostDisplayTexture> CreateTexture(u32 width, u32 height, u32 layers, u32 levels, u32 samples,
                                                    HostDisplayPixelFormat format, const void* data, u32 data_stride,
                                                    bool dynamic = false) override;
  bool SupportsDisplayPixelFormat(HostDisplayPixelFormat format) const override;
  bool BeginSetDisplayPixels(HostDisplayPixelFormat format, u32 width, u32 height, void** out_buffer,
                             u32* out_pitch) override;
  void EndSetDisplayPixels() override;
  bool SetDisplayPixels(HostDisplayPixelFormat format, u32 width, u32 height, const void* buffer, u32 pitch) override;

  bool Render() override;

protected:
  bool CreateResources() override;
  void DestroyResources() override;
  void RenderSoftwareCursor() override;
  void RenderSoftwareCursor(s32 left, s32 top, s32 width, s32 height, HostDisplayTexture* texture_handle);

  void RenderDisplay(s32 left, s32 bottom, s32 width, s32 height, void* texture_handle, u32 texture_width,
                     s32 texture_height, s32 texture_view_x, s32 texture_view_y, s32 texture_view_width,
                     s32 texture_view_height);

private:
  const char* GetGLSLVersionString() const;
  std::string GetGLSLVersionHeader() const;

  GL::Program m_display_program;
  GL::Program m_cursor_program;
  GLuint m_display_vao = 0;
  GLuint m_display_nearest_sampler = 0;
  GLuint m_display_linear_sampler = 0;
  GLuint m_uniform_buffer_alignment = 1;

  GLuint m_display_pixels_texture_id = 0;
  std::unique_ptr<GL::StreamBuffer> m_display_pixels_texture_pbo;
  u32 m_display_pixels_texture_pbo_map_offset = 0;
  u32 m_display_pixels_texture_pbo_map_size = 0;
  std::vector<u8> m_gles_pixels_repack_buffer;

  bool m_is_gles = false;
};

class GPU_HW_OpenGL : public GPU_HW
{
public:
  GPU_HW_OpenGL();
  ~GPU_HW_OpenGL() override;

  GPURenderer GetRendererType() const override;

  bool Initialize(HostDisplay* host_display) override;
  void Reset(bool clear_vram) override;
  bool DoState(StateWrapper& sw, HostDisplayTexture** host_texture, bool update_display) override;

  void ResetGraphicsAPIState() override;
  void RestoreGraphicsAPIState() override;
  void UpdateSettings() override;

protected:
  void ClearDisplay() override;
  void UpdateDisplay() override;
  void ReadVRAM(u32 x, u32 y, u32 width, u32 height) override;
  void FillVRAM(u32 x, u32 y, u32 width, u32 height, u32 color) override;
  void UpdateVRAM(u32 x, u32 y, u32 width, u32 height, const void* data, bool set_mask, bool check_mask) override;
  void CopyVRAM(u32 src_x, u32 src_y, u32 dst_x, u32 dst_y, u32 width, u32 height) override;
  void UpdateVRAMReadTexture() override;
  void UpdateDepthBufferFromMaskBit() override;
  void ClearDepthBuffer() override;
  void SetScissorFromDrawingArea() override;
  void MapBatchVertexPointer(u32 required_vertices) override;
  void UnmapBatchVertexPointer(u32 used_vertices) override;
  void UploadUniformBuffer(const void* data, u32 data_size) override;
  void DrawBatchVertices(BatchRenderMode render_mode, u32 base_vertex, u32 num_vertices) override;

private:
  struct GLStats
  {
    u32 num_batches;
    u32 num_vertices;
    u32 num_vram_reads;
    u32 num_vram_writes;
    u32 num_vram_read_texture_updates;
    u32 num_uniform_buffer_updates;
  };

  ALWAYS_INLINE bool IsGLES() const { return (m_render_api == HostDisplay::RenderAPI::OpenGLES); }

  std::tuple<s32, s32> ConvertToFramebufferCoordinates(s32 x, s32 y);

  void SetCapabilities();
  bool CreateFramebuffer();
  void ClearFramebuffer();
  void CopyFramebufferForState(GLenum target, GLuint src_texture, u32 src_fbo, u32 src_x, u32 src_y, GLuint dst_texture,
                               u32 dst_fbo, u32 dst_x, u32 dst_y, u32 width, u32 height);

  bool CreateVertexBuffer();
  bool CreateUniformBuffer();
  bool CreateTextureBuffer();

  bool CompilePrograms();

  void SetDepthFunc();
  void SetDepthFunc(GLenum func);
  void SetBlendMode();

  bool BlitVRAMReplacementTexture(const TextureReplacementTexture* tex, u32 dst_x, u32 dst_y, u32 width, u32 height);
  void DownsampleFramebuffer(GL::Texture& source, u32 left, u32 top, u32 width, u32 height);
  void DownsampleFramebufferBoxFilter(GL::Texture& source, u32 left, u32 top, u32 width, u32 height);

  // downsample texture - used for readbacks at >1xIR.
  GL::Texture m_vram_texture;
  GL::Texture m_vram_depth_texture;
  GL::Texture m_vram_read_texture;
  GL::Texture m_vram_encoding_texture;
  GL::Texture m_display_texture;
  GL::Texture m_vram_write_replacement_texture;

  std::unique_ptr<GL::StreamBuffer> m_vertex_stream_buffer;
  GLuint m_vram_fbo_id = 0;
  GLuint m_vao_id = 0;
  GLuint m_attributeless_vao_id = 0;
  GLuint m_state_copy_fbo_id = 0;

  std::unique_ptr<GL::StreamBuffer> m_uniform_stream_buffer;

  std::unique_ptr<GL::StreamBuffer> m_texture_stream_buffer;
  GLuint m_texture_buffer_r16ui_texture = 0;

  std::array<std::array<std::array<std::array<GL::Program, 2>, 2>, 9>, 4>
    m_render_programs;                                          // [render_mode][texture_mode][dithering][interlacing]
  std::array<std::array<GL::Program, 3>, 2> m_display_programs; // [depth_24][interlaced]
  std::array<std::array<GL::Program, 2>, 2> m_vram_fill_programs;
  GL::Program m_vram_read_program;
  GL::Program m_vram_write_program;
  GL::Program m_vram_copy_program;
  GL::Program m_vram_update_depth_program;

  u32 m_uniform_buffer_alignment = 1;
  u32 m_texture_stream_buffer_size = 0;

  bool m_use_texture_buffer_for_vram_writes = false;
  bool m_use_ssbo_for_vram_writes = false;

  GLenum m_current_depth_test = 0;
  GPUTransparencyMode m_current_transparency_mode = GPUTransparencyMode::Disabled;
  BatchRenderMode m_current_render_mode = BatchRenderMode::TransparencyDisabled;

  GL::Texture m_downsample_texture;
  GL::Program m_downsample_program;
};
