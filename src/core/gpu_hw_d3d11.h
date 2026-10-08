#pragma once
#include "common/d3d11/staging_texture.h"
#include "common/d3d11/stream_buffer.h"
#include "common/d3d11/texture.h"
#include "common/lockfree.h"
#include "gpu_hw.h"
#include "host_display.h"
#include "texture_replacements.h"
#include <array>
#include <d3d11.h>
#include <memory>
#include <rthreads/rthreads.h>
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

  void ResizeRenderWindow(int32_t new_window_width, int32_t new_window_height) override;

  bool ChangeRenderWindow(const WindowInfo& new_wi) override;

  std::unique_ptr<HostDisplayTexture> CreateTexture(uint32_t width, uint32_t height, uint32_t layers, uint32_t levels, uint32_t samples,
                                                    HostDisplayPixelFormat format, const void* data, uint32_t data_stride,
                                                    bool dynamic = false) override;
  bool SupportsDisplayPixelFormat(HostDisplayPixelFormat format) const override;
  bool BeginSetDisplayPixels(HostDisplayPixelFormat format, uint32_t width, uint32_t height, void** out_buffer,
                             uint32_t* out_pitch) override;
  void EndSetDisplayPixels() override;

  bool Render() override;

protected:
  bool CreateResources() override;
  void DestroyResources() override;
  void RenderSoftwareCursor(int32_t left, int32_t top, int32_t width, int32_t height, HostDisplayTexture* texture_handle);

  void RenderDisplay(int32_t left, int32_t top, int32_t width, int32_t height, void* texture_handle, uint32_t texture_width,
                     int32_t texture_height, int32_t texture_view_x, int32_t texture_view_y, int32_t texture_view_width,
                     int32_t texture_view_height);

private:
  static constexpr uint32_t DISPLAY_UNIFORM_BUFFER_SIZE = 16;

  template<typename T>
  using ComPtr = Microsoft::WRL::ComPtr<T>;

  bool CheckFramebufferSize(uint32_t width, uint32_t height);

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

  bool Initialize(HostDisplay* host_display) override;
  void Reset(bool clear_vram) override;
  bool DoState(StateWrapper& sw, HostDisplayTexture** host_texture, bool update_display) override;

  void ResetGraphicsAPIState() override;
  void RestoreGraphicsAPIState() override;
  void UpdateSettings() override;

