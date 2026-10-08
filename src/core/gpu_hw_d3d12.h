#pragma once
#include "common/d3d12/shader_cache.h"
#include "common/d3d12/staging_texture.h"
#include "common/d3d12/stream_buffer.h"
#include "common/d3d12/texture.h"
#include "common/dimensional_array.h"
#include "common/lockfree.h"
#include "gpu_hw.h"
#include "host_display.h"
#include "texture_replacements.h"
#include <array>
#include <d3d12.h>
#include <memory>
#include <rthreads/rthreads.h>
#include <tuple>
#include <vector>
#include <wrl/client.h>
#include <libretro.h>

class LibretroD3D12HostDisplay final : public HostDisplay
{
public:
  LibretroD3D12HostDisplay();
  ~LibretroD3D12HostDisplay();

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

private:
  template<typename T>
  using ComPtr = Microsoft::WRL::ComPtr<T>;

  bool CheckFramebufferSize(uint32_t width, uint32_t height);

  // Composite the (view_x, view_y, view_width, view_height) sub-rect of the
  // source display texture into m_framebuffer at the (left, top, width,
  // height) draw rect, sampling via the copy pixel shader. Mirrors
  // LibretroD3D11HostDisplay::RenderDisplay / the Vulkan equivalent so the
  // frontend always receives a (0,0)-origin image regardless of where the
  // source content sits in its texture.
  void RenderDisplay(int32_t left, int32_t top, int32_t width, int32_t height, D3D12::Texture* texture,
                     int32_t texture_width, int32_t texture_height, int32_t view_x, int32_t view_y,
                     int32_t view_width, int32_t view_height);

  // retro_hw_render_interface_d3d12::set_texture callback - the frontend
  // reads from this resource in m_required_state to compose the final
  // output frame. We hand it the framebuffer texture (post our display
  // blit) at the end of Render().
  void (*m_set_texture)(void* handle, ID3D12Resource* texture, DXGI_FORMAT format) = nullptr;
  void* m_frontend_handle = nullptr;
  D3D12_RESOURCE_STATES m_required_state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

  D3D12::Texture m_framebuffer;

  // Display blit pipeline (fullscreen-quad VS + copy PS). Built in
  // CreateResources, used by RenderDisplay. The root signature shape
  // matches GPU_HW_D3D12::m_single_sampler_root_signature: 32-bit
  // constants at b0 (the source UV rect), an SRV table at t0, and a
  // sampler table at s0.
  ComPtr<ID3D12RootSignature> m_display_root_signature;
  ComPtr<ID3D12PipelineState> m_display_pipeline;
  D3D12::DescriptorHandle m_point_sampler;
};

class GPU_HW_D3D12 : public GPU_HW
{
public:
  template<typename T>
  using ComPtr = Microsoft::WRL::ComPtr<T>;

  GPU_HW_D3D12();
  ~GPU_HW_D3D12() override;

  bool Initialize(HostDisplay* host_display) override;
  void Reset(bool clear_vram) override;

  void ResetGraphicsAPIState() override;
  void RestoreGraphicsAPIState() override;
  void UpdateSettings() override;

protected:
  void ClearDisplay() override;
  void UpdateDisplay() override;
  void DownsampleFramebuffer(D3D12::Texture& source, uint32_t left, uint32_t top, uint32_t width, uint32_t height);
  void DownsampleFramebufferBoxFilter(D3D12::Texture& source, uint32_t left, uint32_t top, uint32_t width,
                                      uint32_t height);
  void DownsampleFramebufferAdaptive(D3D12::Texture& source, uint32_t left, uint32_t top, uint32_t width,
                                     uint32_t height);
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
  static constexpr uint32_t MAX_PUSH_CONSTANTS_SIZE = 64, TEXTURE_REPLACEMENT_BUFFER_SIZE = 64 * 1024 * 1024;
  void SetCapabilities();
  void DestroyResources();

  bool CreateRootSignatures();
  bool CreateSamplers();

