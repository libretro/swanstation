#pragma once
#include "common/dimensional_array.h"
#include "common/lockfree.h"
#include "common/vulkan/staging_texture.h"
#include "common/vulkan/stream_buffer.h"
#include "common/vulkan/texture.h"
#include "core/host_display.h"
#include "gpu_hw.h"
#include "texture_replacements.h"
#include <array>
#include <memory>
#include <rthreads/rthreads.h>
#include <tuple>
#include <libretro.h>

#define HAVE_VULKAN
#include <libretro_vulkan.h>

class LibretroVulkanHostDisplay final : public HostDisplay
{
public:
  LibretroVulkanHostDisplay();
  ~LibretroVulkanHostDisplay();

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
  static constexpr VkFormat FRAMEBUFFER_FORMAT = VK_FORMAT_R8G8B8A8_UNORM;

  struct PushConstants
  {
    float src_rect_left;
    float src_rect_top;
    float src_rect_width;
    float src_rect_height;
  };

  bool CheckFramebufferSize(uint32_t width, uint32_t height);

  VkDescriptorSetLayout m_descriptor_set_layout = VK_NULL_HANDLE;
  VkPipelineLayout m_pipeline_layout = VK_NULL_HANDLE;
  VkPipeline m_cursor_pipeline = VK_NULL_HANDLE;
  VkPipeline m_display_pipeline = VK_NULL_HANDLE;
  VkSampler m_point_sampler = VK_NULL_HANDLE;
  VkSampler m_linear_sampler = VK_NULL_HANDLE;

  Vulkan::Texture m_display_pixels_texture;
  Vulkan::StagingTexture m_upload_staging_texture;
  Vulkan::StagingTexture m_readback_staging_texture;

  retro_hw_render_interface_vulkan* m_ri = nullptr;

  Vulkan::Texture m_frame_texture;
  retro_vulkan_image m_frame_view = {};
  VkFramebuffer m_frame_framebuffer = VK_NULL_HANDLE;
  VkRenderPass m_frame_render_pass = VK_NULL_HANDLE;
};

class GPU_HW_Vulkan : public GPU_HW
{
public:
  GPU_HW_Vulkan();
  ~GPU_HW_Vulkan() override;

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
  static constexpr uint32_t MAX_PUSH_CONSTANTS_SIZE = 64, TEXTURE_REPLACEMENT_BUFFER_SIZE = 64 * 1024 * 1024;
  void SetCapabilities();
  void DestroyResources();

  ALWAYS_INLINE bool InRenderPass() const { return (m_current_render_pass != VK_NULL_HANDLE); }
  void BeginRenderPass(VkRenderPass render_pass, VkFramebuffer framebuffer, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                       const VkClearValue* clear_value = nullptr);
  void BeginVRAMRenderPass();
  void EndRenderPass();
  void ExecuteCommandBuffer(bool wait_for_completion, bool restore_state);

  bool CreatePipelineLayouts();
  bool CreateSamplers();

  bool CreateFramebuffer();
  void ClearFramebuffer();
  void DestroyFramebuffer();

  // Downsample-only resource lifecycle. Extracted from
  // Create/DestroyFramebuffer so a downsample-mode toggle can be
  // serviced WITHOUT the full framebuffer round-trip (ReadVRAM ->
  // recreate-everything -> UpdateVRAM). Used directly by
  // UpdateSettings when downsample_changed is the only thing that
  // differs, and called from Create/DestroyFramebuffer for the
  // full-rebuild path.
  bool CreateDownsampleResources(uint32_t texture_width, uint32_t texture_height, VkFormat texture_format);
  void DestroyDownsampleResources();
  void DestroyDownsamplePipelines();

  bool CreateVertexBuffer();
  bool CreateUniformBuffer();
  bool CreateTextureBuffer();

  bool CompilePipelines();
  void DestroyPipelines();