protected:
  void ClearDisplay() override;
  void UpdateDisplay() override;
  void ReadVRAM(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;
  void FillVRAM(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color) override;
  void UpdateVRAM(uint32_t x, uint32_t y, uint32_t width, uint32_t height, const void* data, bool set_mask, bool check_mask) override;
  void CopyVRAM(uint32_t src_x, uint32_t src_y, uint32_t dst_x, uint32_t dst_y, uint32_t width, uint32_t height) override;
  void UpdateVRAMReadTexture() override;
  void UpdateDepthBufferFromMaskBit() override;
  void ClearDepthBuffer() override;
  void SetScissorFromDrawingArea() override;
  void MapBatchVertexPointer(uint32_t required_vertices) override;
  void UnmapBatchVertexPointer(uint32_t used_vertices) override;
  void UploadUniformBuffer(const void* data, uint32_t data_size) override;
  void DrawBatchVertices(BatchRenderMode render_mode, uint32_t base_vertex, uint32_t num_vertices) override;

private:
  // Currently we don't stream uniforms, instead just re-map the buffer every time and let the driver take care of it.
  static constexpr uint32_t MAX_UNIFORM_BUFFER_SIZE = 64;

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
  bool CompileDownsampleShaders();
  void DestroyShaders();
  bool RebuildDisplayPixelShaders();

  // Fast-path companion to CompileShaders for the dim-cache
  // filter-only flip in UpdateSettings: walks just the current
  // m_texture_filtering sub-cube of m_batch_pixel_shader_fastpath and
  // launches the Lazy worker, without touching the filter-
  // independent non-batch shaders (input layout, vertex shaders,
  // copy / VRAM ops / display / downsample pixel shaders). All
  // those stay valid through a filter toggle because none of them
  // bake filter into their HLSL source - filter only affects the
  // batch FS via FilteredSampleFromVRAM. Caller must have already
  // joined any previous worker (StopShaderCompileThread) and
  // populated m_shader_cache + m_shadergen. Called from
  // CompileShaders itself for the initial / full-rebuild path, and
  // directly from UpdateSettings's only_dim_changed branch to
  // bypass the non-batch rebuild on warm filter cycling.
  bool PrecompileBatchShaders(ShaderCompileProgressTracker& progress);

  // Lazy batch-fragment-shader compile path. The shader matrix
  // starts all-null; GetBatchPixelShader fills a slot on demand
  // (the first time the game dispatches a draw using that
  // combination) and returns a non-owning pointer to the underlying
  // ID3D11PixelShader. It also handles the texture-mode dedup
  // (3->2 and 7->6) inline so the duplicate slots share their
  // canonical ComPtr.
  //
  // Two-level synchronisation:
  //   - FAST path (slot already filled): a single atomic
  //     acquire-load from m_batch_pixel_shader_fastpath. No mutex,
  //     no kernel call, no serialisation against the background
  //     precompile worker. This is what DrawBatchVertices hits in
  //     steady state.
  //   - SLOW path (slot null after the atomic load): picks the
  //     pre-baked DXBC blob for this (filter, render_mode,
  //     texture_mode) cell from D3DCommon::EmbeddedShaders and
  //     wraps it into an ID3D11PixelShader via
  //     D3D11::ShaderCompiler::CreatePixelShader, which is
  //     free-threaded, then publishes it with a compare-exchange on
  //     the slot. A race loser drops its own copy.
  //
  // ID3D11PixelShader* itself is free-threaded for the consumer
  // side (PSSetShader), so DrawBatchVertices can use the raw
  // pointer returned here without further locking.
  //
  // filter is the outermost cache dimension. With the cbuffer-
  // refactor patch (7b575a3) the HLSL is invariant under
  // resolution_scale / true_color / scaled_dithering, but it is
  // STILL dependent on texture filter - the FilteredSampleFromVRAM
  // helper is emitted differently for Nearest / Bilinear / JINC2 /
  // xBR / the BinAlpha variants. Dimensioning the cache over filter
  // lets a filter toggle skip the DestroyShaders round trip in
  // UpdateSettings: the previous filter's sub-cube remains valid
  // and reachable, switching back to it later is an atomic load on
  // an already-filled slot. Mirrors the D3D12 dim cache landed in
  // 10c53b8 and the Vulkan dim cache from the glslang-elimination
  // series. The slow path picks the pre-baked DXBC for the
  // requested filter directly, so a worker / main-thread fault for
  // a non-current filter's sub-cube wraps that filter's blob, not
  // m_texture_filtering's.
  ID3D11PixelShader* GetBatchPixelShader(GPUTextureFilter filter, uint8_t render_mode, uint8_t texture_mode, bool dithering, bool interlacing);

  // Background-thread worker for 'Lazy' mode: walks the entire
  // (render_mode, texture_mode, dithering, interlacing) matrix and
  // calls GetBatchPixelShader on each slot. The main thread can
  // race ahead and pre-populate any slots it actually needs; the
  // worker just skips already-filled slots.
  void ShaderCompileThreadEntryPoint();
  void StartShaderCompileThread();
  void StopShaderCompileThread();

  void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height);
  void SetScissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height);
  void SetViewportAndScissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height);

  void DrawUtilityShader(ID3D11PixelShader* shader, const void* uniforms, uint32_t uniforms_size);

  bool BlitVRAMReplacementTexture(const TextureReplacementTexture* tex, uint32_t dst_x, uint32_t dst_y, uint32_t width, uint32_t height);

  void DownsampleFramebuffer(D3D11::Texture& source, uint32_t left, uint32_t top, uint32_t width, uint32_t height);
  void DownsampleFramebufferAdaptive(D3D11::Texture& source, uint32_t left, uint32_t top, uint32_t width, uint32_t height);
  void DownsampleFramebufferBoxFilter(D3D11::Texture& source, uint32_t left, uint32_t top, uint32_t width, uint32_t height);

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

  // Batch pixel shader matrix. Filter is the outermost dimension -
  // see the comment on GetBatchPixelShader above for why.
  //
  // [filter][render_mode][texture_mode][dithering][interlacing]
  //
  // Each slot owns one reference to its shader and is filled once by
  // compare-exchange, so the draw path reads it with one acquire load
  // and never waits on the background precompile worker. The
  // Reserved_*Direct16Bit texture modes share their canonical slot.
  std::array<std::array<std::array<std::array<std::array<AtomicSlot<ID3D11PixelShader*>, 2>, 2>, 9>, 4>, 7>
    m_batch_pixel_shader_fastpath; // [filter][render_mode][texture_mode][dithering][interlacing]

  sthread_t* m_shader_compile_thread = nullptr;
  retro_atomic_int_t m_shader_compile_thread_quit = {};

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