  bool CreateFramebuffer();
  void ClearFramebuffer();
  void DestroyFramebuffer();

  bool CreateVertexBuffer();
  bool CreateUniformBuffer();
  bool CreateTextureBuffer();

  bool CompilePipelines();
  bool CompileDownsamplePipeline();
  void DestroyPipelines();
  void ClearDisplayPipelines();

  // Lazy batch PSO compile path.
  //
  // GetBatchPipeline returns the ID3D12PipelineState for the full
  // 7-D PSO matrix slot (filter as the outermost dimension over the
  // existing 6-D layout - mirrors the Vulkan dim-cache shape). It
  // builds the gpbuilder state on demand, pulling the fragment-
  // shader bytecode directly from the D3DCommon::EmbeddedShaders
  // pre-baked pickers (every batch FS variant is pre-baked as of
  // the f2620c1 arc completion - no runtime shadergen + D3DCompile)
  // and the vertex-shader bytecode from the PickBatchVertexShader
  // pre-baked picker (also pre-baked now; 2 variants). It then hands
  // the descriptor to m_shader_cache.GetPipelineState (which hits the
  // on-disk PSO cache where possible, only paying the actual driver
  // compile on cold runs). Reserved_* texture-mode PSOs share the
  // canonical slot.
  //
  // A new PSO is published with a compare-exchange on its slot. The
  // fast path is one uncontended atomic load per DrawBatchVertices
  // call.
  ComPtr<ID3D12PipelineState> GetBatchPipeline(GPUTextureFilter filter, uint8_t depth_test, uint8_t render_mode, uint8_t texture_mode, uint8_t transparency_mode);

  // Lazy non-batch PSO compile path.
  //
  // The non-batch pipelines (VRAM fill / copy / write / update depth
  // / readback, display, copy/blit) used to be compiled
  // unconditionally at CompilePipelines time regardless of the
  // gpu_shader_precompile_mode setting. Even with 'Disabled' the
  // user would pay the cost of building 17 PSOs + the fullscreen
  // quad vertex shader at GPU init - on a cold cache that is
  // hundreds of ms of D3DCompile + driver PSO assembly. The
  // 'Disabled' contract is meant to be "skip all compilation at
  // init time, fault everything in on first use", so these helpers
  // bring the non-batch pipelines under the same lazy-fault
  // umbrella as the batch matrix.
  //
  // Each helper follows the same fast-path / slow-path layout as
  // GetBatchPipeline: an acquire-load on the slot, and on a miss a
  // build published by compare-exchange. The runloop hot path -
  // FillVRAM / CopyVRAM / UpdateVRAM / UpdateDisplay / etc. - is one
  // atomic load once the slot is filled.
  //
  // GetFullscreenQuadVertexShader returns the pre-baked DXBC blob for
  // the screen-quad VS. No lazy compile, no caching state - the blob
  // is statically linked from src/common/d3d12/embedded_dxbc/. Every
  // non-batch pipeline in this backend uses it as the VS stage; see
  // src/common/d3d12/embedded_shaders.h for the underlying declaration.
  D3D12_SHADER_BYTECODE GetFullscreenQuadVertexShader();
  ComPtr<ID3D12PipelineState> GetVRAMFillPipeline(uint8_t wrapped, uint8_t interlaced);
  ComPtr<ID3D12PipelineState> GetVRAMCopyPipeline(uint8_t depth_test);
  ComPtr<ID3D12PipelineState> GetVRAMWritePipeline(uint8_t depth_test);
  ComPtr<ID3D12PipelineState> GetVRAMUpdateDepthPipeline();
  ComPtr<ID3D12PipelineState> GetVRAMReadbackPipeline();
  ComPtr<ID3D12PipelineState> GetDisplayPipeline(uint8_t depth_24, uint8_t interlace_mode);
  ComPtr<ID3D12PipelineState> GetCopyPipeline();