  // Lazy batch-fragment-shader-module + PSO compile path. Same
  // three-mode (Disabled / Enabled / Lazy) plumbing as the D3D11
  // and D3D12 backends; see GPUShaderPrecompileMode in core/types.h
  // for the user-facing semantics.
  //
  // Unlike D3D11 / D3D12, Vulkan can't share a refcounted resource
  // wrapper between dispatch matrix slots, because both
  // VkShaderModule and VkPipeline are raw handles destroyed via
  // SafeDestroyShaderModule / SafeDestroyPipeline - two slots
  // holding the same handle would double-call vkDestroy*. So the
  // Reserved_*Direct16Bit dedup is applied at the *helper entry*
  // (texture_mode 3 -> 2 and 7 -> 6 before the array index is
  // computed) rather than by populating both slots. The dup slots
  // for texture_mode 3 / 7 remain VK_NULL_HANDLE for the lifetime
  // of the GPU backend; SafeDestroy* handles VK_NULL_HANDLE
  // gracefully so DestroyPipelines doesn't trip on them.
  // The filter parameter selects which sub-cube of m_batch_pipelines /
  // m_batch_fragment_shaders the lookup and slow-path compile target.
  // The true_color / scaled_dithering parameters extend the same
  // pattern to those per-session spec consts on m_batch_pipelines
  // only - they do not affect SPIR-V blob choice so the FS module
  // cache stays a single 5D array shared across (true_color,
  // scaled_dithering) combos. Main-thread draw callers pass
  // (m_texture_filtering, m_true_color, m_scaled_dithering); the
  // background warm-up worker passes the filter it is currently
  // warming alongside its captured (m_true_color, m_scaled_dithering)
  // snapshot. Both helpers are reentrant under the same tuple - the
  // slot's compare-exchange picks the race winner.
  //
  // on_worker selects the pipeline cache: m_worker_pipeline_cache on
  // the worker, the main cache on the main thread.
  VkShaderModule GetBatchFragmentShader(GPUTextureFilter filter, uint8_t render_mode, uint8_t texture_mode,
                                        bool dithering, bool interlacing);
  VkPipeline GetBatchPipeline(GPUTextureFilter filter, bool true_color, bool scaled_dithering,
                              uint8_t depth_test, uint8_t render_mode, uint8_t texture_mode,
                              uint8_t transparency_mode, bool dithering, bool interlacing,
                              bool on_worker);

  // Lazy non-batch PSO compile path. Mirrors the D3D12 backend's
  // GetVRAMFillPipeline / GetVRAMCopyPipeline / GetDisplayPipeline
  // / etc. helpers: each non-batch pipeline (VRAM fill / copy /
  // write / update depth / readback, display, downsample) is
  // built on first use rather than unconditionally at GPU init.
  //
  // Unlike the batch helpers these helpers run main-thread-only.
  // The Lazy worker pre-fills them via the main-thread pre-fill
  // pass in CompilePipelines BEFORE spawning the batch worker, so
  // by the time gameplay begins all 18-ish non-batch slots are
  // filled and no contention can occur.
  //
  // The shared fullscreen-quad / UV-quad vertex shader modules are
  // cached as members so each helper can reach them without
  // re-running glslang + vkCreateShaderModule on every call.
  VkShaderModule GetFullscreenQuadVertexShader();
  VkShaderModule GetUVQuadVertexShader();
  VkPipeline GetVRAMFillPipeline(uint8_t wrapped, uint8_t interlaced);
  VkPipeline GetVRAMCopyPipeline(uint8_t depth_test);
  VkPipeline GetVRAMWritePipeline(uint8_t depth_test);
  VkPipeline GetVRAMUpdateDepthPipeline();
  VkPipeline GetVRAMReadbackPipeline();
  VkPipeline GetDisplayPipeline(uint8_t depth_24, uint8_t interlace_mode);
  VkPipeline GetDownsampleFirstPassPipeline();
  VkPipeline GetDownsampleMidPassPipeline();
  VkPipeline GetDownsampleBlurPassPipeline();
  VkPipeline GetDownsampleCompositePassPipeline();

  // Background-thread worker for 'Lazy' mode: walks the full PSO
  // matrix and calls GetBatchPipeline on each cell. Main thread
  // can race ahead and fault in any slot it actually needs at draw
  // time; the worker observes the filled slot and moves on. Quit
  // flag checked between cells so DestroyPipelines can stop the
  // worker within at most one PSO compile of latency.
  void ShaderCompileThreadEntryPoint();
  void StartShaderCompileThread();
  void StopShaderCompileThread();

  bool CreateTextureReplacementStreamBuffer();

  bool BlitVRAMReplacementTexture(const TextureReplacementTexture* tex, uint32_t dst_x, uint32_t dst_y, uint32_t width, uint32_t height);

  void DownsampleFramebuffer(Vulkan::Texture& source, uint32_t left, uint32_t top, uint32_t width, uint32_t height);
  void DownsampleFramebufferBoxFilter(Vulkan::Texture& source, uint32_t left, uint32_t top, uint32_t width, uint32_t height);
  void DownsampleFramebufferAdaptive(Vulkan::Texture& source, uint32_t left, uint32_t top, uint32_t width, uint32_t height);

  VkRenderPass m_current_render_pass = VK_NULL_HANDLE;

  VkRenderPass m_vram_render_pass = VK_NULL_HANDLE;
  VkRenderPass m_vram_update_depth_render_pass = VK_NULL_HANDLE;
  VkRenderPass m_display_load_render_pass = VK_NULL_HANDLE;
  VkRenderPass m_display_discard_render_pass = VK_NULL_HANDLE;
  VkRenderPass m_vram_readback_render_pass = VK_NULL_HANDLE;

