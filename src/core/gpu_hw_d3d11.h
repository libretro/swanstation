#pragma once
#include "common/d3d11/shader_cache.h"
#include "common/d3d11/staging_texture.h"
#include "common/d3d11/stream_buffer.h"
#include "common/d3d11/texture.h"
#include "gpu_hw.h"
#include "host_display.h"
#include "texture_replacements.h"
#include <array>
#include <d3d11.h>
#include <memory>
#include <tuple>
#include <wrl/client.h>
#include <libretro.h>

class LibretroD3D11HostDisplay final : public HostDisplay
{
public:
  LibretroD3D11HostDisplay();
  ~LibretroD3D11HostDisplay();

  static bool RequestHardwareRendererContext(retro_hw_render_callback* cb);

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

  bool Render() override;

protected:
  bool CreateResources() override;
  void DestroyResources() override;
  void RenderSoftwareCursor() override;
  void RenderSoftwareCursor(s32 left, s32 top, s32 width, s32 height, HostDisplayTexture* texture_handle);

  void RenderDisplay(s32 left, s32 top, s32 width, s32 height, void* texture_handle, u32 texture_width,
                     s32 texture_height, s32 texture_view_x, s32 texture_view_y, s32 texture_view_width,
                     s32 texture_view_height);

private:
  static constexpr u32 DISPLAY_UNIFORM_BUFFER_SIZE = 16;

  template<typename T>
  using ComPtr = Microsoft::WRL::ComPtr<T>;

  bool CheckFramebufferSize(u32 width, u32 height);

  ComPtr<ID3D11Device> m_device;
  ComPtr<ID3D11DeviceContext> m_context;

  ComPtr<ID3D11RasterizerState> m_display_rasterizer_state;
  ComPtr<ID3D11DepthStencilState> m_display_depth_stencil_state;
  ComPtr<ID3D11BlendState> m_display_blend_state;
  ComPtr<ID3D11BlendState> m_software_cursor_blend_state;
  ComPtr<ID3D11VertexShader> m_display_vertex_shader;
  ComPtr<ID3D11PixelShader> m_display_pixel_shader;
  ComPtr<ID3D11PixelShader> m_display_alpha_pixel_shader;
  ComPtr<ID3D11SamplerState> m_point_sampler;
  ComPtr<ID3D11SamplerState> m_linear_sampler;

  D3D11::Texture m_display_pixels_texture;
  D3D11::StreamBuffer m_display_uniform_buffer;
  D3D11::AutoStagingTexture m_readback_staging_texture;

  D3D11::Texture m_framebuffer;
};

class GPU_HW_D3D11 : public GPU_HW
{
public:
  template<typename T>
  using ComPtr = Microsoft::WRL::ComPtr<T>;

  GPU_HW_D3D11();
  ~GPU_HW_D3D11() override;

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
  // Currently we don't stream uniforms, instead just re-map the buffer every time and let the driver take care of it.
  static constexpr u32 MAX_UNIFORM_BUFFER_SIZE = 64;

  void SetCapabilities();
  bool CreateFramebuffer();
  void ClearFramebuffer();
  void DestroyFramebuffer();

  bool CreateVertexBuffer();
  bool CreateUniformBuffer();
  bool CreateTextureBuffer();
  bool CreateStateObjects();
  void DestroyStateObjects();

  bool CompileShaders();
  void DestroyShaders();
  void SetViewport(u32 x, u32 y, u32 width, u32 height);
  void SetScissor(u32 x, u32 y, u32 width, u32 height);
  void SetViewportAndScissor(u32 x, u32 y, u32 width, u32 height);

  void DrawUtilityShader(ID3D11PixelShader* shader, const void* uniforms, u32 uniforms_size);

  bool BlitVRAMReplacementTexture(const TextureReplacementTexture* tex, u32 dst_x, u32 dst_y, u32 width, u32 height);

  void DownsampleFramebuffer(D3D11::Texture& source, u32 left, u32 top, u32 width, u32 height);
  void DownsampleFramebufferAdaptive(D3D11::Texture& source, u32 left, u32 top, u32 width, u32 height);
  void DownsampleFramebufferBoxFilter(D3D11::Texture& source, u32 left, u32 top, u32 width, u32 height);