  // Background-thread worker for 'Lazy' precompile mode: walks the
  // full PSO matrix in (depth_test, render_mode, transparency_mode,
  // texture_mode) order and calls
  // GetBatchPipeline on each cell. As with D3D11, the main thread
  // can race ahead and fill any slot it needs at draw time; the
  // worker just observes the filled slot and moves on.
  void ShaderCompileThreadEntryPoint();
  void StartShaderCompileThread();
  void StopShaderCompileThread();

  bool CreateTextureReplacementStreamBuffer();
  bool BlitVRAMReplacementTexture(const TextureReplacementTexture* tex, uint32_t dst_x, uint32_t dst_y, uint32_t width, uint32_t height);

  ComPtr<ID3D12RootSignature> m_batch_root_signature;
  ComPtr<ID3D12RootSignature> m_single_sampler_root_signature;

  D3D12::Texture m_vram_texture;
  D3D12::Texture m_vram_depth_texture;
  D3D12::Texture m_vram_read_texture;
  D3D12::Texture m_vram_readback_texture;
  D3D12::StagingTexture m_vram_readback_staging_texture;
  D3D12::Texture m_display_texture;
  D3D12::Texture m_downsample_texture;

  // Adaptive downsample pyramid. D3D12::Texture is single-mip, so the
  // mip chain is managed as a raw resource with manually-built views: a
  // full-chain SRV for the composite pass (samp0, SampleLevel across
  // mips), a per-mip SRV for each generation/blur pass's read, and a
  // per-mip RTV for each generation pass's write. Per-subresource (mip)
  // resource states are tracked in m_downsample_mip_states because the
  // generation loop reads mip[n-1] while writing mip[n].
  Microsoft::WRL::ComPtr<ID3D12Resource> m_downsample_mip_resource;
  std::vector<D3D12_RESOURCE_STATES> m_downsample_mip_states;
  D3D12::DescriptorHandle m_downsample_full_srv;
  std::vector<D3D12::DescriptorHandle> m_downsample_mip_srvs;
  std::vector<D3D12::DescriptorHandle> m_downsample_mip_rtvs;
  D3D12::Texture m_downsample_weight_texture;
  uint32_t m_downsample_mip_levels = 0;

  D3D12::DescriptorHandle m_point_sampler;
  D3D12::DescriptorHandle m_linear_sampler;
  D3D12::DescriptorHandle m_trilinear_sampler;

  D3D12::StreamBuffer m_vertex_stream_buffer;
  D3D12::StreamBuffer m_uniform_stream_buffer;
  D3D12::StreamBuffer m_texture_stream_buffer;
  D3D12::DescriptorHandle m_texture_stream_buffer_srv;

  uint32_t m_current_uniform_buffer_offset = 0;

  // Batch PSO matrix. Each slot owns one reference to its PSO and is
  // filled once by compare-exchange, so DrawBatchVertices samples a
  // slot without taking any lock. Same scheme as the D3D11 batch
  // pixel shader matrix.
  //
  // filter is the outermost dimension. With the cbuffer-refactor
  // patch (7b575a3) the HLSL is invariant under
  // resolution_scale / true_color / scaled_dithering, but it is
  // STILL dependent on texture filter - the FilteredSampleFromVRAM
  // helper is emitted differently for Nearest vs Bilinear vs
  // JINC2 vs xBR vs the BinAlpha variants. Dimensioning over filter
  // lets a filter toggle skip the DestroyPipelines round trip in
  // UpdateSettings: the previous filter's sub-cube remains valid
  // and reachable, and switching back to it later is an atomic
  // load on an already-filled slot. Mirrors the same shape the
  // Vulkan backend has carried for filter / true_color /
  // scaled_dithering (Vulkan keeps true_color and scaled_dithering
  // as dims too because those are VkSpecializationInfo spec
  // constants and each combination needs a distinct VkPipeline;
  // D3D12 doesn't because they're cbuffer fields now).
  //
  // GPUTextureFilter::Count = 7 -> the outermost dim size.
  //
  // [filter][depth_test][render_mode][texture_mode][transparency_mode]
  // The [dithering] dim was dropped at the matrix-collapse commit
  // following the DITHERING-via-cbuffer routing (3af8e02): the FS
  // source no longer depends on the dithering bit, so the two
  // dithering slots compiled to identical DXBC and the matrix only
  // held a redundant ComPtr in each pair. m_batch.dithering still
  // flips per-batch but it reaches the FS through u_dithering on the
  // batch UBO; the PSO lookup is dithering-agnostic.
  // The [interlacing] dim was dropped at the matrix-collapse commit
  // following the INTERLACING-via-cbuffer routing (eb42ffb): same
  // shape, same reasoning. m_batch.interlacing still flips per-batch
  // but the FS reads u_interlacing from the batch UBO with a
  // short-circuit branch; the PSO lookup is interlacing-agnostic.
  DimensionalArray<AtomicSlot<ID3D12PipelineState*>, 5, 9, 4, 2, 7> m_batch_pipelines_fastpath;