  VkDescriptorSetLayout m_batch_descriptor_set_layout = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_single_sampler_descriptor_set_layout = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_vram_write_descriptor_set_layout = VK_NULL_HANDLE;

  VkPipelineLayout m_batch_pipeline_layout = VK_NULL_HANDLE;
  VkPipelineLayout m_no_samplers_pipeline_layout = VK_NULL_HANDLE;
  VkPipelineLayout m_single_sampler_pipeline_layout = VK_NULL_HANDLE;
  VkPipelineLayout m_vram_write_pipeline_layout = VK_NULL_HANDLE;

  Vulkan::Texture m_vram_texture;
  Vulkan::Texture m_vram_depth_texture;
  Vulkan::Texture m_vram_read_texture;
  Vulkan::Texture m_vram_readback_texture;
  Vulkan::StagingTexture m_vram_readback_staging_texture;
  Vulkan::Texture m_display_texture;
  bool m_use_ssbos_for_vram_writes = false;

  VkFramebuffer m_vram_framebuffer = VK_NULL_HANDLE;
  VkFramebuffer m_vram_update_depth_framebuffer = VK_NULL_HANDLE;
  VkFramebuffer m_vram_readback_framebuffer = VK_NULL_HANDLE;
  VkFramebuffer m_display_framebuffer = VK_NULL_HANDLE;

  VkSampler m_point_sampler = VK_NULL_HANDLE;
  VkSampler m_linear_sampler = VK_NULL_HANDLE;
  VkSampler m_trilinear_sampler = VK_NULL_HANDLE;

  VkDescriptorSet m_batch_descriptor_set = VK_NULL_HANDLE;
  VkDescriptorSet m_vram_copy_descriptor_set = VK_NULL_HANDLE;
  VkDescriptorSet m_vram_read_descriptor_set = VK_NULL_HANDLE;
  VkDescriptorSet m_vram_write_descriptor_set = VK_NULL_HANDLE;
  VkDescriptorSet m_display_descriptor_set = VK_NULL_HANDLE;

  Vulkan::StreamBuffer m_vertex_stream_buffer;
  Vulkan::StreamBuffer m_uniform_stream_buffer;
  Vulkan::StreamBuffer m_texture_stream_buffer;

  uint32_t m_current_uniform_buffer_offset = 0;
  VkBufferView m_texture_stream_buffer_view = VK_NULL_HANDLE;

  // [filter][true_color][scaled_dithering][depth_test][render_mode][texture_mode][transparency_mode][dithering][interlacing]
  //
  // Three outermost dimensions are the "near-instant toggle"
  // settings: each represents a per-session value baked into every
  // PSO via spec const (true_color = id 106, scaled_dithering = id
  // 105) or structural SPIR-V blob selection (filter). Indexing by
  // the current m_texture_filtering / m_true_color / m_scaled_dith-
  // ering means toggling any of them in the core options menu does
  // NOT flush the cache: the previous sub-cube remains valid,
  // populated, and instantly addressable when the user cycles back.
  // CompilePipelines on such a toggle populates the new sub-cube
  // (Enabled mode synchronous, Lazy via worker, Disabled lazy
  // fault-in). DestroyPipelines still does a full sweep - it is
  // reached only on a NON-dimensioned shader-affecting change
  // (resolution scale, MSAA, per-sample shading, UV limits, chroma
  // smoothing, downsample mode, PGXP depth, colour perspective,
  // precompile mode), which flips per-session state applied to
  // EVERY sub-cube and therefore invalidates all of them at once.
  //
  // The six inner dimensions are the per-batch / per-draw state
  // that GetBatchPipeline indexes today: depth-test mode, render
  // mode, texture mode, transparency mode, per-call dithering bit
  // (m_batch.dithering, NOT m_scaled_dithering), and the
  // interlacing bit. These are passed to GetBatchPipeline as
  // explicit arguments from DrawBatchVertices and the precompile
  // loop.
  //
  // The background warm-up worker scopes its walk to the CURRENT
  // (m_true_color, m_scaled_dithering) snapshot taken at thread
  // entry, varying only filter and the six inner dims. We
  // intentionally do NOT walk the off-current spec const sub-cubes
  // in the background: a full 7 x 2 x 2 = 28 sub-cube warm-up on
  // a cold pipeline cache would take a few minutes of background
  // CPU per session, well past "useful" into "annoying". Instead
  // the first toggle of true_color or scaled_dithering pays the
  // usual sync compile (Enabled) or fault-in stutter (Lazy /
  // Disabled) for the new combo's current filter sub-cube; the
  // worker then walks the new combo's six other filter sub-cubes
  // in the background as before. Cycling back is instant in all
  // modes thanks to the dimensioned cache.
  //
  // AtomicSlot<VkPipeline> rather than a plain VkPipeline so the
  // draw path can sample a slot without taking any lock. The slow
  // path (slot still null after the atomic load) compiles the PSO,
  // then publishes it with a compare-exchange; the loser of a race
  // destroys its own. The fast path - which is what
  // DrawBatchVertices hits once a slot has been filled either by
  // the precompile worker or by an earlier main-thread fault-in - is
  // a single acquire load with no kernel calls and no serialisation
  // against the worker.
  DimensionalArray<AtomicSlot<VkPipeline>, 2, 2, 5, 9, 4, 3, 2, 2, 7> m_batch_pipelines;