  ComPtr<ID3D11Device> m_device;
  ComPtr<ID3D11DeviceContext> m_context;

  // downsample texture - used for readbacks at >1xIR.
  D3D11::Texture m_vram_texture;
  D3D11::Texture m_vram_depth_texture;
  ComPtr<ID3D11DepthStencilView> m_vram_depth_view;
  D3D11::Texture m_vram_read_texture;
  D3D11::Texture m_vram_encoding_texture;
  D3D11::Texture m_display_texture;

  D3D11::StreamBuffer m_vertex_stream_buffer;

  D3D11::StreamBuffer m_uniform_stream_buffer;

  D3D11::StreamBuffer m_texture_stream_buffer;

  D3D11::StagingTexture m_vram_readback_texture;

  ComPtr<ID3D11ShaderResourceView> m_texture_stream_buffer_srv_r16ui;

  ComPtr<ID3D11RasterizerState> m_cull_none_rasterizer_state;
  ComPtr<ID3D11RasterizerState> m_cull_none_rasterizer_state_no_msaa;

  ComPtr<ID3D11DepthStencilState> m_depth_disabled_state;
  ComPtr<ID3D11DepthStencilState> m_depth_test_always_state;
  ComPtr<ID3D11DepthStencilState> m_depth_test_less_state;
  ComPtr<ID3D11DepthStencilState> m_depth_test_greater_state;

  ComPtr<ID3D11BlendState> m_blend_disabled_state;
  ComPtr<ID3D11BlendState> m_blend_no_color_writes_state;

  ComPtr<ID3D11SamplerState> m_point_sampler_state;
  ComPtr<ID3D11SamplerState> m_linear_sampler_state;
  ComPtr<ID3D11SamplerState> m_trilinear_sampler_state;

  std::array<ComPtr<ID3D11BlendState>, 5> m_batch_blend_states; // [transparency_mode]
  ComPtr<ID3D11InputLayout> m_batch_input_layout;
  std::array<ComPtr<ID3D11VertexShader>, 2> m_batch_vertex_shaders; // [textured]
  std::array<std::array<std::array<std::array<ComPtr<ID3D11PixelShader>, 2>, 2>, 9>, 4>
    m_batch_pixel_shaders; // [render_mode][texture_mode][dithering][interlacing]

  ComPtr<ID3D11VertexShader> m_screen_quad_vertex_shader;
  ComPtr<ID3D11VertexShader> m_uv_quad_vertex_shader;
  ComPtr<ID3D11PixelShader> m_copy_pixel_shader;
  std::array<std::array<ComPtr<ID3D11PixelShader>, 2>, 2> m_vram_fill_pixel_shaders;  // [wrapped][interlaced]
  ComPtr<ID3D11PixelShader> m_vram_read_pixel_shader;
  ComPtr<ID3D11PixelShader> m_vram_write_pixel_shader;
  ComPtr<ID3D11PixelShader> m_vram_copy_pixel_shader;
  ComPtr<ID3D11PixelShader> m_vram_update_depth_pixel_shader;
  std::array<std::array<ComPtr<ID3D11PixelShader>, 3>, 2> m_display_pixel_shaders; // [depth_24][interlaced]

  D3D11::Texture m_vram_replacement_texture;

  // downsampling
  ComPtr<ID3D11PixelShader> m_downsample_first_pass_pixel_shader;
  ComPtr<ID3D11PixelShader> m_downsample_mid_pass_pixel_shader;
  ComPtr<ID3D11PixelShader> m_downsample_blur_pass_pixel_shader;
  ComPtr<ID3D11PixelShader> m_downsample_composite_pixel_shader;
  D3D11::Texture m_downsample_texture;
  D3D11::Texture m_downsample_weight_texture;
  std::vector<std::pair<ComPtr<ID3D11ShaderResourceView>, ComPtr<ID3D11RenderTargetView>>> m_downsample_mip_views;
};