  // m_shader_cache is retained for the PSO pipeline-library cache
  // (the gpbuilder.Create(device, m_shader_cache) calls). There is no
  // m_shadergen member any more: every batch / VRAM-ops / display
  // shader is pre-baked, so the D3D12 backend issues zero D3DCompile
  // calls and never instantiates GPU_HW_ShaderGen.
  D3D12::ShaderCache m_shader_cache;
  sthread_t* m_shader_compile_thread = nullptr;
  retro_atomic_int_t m_shader_compile_thread_quit = {};

  // The fragment-shader side needs no blob matrix: every batch FS
  // variant is pre-baked (the batch FS pre-bake arc completed at
  // f2620c1), so GetBatchPipeline pulls FS bytecode directly from the
  // D3DCommon::EmbeddedShaders pickers. The vertex shader is now
  // pre-baked too (2 variants via PickBatchVertexShader), so the
  // former m_batch_vertex_shader_blobs runtime-compiled cache is gone
  // as well - GetBatchPipeline wraps the picked VS DXBC into a
  // D3D12_SHADER_BYTECODE inline.

  // Non-batch PSO slots, same scheme as the batch matrix.
  // [wrapped][interlaced]
  DimensionalArray<AtomicSlot<ID3D12PipelineState*>, 2, 2> m_vram_fill_pipelines_fastpath;

  // [depth_test]
  std::array<AtomicSlot<ID3D12PipelineState*>, 2> m_vram_write_pipelines_fastpath;
  std::array<AtomicSlot<ID3D12PipelineState*>, 2> m_vram_copy_pipelines_fastpath;

  AtomicSlot<ID3D12PipelineState*> m_vram_readback_pipeline_fastpath;
  AtomicSlot<ID3D12PipelineState*> m_vram_update_depth_pipeline_fastpath;

  // [depth_24][interlace_mode]
  DimensionalArray<AtomicSlot<ID3D12PipelineState*>, 3, 2> m_display_pipelines_fastpath;

  AtomicSlot<ID3D12PipelineState*> m_copy_pipeline_fastpath;

  ComPtr<ID3D12PipelineState> m_downsample_pipeline;
  // Adaptive downsample pipelines. The mip-generation (first/mid) and
  // blur passes reuse m_single_sampler_root_signature (b0 + 1 SRV + 1
  // sampler) and the UV-quad VS; the composite pass needs two SRVs +
  // two samplers, so it gets its own root signature.
  ComPtr<ID3D12RootSignature> m_downsample_composite_root_signature;
  ComPtr<ID3D12PipelineState> m_downsample_first_pass_pipeline;
  ComPtr<ID3D12PipelineState> m_downsample_mid_pass_pipeline;
  ComPtr<ID3D12PipelineState> m_downsample_blur_pass_pipeline;
  ComPtr<ID3D12PipelineState> m_downsample_composite_pipeline;
  D3D12::Texture m_vram_write_replacement_texture;
  D3D12::StreamBuffer m_texture_replacment_stream_buffer;
};