  // Persistent vertex / fragment shader modules and shadergen for
  // the lazy and background-thread compile paths. These used to be
  // locals in CompilePipelines and were leaked on success; now they
  // live for the lifetime of this GPU backend so the lazy PSO
  // builder can reach them at draw time, and DestroyPipelines tears
  // them down properly.
  //
  // Dup slots for texture_mode 3 / 7 stay VK_NULL_HANDLE; the
  // helper-entry remap (see GetBatchFragmentShader) routes all
  // accesses through the canonical slots 2 / 6.
  //
  // Slots are published the same way as m_batch_pipelines. The
  // worker creates pipelines against its own m_worker_pipeline_cache
  // (vkCreateGraphicsPipelines requires external synchronisation of
  // the cache), which StopShaderCompileThread merges back into the
  // main cache after joining it.
  sthread_t* m_shader_compile_thread = nullptr;
  retro_atomic_int_t m_shader_compile_thread_quit = {};
  VkPipelineCache m_worker_pipeline_cache = VK_NULL_HANDLE;

  DimensionalArray<AtomicSlot<VkShaderModule>, 2> m_batch_vertex_shaders;              // [textured]
  DimensionalArray<AtomicSlot<VkShaderModule>, 2, 2, 9, 4, 7> m_batch_fragment_shaders;   // [filter][render][texture][dither][interlace]

  // Shared vertex shaders used by all non-batch pipelines. Cached
  // here so the lazy non-batch helpers don't run glslang +
  // vkCreateShaderModule on every call - first GetXxxQuadVertexShader
  // call populates them, subsequent calls return the cached handle.
  // DestroyPipelines tears them down via SafeDestroyShaderModule.
  VkShaderModule m_fullscreen_quad_vertex_shader = VK_NULL_HANDLE;
  VkShaderModule m_uv_quad_vertex_shader = VK_NULL_HANDLE;

  // [wrapped][interlaced]
  DimensionalArray<VkPipeline, 2, 2> m_vram_fill_pipelines{};

  // [depth_test]
  std::array<VkPipeline, 2> m_vram_write_pipelines{};
  std::array<VkPipeline, 2> m_vram_copy_pipelines{};

  VkPipeline m_vram_readback_pipeline = VK_NULL_HANDLE;
  VkPipeline m_vram_update_depth_pipeline = VK_NULL_HANDLE;

  // [depth_24][interlace_mode]
  DimensionalArray<VkPipeline, 3, 2> m_display_pipelines{};

  // texture replacements
  Vulkan::Texture m_vram_write_replacement_texture;
  Vulkan::StreamBuffer m_texture_replacment_stream_buffer;

  // downsampling
  Vulkan::Texture m_downsample_texture;
  VkRenderPass m_downsample_render_pass = VK_NULL_HANDLE;
  Vulkan::Texture m_downsample_weight_texture;
  VkRenderPass m_downsample_weight_render_pass = VK_NULL_HANDLE;
  VkFramebuffer m_downsample_weight_framebuffer = VK_NULL_HANDLE;

  struct SmoothMipView
  {
    VkImageView image_view = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
  };
  std::vector<SmoothMipView> m_downsample_mip_views;

  VkPipelineLayout m_downsample_pipeline_layout = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_downsample_composite_descriptor_set_layout = VK_NULL_HANDLE;
  VkPipelineLayout m_downsample_composite_pipeline_layout = VK_NULL_HANDLE;
  VkDescriptorSet m_downsample_composite_descriptor_set = VK_NULL_HANDLE;
  VkPipeline m_downsample_first_pass_pipeline = VK_NULL_HANDLE;
  VkPipeline m_downsample_mid_pass_pipeline = VK_NULL_HANDLE;
  VkPipeline m_downsample_blur_pass_pipeline = VK_NULL_HANDLE;
  VkPipeline m_downsample_composite_pass_pipeline = VK_NULL_HANDLE;
};
