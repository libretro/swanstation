#include "gpu_hw_d3d12.h"
#include "common/align.h"
#include "common/d3d12/context.h"
#include "common/d3d12/descriptor_heap_manager.h"
#include "common/d3d_common/embedded_shaders.h"
#include "common/d3d12/shader_cache.h"
#include "common/d3d12/util.h"
#include "common/log.h"
#include "common/thread_priority.h"
#include "common/timer.h"
#include "host_display.h"
#include "host_interface.h"
#include "shader_cache_version.h"
#include "system.h"
#define HAVE_D3D12
#include "libretro_d3d.h"
#include <cstring>
Log_SetChannel(GPU_HW_D3D12);

extern retro_environment_t g_retro_environment_callback;
extern retro_video_refresh_t g_retro_video_refresh_callback;

// Index parallels HostDisplayPixelFormat (Unknown, RGBA8, BGRA8, RGB565,
// RGBA5551, Count). Same table layout as gpu_hw_d3d11.cpp; the Unknown
// slot resolves to DXGI_FORMAT_UNKNOWN as a value-0 sentinel.
static constexpr std::array<DXGI_FORMAT, static_cast<uint32_t>(HostDisplayPixelFormat::Count)>
  s_display_pixel_format_mapping = {{DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM,
                                     DXGI_FORMAT_B5G6R5_UNORM, DXGI_FORMAT_B5G5R5A1_UNORM}};

// =====================================================================
// LibretroD3D12HostDisplay - libretro HostDisplay subclass for the D3D12
// hardware renderer path.
//
// Mirrors LibretroD3D11HostDisplay structurally, with two architectural
// differences driven by the libretro D3D12 hardware interface
// (retro_hw_render_interface_d3d12 in libretro_d3d.h):
//
//   1. Device + queue ownership lives in the frontend. We adopt them
//      via D3D12::Context::CreateForLibretro(); D3D12::Context::Destroy()
//      releases our refs. m_device / m_context style members are absent
//      because everything that needs the device goes through
//      g_d3d12_context, which is what gpu_hw_d3d12.cpp uses anyway.
//
//   2. Present path is the set_texture callback rather than a swapchain
//      blit. Render() composites into m_framebuffer (an RTV we own),
//      transitions it to required_state, calls set_texture, then signals
//      the frontend a HW frame is ready via
//      g_retro_video_refresh_callback(RETRO_HW_FRAME_BUFFER_VALID, ...).
//
// The HostDisplay::SetDisplayPixels path (SW pixels uploaded to a
// frontend-readable texture via UpdateSubresource) is not implemented
// for D3D12 in the current libretro frontend integration; the SW
// renderer uses LibretroHostDisplay instead. BeginSetDisplayPixels
// returns false so any caller short-circuits.
// =====================================================================

LibretroD3D12HostDisplay::LibretroD3D12HostDisplay() = default;
LibretroD3D12HostDisplay::~LibretroD3D12HostDisplay() = default;

bool LibretroD3D12HostDisplay::RequestHardwareRendererContext(retro_hw_render_callback* cb)
{
  cb->cache_context = false;
  cb->bottom_left_origin = false;
  cb->context_type = RETRO_HW_CONTEXT_D3D12;
  cb->version_major = 12;
  cb->version_minor = 0;

  return g_retro_environment_callback(RETRO_ENVIRONMENT_SET_HW_RENDER, cb);
}

HostDisplay::RenderAPI LibretroD3D12HostDisplay::GetRenderAPI() const
{
  return RenderAPI::D3D12;
}

void* LibretroD3D12HostDisplay::GetRenderDevice() const
{
  return g_d3d12_context ? g_d3d12_context->GetDevice() : nullptr;
}

void* LibretroD3D12HostDisplay::GetRenderContext() const
{
  // D3D12 has no DeviceContext analogue - work is recorded on command
  // lists owned by the Context. Return nullptr; callers either don't
  // need it (D3D12 path) or already special-case the D3D12 RenderAPI.
  return nullptr;
}

bool LibretroD3D12HostDisplay::CreateRenderDevice(const WindowInfo& wi, std::string_view adapter_name, bool debug_device,
                                                  bool threaded_presentation)
{
  retro_hw_render_interface* ri = nullptr;
  if (!g_retro_environment_callback(RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE, &ri))
  {
    Log_ErrorPrint("Failed to get HW render interface");
    return false;
  }
  else if (ri->interface_type != RETRO_HW_RENDER_INTERFACE_D3D12 ||
           ri->interface_version != RETRO_HW_RENDER_INTERFACE_D3D12_VERSION)
  {
    Log_ErrorPrintf("Unexpected HW interface - type %u version %u", static_cast<unsigned>(ri->interface_type),
                    static_cast<unsigned>(ri->interface_version));
    return false;
  }

  const retro_hw_render_interface_d3d12* d3d12_ri = reinterpret_cast<const retro_hw_render_interface_d3d12*>(ri);
  if (!d3d12_ri->device || !d3d12_ri->queue || !d3d12_ri->set_texture)
  {
    Log_ErrorPrintf("Missing D3D12 device, queue, or set_texture callback");
    return false;
  }

  if (!D3D12::Context::CreateForLibretro(d3d12_ri->device, d3d12_ri->queue))
  {
    Log_ErrorPrint("Failed to adopt D3D12 device/queue for libretro");
    return false;
  }

  m_set_texture = d3d12_ri->set_texture;
  m_frontend_handle = d3d12_ri->handle;
  m_required_state = d3d12_ri->required_state;
  m_window_info = wi;
  return true;
}

bool LibretroD3D12HostDisplay::InitializeRenderDevice(std::string_view shader_cache_directory, bool debug_device,
                                                     bool threaded_presentation)
{
  return CreateResources();
}

void LibretroD3D12HostDisplay::DestroyRenderDevice()
{
  ClearSoftwareCursor();
  DestroyResources();

  if (g_d3d12_context)
  {
    g_d3d12_context->WaitForGPUIdle();
    D3D12::Context::Destroy();
  }
}

void LibretroD3D12HostDisplay::ResizeRenderWindow(int32_t new_window_width, int32_t new_window_height)
{
  m_window_info.surface_width = static_cast<uint32_t>(new_window_width);
  m_window_info.surface_height = static_cast<uint32_t>(new_window_height);
}

bool LibretroD3D12HostDisplay::ChangeRenderWindow(const WindowInfo& new_wi)
{
  // Check that the device/queue/handle haven't changed under us.
  retro_hw_render_interface* ri = nullptr;
  if (!g_retro_environment_callback(RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE, &ri))
  {
    Log_ErrorPrint("Failed to get HW render interface");
    return false;
  }
  else if (ri->interface_type != RETRO_HW_RENDER_INTERFACE_D3D12 ||
           ri->interface_version != RETRO_HW_RENDER_INTERFACE_D3D12_VERSION)
  {
    Log_ErrorPrintf("Unexpected HW interface - type %u version %u", static_cast<unsigned>(ri->interface_type),
                    static_cast<unsigned>(ri->interface_version));
    return false;
  }

  const retro_hw_render_interface_d3d12* d3d12_ri = reinterpret_cast<const retro_hw_render_interface_d3d12*>(ri);
  if (d3d12_ri->device != g_d3d12_context->GetDevice() || d3d12_ri->queue != g_d3d12_context->GetCommandQueue() ||
      d3d12_ri->set_texture != m_set_texture || d3d12_ri->handle != m_frontend_handle)
  {
    Log_ErrorPrintf("D3D12 device/queue/handle changed outside our control");
    return false;
  }

  // required_state is allowed to change across a ChangeRenderWindow -
  // re-cache it in case the frontend renegotiated.
  m_required_state = d3d12_ri->required_state;
  m_window_info = new_wi;
  return true;
}

std::unique_ptr<HostDisplayTexture> LibretroD3D12HostDisplay::CreateTexture(uint32_t width, uint32_t height, uint32_t layers,
                                                                            uint32_t levels, uint32_t samples,
                                                                            HostDisplayPixelFormat format,
                                                                            const void* data, uint32_t data_stride,
                                                                            bool dynamic)
{
  // GPU_HW_D3D12 manages its own textures through D3D12::Texture directly.
  // The HostDisplay::CreateTexture surface is only used by code paths that
  // do not run for the libretro D3D12 backend (software cursor preload,
  // SetDisplayPixels CPU-to-GPU upload), so this returns nullptr the same
  // way LibretroHostDisplay does.
  return nullptr;
}

bool LibretroD3D12HostDisplay::SupportsDisplayPixelFormat(HostDisplayPixelFormat format) const
{
  const DXGI_FORMAT dfmt = s_display_pixel_format_mapping[static_cast<uint32_t>(format)];
  return dfmt != DXGI_FORMAT_UNKNOWN && g_d3d12_context && g_d3d12_context->SupportsTextureFormat(dfmt);
}

bool LibretroD3D12HostDisplay::BeginSetDisplayPixels(HostDisplayPixelFormat format, uint32_t width, uint32_t height,
                                                    void** out_buffer, uint32_t* out_pitch)
{
  // See comment on CreateTexture - this path is not wired for D3D12
  // libretro. The SW renderer uses LibretroHostDisplay.
  return false;
}

void LibretroD3D12HostDisplay::EndSetDisplayPixels()
{
}

bool LibretroD3D12HostDisplay::CreateResources()
{
  // Build the display blit pipeline. m_framebuffer itself is sized lazily
  // in CheckFramebufferSize when Render() sees the first display frame.
  D3D12::RootSignatureBuilder rsbuilder;
  rsbuilder.SetInputAssemblerFlag();
  rsbuilder.Add32BitConstants(0, 4, D3D12_SHADER_VISIBILITY_PIXEL);
  rsbuilder.AddDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1, D3D12_SHADER_VISIBILITY_PIXEL);
  rsbuilder.AddDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 0, 1, D3D12_SHADER_VISIBILITY_PIXEL);
  m_display_root_signature = rsbuilder.Create();
  if (!m_display_root_signature)
    return false;

  D3D12::GraphicsPipelineBuilder gpbuilder;
  gpbuilder.SetRootSignature(m_display_root_signature.Get());
  gpbuilder.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
  gpbuilder.SetVertexShader(D3DCommon::EmbeddedShaders::k_fullscreen_quad_vs,
                            static_cast<uint32_t>(D3DCommon::EmbeddedShaders::k_fullscreen_quad_vs_size_bytes));
  gpbuilder.SetPixelShader(D3DCommon::EmbeddedShaders::k_copy_ps,
                           static_cast<uint32_t>(D3DCommon::EmbeddedShaders::k_copy_ps_size_bytes));
  gpbuilder.SetNoCullRasterizationState();
  gpbuilder.SetNoDepthTestState();
  gpbuilder.SetNoBlendingState();
  gpbuilder.SetRenderTarget(0, DXGI_FORMAT_R8G8B8A8_UNORM);
  m_display_pipeline = gpbuilder.Create(g_d3d12_context->GetDevice());
  if (!m_display_pipeline)
    return false;

  D3D12_SAMPLER_DESC sampler_desc = {};
  D3D12::SetDefaultSampler(&sampler_desc);
  sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
  if (!g_d3d12_context->GetSamplerHeapManager().Allocate(&m_point_sampler))
    return false;
  g_d3d12_context->GetDevice()->CreateSampler(&sampler_desc, m_point_sampler.cpu_handle);

  return true;
}

void LibretroD3D12HostDisplay::DestroyResources()
{
  if (m_point_sampler)
    g_d3d12_context->GetSamplerHeapManager().Free(&m_point_sampler);
  m_display_pipeline.Reset();
  m_display_root_signature.Reset();
  m_framebuffer.Destroy(false);
}

bool LibretroD3D12HostDisplay::CheckFramebufferSize(uint32_t width, uint32_t height)
{
  if (m_framebuffer.GetWidth() == width && m_framebuffer.GetHeight() == height)
    return true;

  m_framebuffer.Destroy(false);
  return m_framebuffer.Create(width, height, 1, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM,
                              DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_UNKNOWN, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
}

bool LibretroD3D12HostDisplay::Render()
{
  // No display texture this frame -> send the libretro frame-dupe
  // signal (NULL frame), matching the SW and D3D11 paths. See the
  // equivalent comment in gpu_hw_d3d11.cpp::Render().
  if (!HasDisplayTexture())
  {
    g_retro_video_refresh_callback(nullptr, 0, 0, 0);
    return true;
  }

  const uint32_t resolution_scale = g_host_interface_storage.GetResolutionScale();
  const uint32_t display_width = static_cast<uint32_t>(m_display_width) * resolution_scale;
  const uint32_t display_height = static_cast<uint32_t>(m_display_height) * resolution_scale;

  if (!CheckFramebufferSize(display_width, display_height))
    return false;

  // The GPU_HW_D3D12 path produced the display image inside its own
  // m_display_texture / m_vram_texture / m_downsample_texture and called
  // SetDisplayTexture with a D3D12::Texture* handle plus a view sub-rect.
  // The content does NOT always sit at the texture origin: the box
  // downsample resolve writes at (view_x, view_y) (the box PS ties its
  // read offset to its write offset), and the direct-VRAM present path
  // points at the display region's VRAM offset. The libretro
  // set_texture / RETRO_HW_FRAME_BUFFER_VALID interface only carries a
  // width/height and always presents from (0,0), so forwarding the raw
  // resource showed the wrong sub-rect whenever the view offset was
  // non-zero. Double-buffered titles flip the display between VRAM y=0
  // and y~=240 every frame, which made the downsampled output flicker at
  // 30Hz between the resolved region and the cleared/other buffer.
  //
  // Composite the (view_x, view_y, view_w, view_h) sub-rect into
  // m_framebuffer (a (0,0)-origin RTV we own) and hand that to the
  // frontend, exactly like the D3D11 and Vulkan backends. The blit also
  // upscales the native-resolution downsampled image to the display size,
  // matching their SSAA output.
  D3D12::Texture* const display_texture = static_cast<D3D12::Texture*>(const_cast<void*>(m_display_texture_handle));

  ID3D12GraphicsCommandList* cmdlist = g_d3d12_context->GetCommandList();
  m_framebuffer.TransitionToState(D3D12_RESOURCE_STATE_RENDER_TARGET);
  display_texture->TransitionToState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

  cmdlist->OMSetRenderTargets(1, &m_framebuffer.GetRTVOrDSVDescriptor().cpu_handle, FALSE, nullptr);
  RenderDisplay(0, 0, static_cast<int32_t>(display_width), static_cast<int32_t>(display_height), display_texture,
                m_display_texture_width, m_display_texture_height, m_display_texture_view_x, m_display_texture_view_y,
                m_display_texture_view_width, m_display_texture_view_height);

  m_framebuffer.TransitionToState(m_required_state);

  // Flush our pending command list before handing the texture over -
  // otherwise the frontend may read stale contents.
  g_d3d12_context->ExecuteCommandList(false);

  m_set_texture(m_frontend_handle, m_framebuffer.GetResource(), DXGI_FORMAT_R8G8B8A8_UNORM);
  g_retro_video_refresh_callback(RETRO_HW_FRAME_BUFFER_VALID, display_width, display_height, 0);
  return true;
}

void LibretroD3D12HostDisplay::RenderDisplay(int32_t left, int32_t top, int32_t width, int32_t height,
                                             D3D12::Texture* texture, int32_t texture_width, int32_t texture_height,
                                             int32_t view_x, int32_t view_y, int32_t view_width, int32_t view_height)
{
  ID3D12GraphicsCommandList* cmdlist = g_d3d12_context->GetCommandList();

  const float uniforms[4] = {static_cast<float>(view_x) / static_cast<float>(texture_width),
                             static_cast<float>(view_y) / static_cast<float>(texture_height),
                             static_cast<float>(view_width) / static_cast<float>(texture_width),
                             static_cast<float>(view_height) / static_cast<float>(texture_height)};

  cmdlist->SetGraphicsRootSignature(m_display_root_signature.Get());
  cmdlist->SetGraphicsRoot32BitConstants(0, sizeof(uniforms) / sizeof(uint32_t), uniforms, 0);
  cmdlist->SetGraphicsRootDescriptorTable(1, texture->GetSRVDescriptor());
  cmdlist->SetGraphicsRootDescriptorTable(2, m_point_sampler.gpu_handle);
  cmdlist->SetPipelineState(m_display_pipeline.Get());
  D3D12::SetViewportAndScissor(cmdlist, left, top, width, height);
  cmdlist->DrawInstanced(3, 1, 0, 0);
}

GPU_HW_D3D12::GPU_HW_D3D12() = default;

GPU_HW_D3D12::~GPU_HW_D3D12()
{
  if (m_host_display)
  {
    m_host_display->ClearDisplayTexture();
    ResetGraphicsAPIState();
  }

  DestroyResources();
}

bool GPU_HW_D3D12::Initialize(HostDisplay* host_display)
{
  if (host_display->GetRenderAPI() != HostDisplay::RenderAPI::D3D12)
  {
    Log_ErrorPrintf("Host render API is incompatible");
    return false;
  }

  SetCapabilities();

  if (!GPU_HW::Initialize(host_display))
    return false;

  if (!CreateRootSignatures())
  {
    Log_ErrorPrintf("Failed to create root signatures");
    return false;
  }

  if (!CreateSamplers())
  {
    Log_ErrorPrintf("Failed to create samplers");
    return false;
  }

  if (!CreateVertexBuffer())
  {
    Log_ErrorPrintf("Failed to create vertex buffer");
    return false;
  }

  if (!CreateUniformBuffer())
  {
    Log_ErrorPrintf("Failed to create uniform buffer");
    return false;
  }

  if (!CreateTextureBuffer())
  {
    Log_ErrorPrintf("Failed to create texture buffer");
    return false;
  }

  if (!CreateFramebuffer())
  {
    Log_ErrorPrintf("Failed to create framebuffer");
    return false;
  }

  if (!CompilePipelines())
  {
    Log_ErrorPrintf("Failed to compile pipelines");
    return false;
  }

  RestoreGraphicsAPIState();
  UpdateDepthBufferFromMaskBit();
  return true;
}

void GPU_HW_D3D12::Reset(bool clear_vram)
{
  GPU_HW::Reset(clear_vram);

  if (clear_vram)
    ClearFramebuffer();
}

void GPU_HW_D3D12::ResetGraphicsAPIState()
{
  GPU_HW::ResetGraphicsAPIState();
}

void GPU_HW_D3D12::RestoreGraphicsAPIState()
{
  ID3D12GraphicsCommandList* cmdlist = g_d3d12_context->GetCommandList();
  m_vram_texture.TransitionToState(D3D12_RESOURCE_STATE_RENDER_TARGET);
  cmdlist->OMSetRenderTargets(1, &m_vram_texture.GetRTVOrDSVDescriptor().cpu_handle, FALSE,
                              &m_vram_depth_texture.GetRTVOrDSVDescriptor().cpu_handle);

  const D3D12_VERTEX_BUFFER_VIEW vbv{m_vertex_stream_buffer.GetGPUPointer(), m_vertex_stream_buffer.GetSize(),
                                     sizeof(BatchVertex)};
  cmdlist->IASetVertexBuffers(0, 1, &vbv);
  cmdlist->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

  cmdlist->SetGraphicsRootSignature(m_batch_root_signature.Get());
  cmdlist->SetGraphicsRootConstantBufferView(0,
                                             m_uniform_stream_buffer.GetGPUPointer() + m_current_uniform_buffer_offset);
  cmdlist->SetGraphicsRootDescriptorTable(1, m_vram_read_texture.GetSRVDescriptor().gpu_handle);
  cmdlist->SetGraphicsRootDescriptorTable(2, m_point_sampler.gpu_handle);

  D3D12::SetViewport(cmdlist, 0, 0, m_vram_texture.GetWidth(), m_vram_texture.GetHeight());

  SetScissorFromDrawingArea();
}

void GPU_HW_D3D12::UpdateSettings()
{
  GPU_HW::UpdateSettings();

  // Stop the background batch-compile worker BEFORE UpdateHWSettings
  // writes m_texture_filtering. The worker captures m_texture_filtering
  // at launch and uses it for every GetBatchPipeline call - if the
  // runloop thread flips it mid-iteration the worker would split its
  // filter-index read across old / new values, installing a PSO built
  // with one filter into a sub-cube indexed by another. Joining first
  // eliminates the race; the next
  // CompilePipelines below restarts a worker for the new filter as
  // appropriate. StopShaderCompileThread is idempotent, so the
  // matching call inside CompilePipelines itself just becomes a
  // no-op on this path.
  StopShaderCompileThread();

  // shader_source_changed: any setting that affects HLSL source
  // string changed. Excludes the cbuffer-only set
  // (resolution_scale / true_color / scaled_dithering).
  //
  // only_dim_changed: dim-cube setting (filter / true_color /
  // scaled_dithering) changed and nothing in non_dim_diff. With the
  // dim cache (filter outermost) this means "filter sub-cube can be
  // lazy-populated, other filters' sub-cubes stay valid". When set
  // alongside shader_source_changed it picks out the filter-only-
  // changed case; when set without shader_source_changed it picks
  // out the cbuffer-only case (which the gate at the bottom
  // implicitly handles by checking shader_source_changed).
  //
  // display_only_source_changed: chroma_smoothing flipped and
  // nothing else affecting shader source changed. The batch matrix
  // and VRAM ops PSOs stay valid; only the 6-slot display PSO
  // cache needs to go.
  bool framebuffer_changed, shaders_changed, only_dim_changed, downsample_changed, shader_source_changed,
    display_only_source_changed;
  UpdateHWSettings(&framebuffer_changed, &shaders_changed, &only_dim_changed, &downsample_changed,
                   &shader_source_changed, &display_only_source_changed);

  // A downsample-mode change UpdateHWSettings did not fold into
  // framebuffer_changed (Disabled <-> Box; it only sets that flag when
  // Adaptive is involved, which D3D12 never selects) still needs the box
  // downsample texture created or freed. CreateFramebuffer (re)builds it
  // for the new mode, so route the change through the normal ReadVRAM ->
  // CreateFramebuffer -> UpdateVRAM round-trip below.
  if (downsample_changed && !framebuffer_changed)
    framebuffer_changed = true;

  if (framebuffer_changed)
  {
    RestoreGraphicsAPIState();
    ReadVRAM(0, 0, VRAM_WIDTH, VRAM_HEIGHT);
    ResetGraphicsAPIState();
  }

  // Everything should be finished executing before recreating resources.
  m_host_display->ClearDisplayTexture();
  g_d3d12_context->ExecuteCommandList(true);

  if (framebuffer_changed)
    CreateFramebuffer();

  if (shader_source_changed)
  {
    if (display_only_source_changed)
    {
      // chroma_smoothing flipped and nothing else - clear just the
      // display PSO cache; the next UpdateDisplay's
      // GetDisplayPipeline lazy-faults each (depth_24, interlace_mode)
      // slot with HLSL re-generated against the new
      // m_chroma_smoothing. Cost is 6 D3DCompile + 6 PSO creates
      // (a couple of seconds) instead of the full ~1164-PSO
      // matrix walk.
      ClearDisplayPipelines();

      // Honour the precompile_mode contract for the rebuild side:
      //   Enabled: pre-fault the six display PSOs synchronously so
      //            the first UpdateDisplay after the toggle doesn't
      //            stutter. Six D3DCompile + six PSO creates run in
      //            roughly a second on the user's RTX 5090, well
      //            under the multi-tens-of-seconds the full matrix
      //            walk used to take.
      //   Lazy:    relaunch the background worker we stopped above
      //            so the batch matrix continues to warm up. The
      //            display PSOs get faulted on first use - same as
      //            Lazy's normal contract for the non-batch
      //            pipelines.
      //   Disabled: no worker, no pre-fault. Display PSOs are
      //            faulted on first use, matching the
      //            "skip all compilation at init" contract.
      const GPUShaderPrecompileMode precompile_mode = g_settings.gpu_shader_precompile_mode;
      if (precompile_mode == GPUShaderPrecompileMode::Enabled)
      {
        for (uint8_t depth_24 = 0; depth_24 < 2; depth_24++)
        {
          for (uint8_t interlace_mode = 0; interlace_mode < 3; interlace_mode++)
            (void)GetDisplayPipeline(depth_24, interlace_mode);
        }
      }
      else if (precompile_mode == GPUShaderPrecompileMode::Lazy)
      {
        StartShaderCompileThread();
      }
    }
    else if (only_dim_changed)
    {
      // Filter changed but nothing in non_dim_diff (and the cbuffer-
      // only members in dim_diff don't move HLSL, so this is
      // effectively "only filter changed"). m_batch_pipelines_fastpath is
      // filter-dimensioned: the previous filter's sub-cube remains
      // populated and reachable, so DestroyPipelines would just
      // throw away valid PSOs. Skip it and just call CompilePipelines,
      // which lazy-populates the new filter's sub-cube on top: in
      // Enabled mode the precompile_sync loop walks the matrix calling
      // GetBatchPipeline with the new m_texture_filtering, so the
      // other sub-cubes are untouched. In Lazy / Disabled the new
      // sub-cube stays empty until first draw / the worker reaches
      // each cell.
      //
      // Cycling back to a previously-visited filter is instant - no
      // D3DCompile call, no PSO rebuild, just an atomic load of an
      // already-filled slot.
      CompilePipelines();
    }
    else
    {
      // Full flush: a non-dim shader-affecting change
      // (multisamples / per-sample shading / UV limits / PGXP depth
      // / colour perspective / precompile mode) invalidates EVERY
      // sub-cube because those settings bake into the HLSL
      // identically for every filter slot.
      DestroyPipelines();
      CompilePipelines();
    }
  }

  // Rebuild the box downsample PSO for the new mode. shader_source_changed
  // was false for a downsample-only change, so CompilePipelines above did
  // not run; without this m_downsample_pipeline would be stale (or null)
  // after a Disabled <-> Box toggle. The texture side is handled by the
  // downsample_changed -> framebuffer_changed promotion above.
  if (downsample_changed)
    CompileDownsamplePipeline();

  // this has to be done here, because otherwise we're using destroyed pipelines in the same cmdbuffer
  if (framebuffer_changed)
  {
    RestoreGraphicsAPIState();
    UpdateVRAM(0, 0, VRAM_WIDTH, VRAM_HEIGHT, m_vram_ptr, false, false);
    UpdateDepthBufferFromMaskBit();
    UpdateDisplay();
    ResetGraphicsAPIState();
  }
}

void GPU_HW_D3D12::MapBatchVertexPointer(uint32_t required_vertices)
{
  const uint32_t required_space = required_vertices * sizeof(BatchVertex);
  if (!m_vertex_stream_buffer.ReserveMemory(required_space, sizeof(BatchVertex)))
  {
    g_d3d12_context->ExecuteCommandList(false);
    RestoreGraphicsAPIState();
    m_vertex_stream_buffer.ReserveMemory(required_space, sizeof(BatchVertex));
  }

  m_batch_start_vertex_ptr = static_cast<BatchVertex*>(m_vertex_stream_buffer.GetCurrentHostPointer());
  m_batch_current_vertex_ptr = m_batch_start_vertex_ptr;
  m_batch_end_vertex_ptr = m_batch_start_vertex_ptr + (m_vertex_stream_buffer.GetCurrentSpace() / sizeof(BatchVertex));
  m_batch_base_vertex = m_vertex_stream_buffer.GetCurrentOffset() / sizeof(BatchVertex);
}

void GPU_HW_D3D12::UnmapBatchVertexPointer(uint32_t used_vertices)
{
  if (used_vertices > 0)
    m_vertex_stream_buffer.CommitMemory(used_vertices * sizeof(BatchVertex));

  m_batch_start_vertex_ptr = nullptr;
  m_batch_end_vertex_ptr = nullptr;
  m_batch_current_vertex_ptr = nullptr;
}

void GPU_HW_D3D12::UploadUniformBuffer(const void* data, uint32_t data_size)
{
  if (!m_uniform_stream_buffer.ReserveMemory(data_size, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT))
  {
    g_d3d12_context->ExecuteCommandList(false);
    RestoreGraphicsAPIState();
    m_uniform_stream_buffer.ReserveMemory(data_size, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
  }

  m_current_uniform_buffer_offset = m_uniform_stream_buffer.GetCurrentOffset();
  std::memcpy(m_uniform_stream_buffer.GetCurrentHostPointer(), data, data_size);
  m_uniform_stream_buffer.CommitMemory(data_size);

  g_d3d12_context->GetCommandList()->SetGraphicsRootConstantBufferView(0, m_uniform_stream_buffer.GetGPUPointer() +
                                                                            m_current_uniform_buffer_offset);
}

void GPU_HW_D3D12::SetCapabilities()
{
  // TODO: Query from device
  const uint32_t max_texture_size = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
  const uint32_t max_texture_scale = max_texture_size / VRAM_WIDTH;
  Log_InfoPrintf("Max texture size: %ux%u", max_texture_size, max_texture_size);
  m_max_resolution_scale = max_texture_scale;

  m_max_multisamples = 1;
  // DXGI hard cap on multisample sample count (the D3D11_/D3D12_
  // MAX_MULTISAMPLE_SAMPLE_COUNT defines, both == 32). Hardcoded so
  // this D3D12 TU doesn't pull in d3d11.h just for the constant (the
  // D3D12_ spelling isn't present in the MinGW headers).
  for (uint32_t multisamples = 2; multisamples < 32; multisamples++)
  {
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS fd = {DXGI_FORMAT_R8G8B8A8_UNORM, static_cast<UINT>(multisamples)};

    if (SUCCEEDED(g_d3d12_context->GetDevice()->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &fd,
                                                                    sizeof(fd))) &&
        fd.NumQualityLevels > 0)
    {
      m_max_multisamples = multisamples;
    }
  }

  m_supports_dual_source_blend = true;
  m_supports_per_sample_shading = true;
  m_supports_adaptive_downsampling = true;
  m_supports_disable_color_perspective = true;
  Log_InfoPrintf("Dual-source blend: %s", m_supports_dual_source_blend ? "supported" : "not supported");
  Log_InfoPrintf("Per-sample shading: %s", m_supports_per_sample_shading ? "supported" : "not supported");
  Log_InfoPrintf("Max multisamples: %u", m_max_multisamples);
}

void GPU_HW_D3D12::DestroyResources()
{
  // Everything should be finished executing before recreating resources.
  if (g_d3d12_context)
    g_d3d12_context->ExecuteCommandList(true);

  DestroyFramebuffer();
  DestroyPipelines();

  g_d3d12_context->GetSamplerHeapManager().Free(&m_point_sampler);
  g_d3d12_context->GetSamplerHeapManager().Free(&m_linear_sampler);
  if (m_trilinear_sampler)
    g_d3d12_context->GetSamplerHeapManager().Free(&m_trilinear_sampler);
  g_d3d12_context->GetDescriptorHeapManager().Free(&m_texture_stream_buffer_srv);

  m_vertex_stream_buffer.Destroy(false);
  m_uniform_stream_buffer.Destroy(false);
  m_texture_stream_buffer.Destroy(false);

  m_single_sampler_root_signature.Reset();
  m_batch_root_signature.Reset();
}

bool GPU_HW_D3D12::CreateRootSignatures()
{
  D3D12::RootSignatureBuilder rsbuilder;
  rsbuilder.SetInputAssemblerFlag();
  rsbuilder.AddCBVParameter(0, D3D12_SHADER_VISIBILITY_ALL);
  rsbuilder.AddDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1, D3D12_SHADER_VISIBILITY_PIXEL);
  rsbuilder.AddDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 0, 1, D3D12_SHADER_VISIBILITY_PIXEL);
  m_batch_root_signature = rsbuilder.Create();
  if (!m_batch_root_signature)
    return false;

  rsbuilder.Add32BitConstants(0, MAX_PUSH_CONSTANTS_SIZE / sizeof(uint32_t), D3D12_SHADER_VISIBILITY_ALL);
  rsbuilder.AddDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1, D3D12_SHADER_VISIBILITY_PIXEL);
  rsbuilder.AddDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 0, 1, D3D12_SHADER_VISIBILITY_PIXEL);
  m_single_sampler_root_signature = rsbuilder.Create();
  if (!m_single_sampler_root_signature)
    return false;

  // Adaptive downsample composite: b0 push constants (the SmoothingUBO -
  // unused by the composite FS but kept in the binding layout to match
  // the other downsample passes), two SRVs (color pyramid at t0, weight
  // at t1), and two samplers (trilinear at s0, linear at s1). Separate
  // single-descriptor tables per register avoid any contiguous-descriptor
  // requirement on the heap allocator.
  rsbuilder.Add32BitConstants(0, MAX_PUSH_CONSTANTS_SIZE / sizeof(uint32_t), D3D12_SHADER_VISIBILITY_ALL);
  rsbuilder.AddDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1, D3D12_SHADER_VISIBILITY_PIXEL);
  rsbuilder.AddDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, D3D12_SHADER_VISIBILITY_PIXEL);
  rsbuilder.AddDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 0, 1, D3D12_SHADER_VISIBILITY_PIXEL);
  rsbuilder.AddDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 1, D3D12_SHADER_VISIBILITY_PIXEL);
  m_downsample_composite_root_signature = rsbuilder.Create();
  if (!m_downsample_composite_root_signature)
    return false;

  return true;
}

bool GPU_HW_D3D12::CreateSamplers()
{
  D3D12_SAMPLER_DESC desc = {};
  D3D12::SetDefaultSampler(&desc);
  desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;

  if (!g_d3d12_context->GetSamplerHeapManager().Allocate(&m_point_sampler))
    return false;

  g_d3d12_context->GetDevice()->CreateSampler(&desc, m_point_sampler.cpu_handle);

  desc.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;

  if (!g_d3d12_context->GetSamplerHeapManager().Allocate(&m_linear_sampler))
    return false;

  g_d3d12_context->GetDevice()->CreateSampler(&desc, m_linear_sampler.cpu_handle);

  // Trilinear (mip-interpolating) sampler for the adaptive composite's
  // colour-pyramid SampleLevel. Matches D3D11's m_trilinear_sampler_state.
  desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;

  if (!g_d3d12_context->GetSamplerHeapManager().Allocate(&m_trilinear_sampler))
    return false;

  g_d3d12_context->GetDevice()->CreateSampler(&desc, m_trilinear_sampler.cpu_handle);
  return true;
}

bool GPU_HW_D3D12::CreateFramebuffer()
{
  DestroyFramebuffer();

  // scale vram size to internal resolution
  const uint32_t texture_width = VRAM_WIDTH * m_resolution_scale;
  const uint32_t texture_height = VRAM_HEIGHT * m_resolution_scale;
  const DXGI_FORMAT texture_format = DXGI_FORMAT_R8G8B8A8_UNORM;
  /* D32_FLOAT rather than D16_UNORM: in PGXP depth-buffer mode the value
   * written to this attachment is the reconstructed perspective W, carried at
   * full float32 through the vertex path (a_pos is R32G32B32A32_FLOAT) - a
   * 16-bit UNORM depth quantizes exactly the precision PGXP exists to recover.
   * D32_FLOAT is lossless for the legacy ordered-depth path too (the 16-bit
   * counter's 0..1 range is trivially representable and ordering is preserved),
   * so the format is made unconditional: a format conditional on the PGXP-depth
   * setting would go stale on a mid-session toggle, since that toggle recompiles
   * shaders but does not set framebuffer_changed / recreate this texture. The
   * batch/VRAM-ops PSOs pick this up automatically via
   * m_vram_depth_texture.GetFormat(). */
  const DXGI_FORMAT depth_format = DXGI_FORMAT_D32_FLOAT;

  if (!m_vram_texture.Create(texture_width, texture_height, m_multisamples, texture_format, texture_format,
                             texture_format, DXGI_FORMAT_UNKNOWN, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) ||
      !m_vram_depth_texture.Create(
        texture_width, texture_height, m_multisamples, depth_format, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN,
        depth_format, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) ||
      !m_vram_read_texture.Create(texture_width, texture_height, 1, texture_format, texture_format, DXGI_FORMAT_UNKNOWN,
                                  DXGI_FORMAT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE) ||
      !m_display_texture.Create(texture_width, texture_height, 1, texture_format, texture_format, texture_format,
                                DXGI_FORMAT_UNKNOWN, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) ||
      !m_vram_readback_texture.Create(VRAM_WIDTH, VRAM_HEIGHT, 1, texture_format, texture_format, texture_format,
                                      DXGI_FORMAT_UNKNOWN, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) ||
      !m_vram_readback_staging_texture.Create(VRAM_WIDTH / 2, VRAM_HEIGHT, texture_format, false))
  {
    return false;
  }

  D3D12::SetObjectName(m_vram_texture, "VRAM Texture");
  D3D12::SetObjectName(m_vram_depth_texture, "VRAM Depth Texture");
  D3D12::SetObjectName(m_vram_read_texture, "VRAM Read/Sample Texture");
  D3D12::SetObjectName(m_display_texture, "VRAM Display Texture");
  D3D12::SetObjectName(m_vram_read_texture, "VRAM Readback Texture");

  m_vram_texture.TransitionToState(D3D12_RESOURCE_STATE_RENDER_TARGET);
  m_vram_depth_texture.TransitionToState(D3D12_RESOURCE_STATE_DEPTH_WRITE);
  m_vram_read_texture.TransitionToState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

  // Box downsample target. Resolves the upscaled VRAM/display texture
  // down to native PSX resolution in a single pass, so it is sized at
  // VRAM_WIDTH x VRAM_HEIGHT (unscaled) regardless of m_resolution_scale.
  if (m_downsample_mode == GPUDownsampleMode::Box)
  {
    if (!m_downsample_texture.Create(VRAM_WIDTH, VRAM_HEIGHT, 1, texture_format, texture_format, texture_format,
                                     DXGI_FORMAT_UNKNOWN, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET))
    {
      return false;
    }

    D3D12::SetObjectName(m_downsample_texture, "Downsample Texture");
    m_downsample_texture.TransitionToState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  }
  else if (m_downsample_mode == GPUDownsampleMode::Adaptive)
  {
    // Adaptive downsample pyramid. D3D12::Texture cannot create a mip
    // chain, so build the resource and its views by hand. The pyramid is
    // the scaled VRAM size with GetAdaptiveDownsamplingMipLevels() mips;
    // the weight texture is single-channel at the lowest mip's size.
    const uint32_t levels = GetAdaptiveDownsamplingMipLevels();
    m_downsample_mip_levels = levels;

    const D3D12_HEAP_PROPERTIES heap_properties = {D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = texture_width;
    desc.Height = texture_height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = static_cast<UINT16>(levels);
    desc.Format = texture_format;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE clear_value = {};
    clear_value.Format = texture_format;
    if (FAILED(g_d3d12_context->GetDevice()->CreateCommittedResource(
          &heap_properties, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear_value,
          IID_PPV_ARGS(m_downsample_mip_resource.GetAddressOf()))))
    {
      return false;
    }
    D3D12::SetObjectName(m_downsample_mip_resource.Get(), "Adaptive Downsample Pyramid");
    m_downsample_mip_states.assign(levels, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    // Full-chain SRV (all mips) for the composite pass's SampleLevel.
    if (!g_d3d12_context->GetDescriptorHeapManager().Allocate(&m_downsample_full_srv))
      return false;
    {
      D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
      srv_desc.Format = texture_format;
      srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
      srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      srv_desc.Texture2D.MostDetailedMip = 0;
      srv_desc.Texture2D.MipLevels = levels;
      g_d3d12_context->GetDevice()->CreateShaderResourceView(m_downsample_mip_resource.Get(), &srv_desc,
                                                             m_downsample_full_srv.cpu_handle);
    }

    // Per-mip read SRV + write RTV.
    m_downsample_mip_srvs.resize(levels);
    m_downsample_mip_rtvs.resize(levels);
    for (uint32_t i = 0; i < levels; i++)
    {
      if (!g_d3d12_context->GetDescriptorHeapManager().Allocate(&m_downsample_mip_srvs[i]))
        return false;
      D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
      srv_desc.Format = texture_format;
      srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
      srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      srv_desc.Texture2D.MostDetailedMip = i;
      srv_desc.Texture2D.MipLevels = 1;
      g_d3d12_context->GetDevice()->CreateShaderResourceView(m_downsample_mip_resource.Get(), &srv_desc,
                                                             m_downsample_mip_srvs[i].cpu_handle);

      if (!g_d3d12_context->GetRTVHeapManager().Allocate(&m_downsample_mip_rtvs[i]))
        return false;
      D3D12_RENDER_TARGET_VIEW_DESC rtv_desc = {};
      rtv_desc.Format = texture_format;
      rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
      rtv_desc.Texture2D.MipSlice = i;
      g_d3d12_context->GetDevice()->CreateRenderTargetView(m_downsample_mip_resource.Get(), &rtv_desc,
                                                           m_downsample_mip_rtvs[i].cpu_handle);
    }

    if (!m_downsample_weight_texture.Create(texture_width >> (levels - 1), texture_height >> (levels - 1), 1,
                                            DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8_UNORM,
                                            DXGI_FORMAT_UNKNOWN, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET))
    {
      return false;
    }
    D3D12::SetObjectName(m_downsample_weight_texture, "Adaptive Downsample Weight Texture");
    m_downsample_weight_texture.TransitionToState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  }

  ClearDisplay();
  SetFullVRAMDirtyRectangle();
  return true;
}

void GPU_HW_D3D12::ClearFramebuffer()
{
  static constexpr float clear_color[4] = {};

  ID3D12GraphicsCommandList* cmdlist = g_d3d12_context->GetCommandList();
  cmdlist->ClearRenderTargetView(m_vram_texture.GetRTVOrDSVDescriptor(), clear_color, 0, nullptr);
  cmdlist->ClearDepthStencilView(m_vram_depth_texture.GetRTVOrDSVDescriptor(), D3D12_CLEAR_FLAG_DEPTH,
                                 m_pgxp_depth_buffer ? 1.0f : 0.0f, 0, 0, nullptr);
  SetFullVRAMDirtyRectangle();
}

void GPU_HW_D3D12::DestroyFramebuffer()
{
  // Adaptive pyramid views/resource (no-ops when Box/Disabled).
  if (m_downsample_full_srv)
    g_d3d12_context->GetDescriptorHeapManager().Free(&m_downsample_full_srv);
  for (D3D12::DescriptorHandle& h : m_downsample_mip_srvs)
  {
    if (h)
      g_d3d12_context->GetDescriptorHeapManager().Free(&h);
  }
  for (D3D12::DescriptorHandle& h : m_downsample_mip_rtvs)
  {
    if (h)
      g_d3d12_context->GetRTVHeapManager().Free(&h);
  }
  m_downsample_mip_srvs.clear();
  m_downsample_mip_rtvs.clear();
  m_downsample_mip_states.clear();
  m_downsample_mip_levels = 0;
  m_downsample_weight_texture.Destroy(false);
  m_downsample_mip_resource.Reset();

  m_vram_read_texture.Destroy(false);
  m_vram_depth_texture.Destroy(false);
  m_vram_texture.Destroy(false);
  m_vram_readback_texture.Destroy(false);
  m_display_texture.Destroy(false);
  m_downsample_texture.Destroy(false);
  m_vram_readback_staging_texture.Destroy(false);
}

bool GPU_HW_D3D12::CreateVertexBuffer()
{
  if (!m_vertex_stream_buffer.Create(VERTEX_BUFFER_SIZE))
    return false;

  D3D12::SetObjectName(m_vertex_stream_buffer.GetBuffer(), "Vertex Stream Buffer");
  return true;
}

bool GPU_HW_D3D12::CreateUniformBuffer()
{
  if (!m_uniform_stream_buffer.Create(UNIFORM_BUFFER_SIZE))
    return false;

  D3D12::SetObjectName(m_vertex_stream_buffer.GetBuffer(), "Uniform Stream Buffer");
  return true;
}

bool GPU_HW_D3D12::CreateTextureBuffer()
{
  if (!m_texture_stream_buffer.Create(VRAM_UPDATE_TEXTURE_BUFFER_SIZE))
    return false;

  if (!g_d3d12_context->GetDescriptorHeapManager().Allocate(&m_texture_stream_buffer_srv))
    return false;

  D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
  desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  desc.Format = DXGI_FORMAT_R16_UINT;
  desc.Buffer.NumElements = VRAM_UPDATE_TEXTURE_BUFFER_SIZE / sizeof(uint16_t);
  desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  g_d3d12_context->GetDevice()->CreateShaderResourceView(m_texture_stream_buffer.GetBuffer(), &desc,
                                                         m_texture_stream_buffer_srv);

  D3D12::SetObjectName(m_texture_stream_buffer.GetBuffer(), "Texture Stream Buffer");
  return true;
}

bool GPU_HW_D3D12::CompilePipelines()
{
  // Make sure no previous background-compile worker is still alive
  // from a prior CompilePipelines call (UpdateSettings triggers
  // DestroyPipelines -> CompilePipelines, and the worker from the
  // previous incarnation has to be joined before we start a new one
  // so it doesn't keep writing into the matrix the new run is about
  // to fill).
  StopShaderCompileThread();

  // Open the disk-backed shader cache once; on subsequent calls
  // (UpdateSettings -> DestroyPipelines -> CompilePipelines) the
  // instance still holds the index from last time and the disk file
  // hasn't moved, so re-opening would double-count m_shader_index /
  // m_pipeline_index entries and leak the previous RFILE* handles.
  if (!m_shader_cache.IsOpen())
  {
    m_shader_cache.Open(g_host_interface->GetShaderCacheBasePath(), g_d3d12_context->GetDevice(),
                        g_d3d12_context->GetFeatureLevel(), SHADER_CACHE_VERSION, false);
  }

  // Whether to walk the full batch / PSO matrix synchronously,
  // hand it to a background thread, or skip it entirely. See the
  // comment on GPUShaderPrecompileMode in core/types.h.
  //
  // 'precompile_sync' also gates the non-batch pipelines (VRAM
  // fill / copy / write / update depth / readback, display,
  // copy/blit, and the shared fullscreen-quad vertex shader). On
  // 'Disabled' those are faulted in via the GetXxxPipeline helpers
  // the first time the runloop actually needs them, matching the
  // documented "Disabled = no compilation at init" contract. On
  // 'Lazy' the background thread pre-fills them before walking the
  // batch matrix. On 'Enabled' we still build everything upfront
  // here.
  const GPUShaderPrecompileMode precompile_mode = g_settings.gpu_shader_precompile_mode;
  const bool precompile_sync = (precompile_mode == GPUShaderPrecompileMode::Enabled);
  // Reachable cell counts for each matrix - see IsBatchShaderReachable
  // in gpu_hw.h. The PSO matrix multiplies by 2 (depth_test) * 5
  // (transparency_mode); these axes don't affect reachability, so the
  // per-(render_mode, texture_mode) skip rule is independent of them.
  const uint32_t reachable_batch_cells = CountReachableBatchShaders(m_supports_dual_source_blend);
  const uint32_t batch_shader_progress_units = precompile_sync ? reachable_batch_cells : 0u;
  const uint32_t batch_pipeline_progress_units =
    precompile_sync ? reachable_batch_cells * 2u * 5u : 0u;
  // Non-batch units only counted when we build them upfront. The
  // VRAM fill (4), VRAM copy (2), VRAM write (2), VRAM update depth
  // (1), VRAM readback (1), display (6) and copy/blit (1) sum to 17
  // - one tick per pipeline so the progress bar tracks the actual
  // work being done. (Down from 18 since the fullscreen-quad VS no
  // longer compiles - statically linked from
  // src/common/d3d12/embedded_dxbc/ via GetFullscreenQuadVertexShader.)
  const uint32_t non_batch_progress_units = precompile_sync ? 17u : 0u;

  ShaderCompileProgressTracker progress("Compiling Pipelines",
                                        batch_shader_progress_units + batch_pipeline_progress_units +
                                          non_batch_progress_units);

  // The batch vertex shaders are pre-baked too (consumed directly by
  // GetBatchPipeline via PickBatchVertexShader, the same way the
  // fragment shaders pull from the FS pickers), so there is no VS blob
  // matrix to fill and no synchronous compile here. With the batch VS
  // pre-baked, the D3D12 backend issues zero D3DCompile calls at
  // runtime - shadergen is gone entirely.

  // Batch PSO matrix. Behaviour depends on
  // g_settings.gpu_shader_precompile_mode (see core/types.h):
  //
  //   - Enabled : build every reachable batch PSO right here. The FS
  //               bytecode comes from the pre-baked pickers (no
  //               D3DCompile), so the only cost is the driver PSO
  //               compile + on-disk PSO cache lookup.
  //   - Lazy    : leave the matrix empty for now; spawn a worker
  //               thread at the end of CompilePipelines that fills
  //               it in the background.
  //   - Disabled: leave the matrix empty. GetBatchPipeline faults
  //               each combo in on the main thread the first time
  //               the game draws it.
  //
  // GetBatchPipeline handles the Reserved_*Direct16Bit dedup
  // internally; the precompile loop doesn't need to special-case
  // them - the second call for a duplicate is a cheap pointer copy
  // after the canonical mode has filled the canonical slot.
  //
  // Structurally unreachable cells (reserved texture modes, two-pass
  // fallback render modes with no texture, single-pass dual-source
  // on hardware that lacks it) are skipped via IsBatchShaderReachable.
  if (precompile_sync)
  {
    const bool dual_source = m_supports_dual_source_blend;
    // The dim cache makes m_batch_pipelines_fastpath filter-dimensioned.
    // precompile_sync walks ONLY the current m_texture_filtering
    // sub-cube, not the full 7-filter matrix - pre-filling six unused
    // sub-cubes would multiply the cold-cache PSO build pass by 7x
    // for no gain (the game can only be running under one filter at a
    // time, and the other sub-cubes get faulted in on demand if the
    // user later flips the filter setting and UpdateSettings calls
    // CompilePipelines again).
    const GPUTextureFilter cur_filter = m_texture_filtering;
    for (uint8_t render_mode = 0; render_mode < 4; render_mode++)
    {
      for (uint8_t texture_mode = 0; texture_mode < 9; texture_mode++)
      {
        if (!IsBatchShaderReachable(static_cast<BatchRenderMode>(render_mode), texture_mode, dual_source))
          continue;

        // All batch FS variants are pre-baked as of the e07ce04 +
        // <this-commit> arc completion. No shadergen + D3DCompile
        // call needed during precompile_sync's warmup; the
        // GetBatchPipeline path consumes pre-baked DXBC via
        // D3DCommon::EmbeddedShaders pickers in all cases:
        //   - Untextured (texture_mode == Disabled): commits
        //     9e6f933 / b6d1903 / c01f8ae (re-bake).
        //   - Textured + Nearest: commits 9e4c33d + a7f5717.
        //   - Textured + Bilinear / BilinearBinAlpha: commits
        //     269a2a0 + f2ae92a.
        //   - Textured + JINC2 / JINC2BinAlpha: commits
        //     17a0c66 + 6afe8c2.
        //   - Textured + xBR / xBRBinAlpha: commits e07ce04 +
        //     this one.
        // progress.Increment ticks for each reachable cell so the
        // user-facing progress bar reads the same count it did pre-
        // pre-bake; the cells just complete quickly because no
        // D3DCompile runs.
        progress.Increment();
      }
    }

    for (uint8_t depth_test = 0; depth_test < 2; depth_test++)
    {
      for (uint8_t render_mode = 0; render_mode < 4; render_mode++)
      {
        for (uint8_t transparency_mode = 0; transparency_mode < 5; transparency_mode++)
        {
          for (uint8_t texture_mode = 0; texture_mode < 9; texture_mode++)
          {
            if (!IsBatchShaderReachable(static_cast<BatchRenderMode>(render_mode), texture_mode, dual_source))
              continue;

            ComPtr<ID3D12PipelineState> pso =
              GetBatchPipeline(cur_filter, depth_test, render_mode, texture_mode, transparency_mode);
            if (!pso)
              return false;
            progress.Increment();
          }
        }
      }
    }
  }
  // For Lazy and Disabled: the matrix stays empty here, filled on
  // demand by GetBatchPipeline. The background-thread launch for
  // Lazy happens at the end of this function, after the rest of the
  // non-batch pipelines are built.

  // Non-batch pipelines (VRAM fill / copy / write / update depth /
  // readback, display, copy/blit). On 'Enabled' build them all
  // upfront here through the lazy helpers so the helpers' GetXxx
  // slot-fill machinery is exercised the same way it would be at
  // draw time. The progress bar ticks per pipeline so the user sees
  // a "4 + 2 + 2 + 1 + 1 + 6 + 1 = 17 unit" count. (Was 18 before
  // the fullscreen-quad VS moved to pre-baked DXBC, which has no
  // compilation step to tick for.)
  //
  // On 'Lazy' the worker thread reaches these via the same helpers
  // before walking the batch matrix. On 'Disabled' nothing happens
  // here at all - first FillVRAM / CopyVRAM / etc. on the runloop
  // faults the corresponding pipeline in and stores the result for
  // subsequent calls.
  if (precompile_sync)
  {
    // (No fullscreen-quad VS step here - it's pre-baked DXBC now,
    // statically linked from src/common/d3d12/embedded_dxbc/, so
    // there's nothing to compile or fault in. See
    // GetFullscreenQuadVertexShader. Progress unit count below drops
    // by 1 accordingly.)

    for (uint8_t wrapped = 0; wrapped < 2; wrapped++)
    {
      for (uint8_t interlaced = 0; interlaced < 2; interlaced++)
      {
        if (!GetVRAMFillPipeline(wrapped, interlaced))
          return false;
        progress.Increment();
      }
    }
    for (uint8_t depth_test = 0; depth_test < 2; depth_test++)
    {
      if (!GetVRAMCopyPipeline(depth_test))
        return false;
      progress.Increment();
    }
    for (uint8_t depth_test = 0; depth_test < 2; depth_test++)
    {
      if (!GetVRAMWritePipeline(depth_test))
        return false;
      progress.Increment();
    }
    if (!GetVRAMUpdateDepthPipeline())
      return false;
    progress.Increment();
    if (!GetVRAMReadbackPipeline())
      return false;
    progress.Increment();
    for (uint8_t depth_24 = 0; depth_24 < 2; depth_24++)
    {
      for (uint8_t interlace_mode = 0; interlace_mode < 3; interlace_mode++)
      {
        if (!GetDisplayPipeline(depth_24, interlace_mode))
          return false;
        progress.Increment();
      }
    }
    if (!GetCopyPipeline())
      return false;
    progress.Increment();
  }

  if (precompile_mode == GPUShaderPrecompileMode::Lazy)
  {
    // Pre-fill the non-batch pipelines on the main thread BEFORE
    // launching the worker, so the first frame (the
    // UpdateDepthBufferFromMaskBit() call at the end of Initialize(),
    // the first FillVRAM / UpdateDisplay) doesn't fault them in. This
    // costs Lazy the same few-hundred-ms upfront pause 'Enabled' pays
    // for its non-batch section. The worker then only walks the batch
    // matrix. (No fullscreen-quad VS pre-fill here either - pre-baked
    // DXBC, see GetFullscreenQuadVertexShader.)
    for (uint8_t wrapped = 0; wrapped < 2; wrapped++)
    {
      for (uint8_t interlaced = 0; interlaced < 2; interlaced++)
      {
        if (!GetVRAMFillPipeline(wrapped, interlaced))
          return false;
      }
    }
    for (uint8_t depth_test = 0; depth_test < 2; depth_test++)
    {
      if (!GetVRAMCopyPipeline(depth_test))
        return false;
    }
    for (uint8_t depth_test = 0; depth_test < 2; depth_test++)
    {
      if (!GetVRAMWritePipeline(depth_test))
        return false;
    }
    if (!GetVRAMUpdateDepthPipeline())
      return false;
    if (!GetVRAMReadbackPipeline())
      return false;
    for (uint8_t depth_24 = 0; depth_24 < 2; depth_24++)
    {
      for (uint8_t interlace_mode = 0; interlace_mode < 3; interlace_mode++)
      {
        if (!GetDisplayPipeline(depth_24, interlace_mode))
          return false;
      }
    }
    if (!GetCopyPipeline())
      return false;

    // Kick off the background batch / PSO fill so gameplay can start
    // immediately. The worker walks the full PSO matrix in
    // (depth_test, render_mode, transparency_mode, texture_mode)
    // order, calling the same GetBatchPipeline the draw path uses;
    // the main thread can race ahead and pre-fill any slot it
    // actually needs at draw time, and the worker just observes the
    // filled slot and moves on. DestroyPipelines signals
    // m_shader_compile_thread_quit and joins.
    StartShaderCompileThread();
  }

  if (!CompileDownsamplePipeline())
    return false;

  return true;
}

bool GPU_HW_D3D12::CompileDownsamplePipeline()
{
  // Box-filter downsample PSO: averages the upscaled VRAM/display texture
  // down to native PSX resolution in a single fullscreen pass. The
  // resolution scale is baked into the pre-baked FS variant
  // (PickBoxSampleDownsampleFS), so no cbuffer is needed - the
  // fullscreen-quad VS plus one source SRV and the point sampler under
  // m_single_sampler_root_signature is the entire binding set, identical
  // in shape to the display PSO. Built once per session (and rebuilt by
  // UpdateSettings on a downsample-mode / resolution-scale change).
  if (m_downsample_mode == GPUDownsampleMode::Box)
  {
    const D3D12_SHADER_BYTECODE vs = GetFullscreenQuadVertexShader();
    const auto fs_bc = D3DCommon::EmbeddedShaders::PickBoxSampleDownsampleFS(m_resolution_scale);
    const D3D12_SHADER_BYTECODE fs{fs_bc.data, fs_bc.size};

    D3D12::GraphicsPipelineBuilder gpbuilder;
    gpbuilder.SetRootSignature(m_single_sampler_root_signature.Get());
    gpbuilder.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
    gpbuilder.SetVertexShader(vs);
    gpbuilder.SetPixelShader(fs);
    gpbuilder.SetNoCullRasterizationState();
    gpbuilder.SetNoDepthTestState();
    gpbuilder.SetNoBlendingState();
    gpbuilder.SetRenderTarget(0, m_downsample_texture.GetFormat());

    m_downsample_pipeline = gpbuilder.Create(g_d3d12_context->GetDevice(), m_shader_cache, false);
    if (!m_downsample_pipeline)
      return false;

    D3D12::SetObjectName(m_downsample_pipeline.Get(), "Downsample Pipeline");
    return true;
  }

  if (m_downsample_mode != GPUDownsampleMode::Adaptive)
    return true;

  // Adaptive downsample: four pre-baked passes. The mip-generation
  // (first/mid) and blur passes share the UV-quad VS and
  // m_single_sampler_root_signature (b0 SmoothingUBO + 1 SRV + 1 sampler);
  // the composite pass uses the fullscreen-quad VS (it reads SV_Position,
  // not the interpolated UV) and the two-SRV/two-sampler composite root
  // signature. RT formats: the colour-pyramid passes write RGBA8, the
  // blur writes the R8 weight, the composite writes the RGBA8 display
  // texture.
  ID3D12Device* const device = g_d3d12_context->GetDevice();
  const D3D12_SHADER_BYTECODE uv_vs{D3DCommon::EmbeddedShaders::k_uv_quad_vs,
                                    D3DCommon::EmbeddedShaders::k_uv_quad_vs_size_bytes};

  {
    const auto fs = D3DCommon::EmbeddedShaders::PickAdaptiveDownsampleMipFS(true);
    D3D12::GraphicsPipelineBuilder b;
    b.SetRootSignature(m_single_sampler_root_signature.Get());
    b.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
    b.SetVertexShader(uv_vs);
    b.SetPixelShader(D3D12_SHADER_BYTECODE{fs.data, fs.size});
    b.SetNoCullRasterizationState();
    b.SetNoDepthTestState();
    b.SetNoBlendingState();
    b.SetRenderTarget(0, DXGI_FORMAT_R8G8B8A8_UNORM);
    m_downsample_first_pass_pipeline = b.Create(device, m_shader_cache, false);
  }
  {
    const auto fs = D3DCommon::EmbeddedShaders::PickAdaptiveDownsampleMipFS(false);
    D3D12::GraphicsPipelineBuilder b;
    b.SetRootSignature(m_single_sampler_root_signature.Get());
    b.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
    b.SetVertexShader(uv_vs);
    b.SetPixelShader(D3D12_SHADER_BYTECODE{fs.data, fs.size});
    b.SetNoCullRasterizationState();
    b.SetNoDepthTestState();
    b.SetNoBlendingState();
    b.SetRenderTarget(0, DXGI_FORMAT_R8G8B8A8_UNORM);
    m_downsample_mid_pass_pipeline = b.Create(device, m_shader_cache, false);
  }
  {
    const auto fs = D3DCommon::EmbeddedShaders::PickAdaptiveDownsampleBlurFS();
    D3D12::GraphicsPipelineBuilder b;
    b.SetRootSignature(m_single_sampler_root_signature.Get());
    b.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
    b.SetVertexShader(uv_vs);
    b.SetPixelShader(D3D12_SHADER_BYTECODE{fs.data, fs.size});
    b.SetNoCullRasterizationState();
    b.SetNoDepthTestState();
    b.SetNoBlendingState();
    b.SetRenderTarget(0, DXGI_FORMAT_R8_UNORM);
    m_downsample_blur_pass_pipeline = b.Create(device, m_shader_cache, false);
  }
  {
    const D3D12_SHADER_BYTECODE composite_vs = GetFullscreenQuadVertexShader();
    const auto fs = D3DCommon::EmbeddedShaders::PickAdaptiveDownsampleCompositeFS(m_resolution_scale);
    D3D12::GraphicsPipelineBuilder b;
    b.SetRootSignature(m_downsample_composite_root_signature.Get());
    b.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
    b.SetVertexShader(composite_vs);
    b.SetPixelShader(D3D12_SHADER_BYTECODE{fs.data, fs.size});
    b.SetNoCullRasterizationState();
    b.SetNoDepthTestState();
    b.SetNoBlendingState();
    b.SetRenderTarget(0, m_display_texture.GetFormat());
    m_downsample_composite_pipeline = b.Create(device, m_shader_cache, false);
  }

  if (!m_downsample_first_pass_pipeline || !m_downsample_mid_pass_pipeline || !m_downsample_blur_pass_pipeline ||
      !m_downsample_composite_pipeline)
  {
    return false;
  }

  D3D12::SetObjectName(m_downsample_first_pass_pipeline.Get(), "Adaptive Downsample First Pass");
  D3D12::SetObjectName(m_downsample_mid_pass_pipeline.Get(), "Adaptive Downsample Mid Pass");
  D3D12::SetObjectName(m_downsample_blur_pass_pipeline.Get(), "Adaptive Downsample Blur Pass");
  D3D12::SetObjectName(m_downsample_composite_pipeline.Get(), "Adaptive Downsample Composite");
  return true;
}

void GPU_HW_D3D12::StartShaderCompileThread()
{
  retro_atomic_store_release_int(&m_shader_compile_thread_quit, 0);
  m_shader_compile_thread = sthread_create(
    [](void* self) { static_cast<GPU_HW_D3D12*>(self)->ShaderCompileThreadEntryPoint(); }, this);
}

void GPU_HW_D3D12::StopShaderCompileThread()
{
  if (!m_shader_compile_thread)
    return;

  retro_atomic_store_release_int(&m_shader_compile_thread_quit, 1);
  sthread_join(m_shader_compile_thread);
  m_shader_compile_thread = nullptr;
  retro_atomic_store_release_int(&m_shader_compile_thread_quit, 0);
}

void GPU_HW_D3D12::ShaderCompileThreadEntryPoint()
{
  // Lower this worker's scheduling priority to "below normal" so
  // it doesn't compete with the runloop / CPU emulation / audio
  // threads on CPU-contended systems. Best-effort: if the platform
  // refuses we just keep going at default priority. See
  // common/thread_priority.h for the per-platform mechanics.
  ThreadPriority::LowerCurrentThreadPriority();

  // The non-batch pipelines (VRAM ops, display, copy/blit) are
  // pre-filled by the main thread in CompilePipelines before the
  // worker is launched - see the comment block on the Lazy branch
  // there for why. The worker only walks the batch matrix.
  //
  // Walks the PSO matrix in (depth_test, render_mode,
  // transparency_mode, texture_mode) order
  // - the same order CompilePipelines uses for the Enabled precompile
  // loop - and calls GetBatchPipeline on each cell. GetBatchPipeline
  // pulls the FS bytecode from the pre-baked pickers and the VS from
  // PickBatchVertexShader, then builds + caches the PSO, so the
  // worker fills the PSO array as it goes. The quit flag is checked
  // between cells so DestroyPipelines can bring the worker down within
  // at most one PSO compile worth of latency (D3D12 PSO compiles can
  // be ~50-200 ms for the heavier filters - the same cell-level bound
  // applies
  // to ShaderCompileThreadEntryPoint on the D3D11 side).
  //
  // Structurally unreachable cells are skipped via
  // IsBatchShaderReachable.
  //
  // The worker captures the runtime-current m_texture_filtering at
  // launch time and walks only that filter's sub-cube. Mirrors what
  // CompilePipelines does in precompile_sync mode - both paths fill
  // the active filter's sub-cube and leave the other six sub-cubes
  // empty, to be lazy-faulted in via GetBatchPipeline on the main
  // thread if a filter toggle later brings them into use.
  // UpdateSettings calls StopShaderCompileThread BEFORE
  // UpdateHWSettings writes m_texture_filtering, so the snapshot here
  // matches the filter the worker is supposed to be warming - the
  // next CompilePipelines starts a fresh worker for the new filter.
  const bool dual_source = m_supports_dual_source_blend;
  const GPUTextureFilter cur_filter = m_texture_filtering;
  for (uint8_t depth_test = 0; depth_test < 2; depth_test++)
  {
    for (uint8_t render_mode = 0; render_mode < 4; render_mode++)
    {
      for (uint8_t transparency_mode = 0; transparency_mode < 5; transparency_mode++)
      {
        for (uint8_t texture_mode = 0; texture_mode < 9; texture_mode++)
        {
          if (!IsBatchShaderReachable(static_cast<BatchRenderMode>(render_mode), texture_mode, dual_source))
            continue;

          if (retro_atomic_load_acquire_int(&m_shader_compile_thread_quit))
            return;

          GetBatchPipeline(cur_filter, depth_test, render_mode, texture_mode, transparency_mode);
        }
      }
    }
  }
}

GPU_HW_D3D12::ComPtr<ID3D12PipelineState> GPU_HW_D3D12::GetBatchPipeline(GPUTextureFilter filter, uint8_t depth_test, uint8_t render_mode,
                                                                         uint8_t texture_mode, uint8_t transparency_mode)
{
  // Reserved_*Direct16Bit PSO dedup. Because the only texture_mode-
  // dependent input to the PSO is the bound fragment shader (which
  // the pre-baked picker collapses - Reserved_Direct16Bit and
  // Direct16Bit map to the same .inc blob, likewise the Raw
  // variants), the resulting PSO for the Reserved_* modes is bit-
  // identical to the canonical mode and both share the canonical
  // slot.
  const uint8_t lookup_mode = (texture_mode == static_cast<uint8_t>(GPUTextureMode::Reserved_Direct16Bit))    ? 2u :
                         (texture_mode == static_cast<uint8_t>(GPUTextureMode::Reserved_RawDirect16Bit)) ? 6u :
                                                                                                      texture_mode;
  const uint8_t filter_idx = static_cast<uint8_t>(filter);

  // Fast path: lock-free acquire-load. DrawBatchVertices hits this
  // once a slot is filled (either by the precompile worker or by
  // an earlier main-thread fault-in). No mutex, no contention
  // against the worker.
  AtomicSlot<ID3D12PipelineState*>& fast_slot =
    m_batch_pipelines_fastpath[filter_idx][depth_test][render_mode][lookup_mode][transparency_mode];
  if (ID3D12PipelineState* existing = fast_slot.Load())
    return ComPtr<ID3D12PipelineState>(existing);

  // Build the PSO. The on-disk PSO cache
  // (m_shader_cache.GetPipelineState) is thread-safe (the pipeline
  // library is free-threaded), so multiple threads can build different
  // PSOs here in parallel. Two threads building the SAME PSO is
  // harmless - both produce identical state objects and the cache
  // dedups internally via its own double-check.
  //
  // Every batch FS variant is pre-baked (the batch FS pre-bake arc
  // completed at f2620c1). The fragment-shader bytecode comes from
  // the D3DCommon::EmbeddedShaders pickers:
  //   - Untextured (lookup_mode == GPUTextureMode::Disabled):
  //     PickBatchUntexturedFS (12-variant table).
  //   - Textured + Nearest:   PickBatchTexturedNearestFS (72).
  //   - Textured + Bilinear:  PickBatchTexturedBilinearFS (144).
  //   - Textured + JINC2:     PickBatchTexturedJINC2FS (144).
  //   - Textured + xBR:       PickBatchTexturedXBRFS (144).
  // No runtime shadergen + D3DCompile remains on this path.
  const bool textured = (static_cast<GPUTextureMode>(lookup_mode) != GPUTextureMode::Disabled);

  // Mirror of the shadergen formula in
  // GPU_HW_ShaderGen::GenerateBatchFragmentShader. The bit drives
  // both the FS variant choice (via the PickBatch*FS pickers below)
  // AND the PSO blend state (via SetBlendState below for SRC1_*-
  // using cells), so it must come from the same input set for both
  // consumers. Identical for textured / untextured slices.
  const bool use_dual_source =
    m_supports_dual_source_blend &&
    ((render_mode != static_cast<uint8_t>(GPU_HW::BatchRenderMode::TransparencyDisabled) &&
      render_mode != static_cast<uint8_t>(GPU_HW::BatchRenderMode::OnlyOpaque)) ||
     filter != GPUTextureFilter::Nearest);

  D3D12_SHADER_BYTECODE fs_bc;
  if (textured)
  {
    if (filter == GPUTextureFilter::Nearest)
    {
      // Second pre-baked batch FS slice (9e4c33d + a7f5717).
      // Shared picker in D3DCommon::EmbeddedShaders selects from 72
      // .inc blobs at src/common/d3d_common/embedded_dxbc/
      // batch_textured_nearest_ps_*.inc; no D3DCompile, no
      // shader-cache lookup. The Bytecode -> D3D12_SHADER_BYTECODE
      // conversion is a 2-field copy.
      const auto bc = D3DCommon::EmbeddedShaders::PickBatchTexturedNearestFS(
        lookup_mode, use_dual_source, m_multisamples, m_per_sample_shading,
        m_disable_color_perspective);
      fs_bc.pShaderBytecode = bc.data;
      fs_bc.BytecodeLength = bc.size;
    }
    else if (filter == GPUTextureFilter::Bilinear ||
             filter == GPUTextureFilter::BilinearBinAlpha)
    {
      // Third pre-baked batch FS slice (269a2a0 foundation + this
      // commit's activation). The two enum values share a single
      // HLSL template + 144-blob table via a BINALPHA -D macro;
      // the picker takes the binalpha bool to select between the
      // sub-cubes (BilinearBinAlpha => binalpha=true, b1 .inc
      // suffix; Bilinear => binalpha=false, b0 .inc suffix). Same
      // shape as the Nearest picker; the use_dual_source bit is
      // identical between Nearest and Bilinear because the
      // use_dual_source formula above already collapses
      // `filter != Nearest` to a single true for every
      // non-Nearest filter.
      const bool binalpha = (filter == GPUTextureFilter::BilinearBinAlpha);
      const auto bc = D3DCommon::EmbeddedShaders::PickBatchTexturedBilinearFS(
        lookup_mode, binalpha, use_dual_source, m_multisamples,
        m_per_sample_shading, m_disable_color_perspective);
      fs_bc.pShaderBytecode = bc.data;
      fs_bc.BytecodeLength = bc.size;
    }
    else if (filter == GPUTextureFilter::JINC2 ||
             filter == GPUTextureFilter::JINC2BinAlpha)
    {
      // Fourth pre-baked batch FS slice (17a0c66 foundation + this
      // commit's activation). Same picker shape as Bilinear; the
      // body of the picked DXBC is what differs (16-tap sinc-
      // windowed resampler with anti-ringing vs 4-tap bilinear).
      // binalpha drives the BINALPHA -D arm of the JINC2 template
      // - JINC2BinAlpha quantises the resampler-weighted alpha
      // to {0, 1} before the `ialpha < 0.5 ? discard : ...` test.
      const bool binalpha = (filter == GPUTextureFilter::JINC2BinAlpha);
      const auto bc = D3DCommon::EmbeddedShaders::PickBatchTexturedJINC2FS(
        lookup_mode, binalpha, use_dual_source, m_multisamples,
        m_per_sample_shading, m_disable_color_perspective);
      fs_bc.pShaderBytecode = bc.data;
      fs_bc.BytecodeLength = bc.size;
    }
    else
    {
      // Fifth and final pre-baked batch FS slice (e07ce04 foundation
      // + this commit's activation). The remaining filter values are
      // xBR / xBRBinAlpha by elimination. Same picker shape as
      // Bilinear / JINC2; the body of the picked DXBC is xBR's
      // 5x5 neighbourhood + 4-quadrant blend decision tree + per-
      // quadrant line-blend special cases. binalpha drives the
      // BINALPHA -D arm: xBRBinAlpha quantises the blend-weighted
      // alpha to {0, 1} before the `ialpha < 0.5 ? discard : ...`
      // test.
      //
      // GPUTextureFilter::Count is unreachable here (the filter enum
      // is set from settings and validated at parse time), so this
      // arm covers exactly {xBR, xBRBinAlpha}. The shadergen +
      // D3DCompile fallback that used to live here was removed when
      // xBR became pre-baked; the GetBatchFragmentShader helper that
      // backed it has been deleted from this TU entirely (the batch
      // FS path is 100% pre-baked).
      const bool binalpha = (filter == GPUTextureFilter::xBRBinAlpha);
      const auto bc = D3DCommon::EmbeddedShaders::PickBatchTexturedXBRFS(
        lookup_mode, binalpha, use_dual_source, m_multisamples,
        m_per_sample_shading, m_disable_color_perspective);
      fs_bc.pShaderBytecode = bc.data;
      fs_bc.BytecodeLength = bc.size;
    }
  }
  else
  {
    const auto bc = D3DCommon::EmbeddedShaders::PickBatchUntexturedFS(
      use_dual_source, m_multisamples, m_per_sample_shading,
      m_disable_color_perspective);
    fs_bc.pShaderBytecode = bc.data;
    fs_bc.BytecodeLength = bc.size;
  }

  D3D12::GraphicsPipelineBuilder gpbuilder;
  gpbuilder.SetRootSignature(m_batch_root_signature.Get());
  gpbuilder.SetRenderTarget(0, m_vram_texture.GetFormat());
  gpbuilder.SetDepthStencilFormat(m_vram_depth_texture.GetFormat());

  gpbuilder.AddVertexAttribute("ATTR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(BatchVertex, x));
  gpbuilder.AddVertexAttribute("ATTR", 1, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(BatchVertex, color));
  if (textured)
  {
    gpbuilder.AddVertexAttribute("ATTR", 2, DXGI_FORMAT_R32_UINT, 0, offsetof(BatchVertex, u));
    gpbuilder.AddVertexAttribute("ATTR", 3, DXGI_FORMAT_R32_UINT, 0, offsetof(BatchVertex, texpage));
    // ATTR4 / a_uv_limits is now bound unconditionally when
    // textured. Pre-UV_LIMITS-routing this was gated on
    // m_using_uv_limits because the shadergen only declared the
    // input when m_uv_limits was true. Post-routing, the shadergen
    // always emits the a_uv_limits VS input + v_uv_limits varying;
    // the FS gates whether to consume the value via the
    // u_uv_limits cbuffer scalar. BatchVertex always carries the
    // uv_limits field (gpu_hw.h:47), so the binding always points
    // at a valid 4-byte slot regardless of whether
    // ComputePolygonUVLimits ran on the vertex.
    gpbuilder.AddVertexAttribute("ATTR", 4, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(BatchVertex, uv_limits));
  }

  gpbuilder.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
  {
    // Pre-baked batch VS DXBC (batch_vertex_vs_{untextured,textured}.inc),
    // wrapped in D3D12_SHADER_BYTECODE the same way the fullscreen-quad
    // VS is. No runtime D3DCompile - this was the last shadergen call on
    // the D3D12 backend.
    const auto vs_bc = D3DCommon::EmbeddedShaders::PickBatchVertexShader(textured);
    gpbuilder.SetVertexShader(D3D12_SHADER_BYTECODE{vs_bc.data, vs_bc.size});
  }
  gpbuilder.SetPixelShader(fs_bc);

  gpbuilder.SetRasterizationState(D3D12_FILL_MODE_SOLID, D3D12_CULL_MODE_NONE, false);
  gpbuilder.SetDepthState(true, true,
                          (depth_test != 0) ? (m_pgxp_depth_buffer ? D3D12_COMPARISON_FUNC_LESS_EQUAL :
                                                                     D3D12_COMPARISON_FUNC_GREATER_EQUAL) :
                                              D3D12_COMPARISON_FUNC_ALWAYS);
  gpbuilder.SetNoBlendingState();
  gpbuilder.SetMultisamples(m_multisamples);

  // Blend state uses the FILTER PARAMETER (not m_texture_filtering)
  // so a precompile-worker thread or a main-thread lazy fault for a
  // non-current filter's sub-cube builds the PSO with that sub-cube's
  // blend mode. The non-current sub-cube would otherwise inherit
  // whatever filter the runtime happens to be at when this slot
  // first gets faulted in - benign for Nearest <-> Nearest revisits,
  // but produces wrong blend state when a Bilinear PSO is reached
  // via a fault during a Nearest session and ends up running later
  // after a filter switch.
  if ((static_cast<GPUTransparencyMode>(transparency_mode) != GPUTransparencyMode::Disabled &&
       (static_cast<BatchRenderMode>(render_mode) != BatchRenderMode::TransparencyDisabled &&
        static_cast<BatchRenderMode>(render_mode) != BatchRenderMode::OnlyOpaque)) ||
      filter != GPUTextureFilter::Nearest)
  {
    gpbuilder.SetBlendState(
      0, true, D3D12_BLEND_ONE,
      m_supports_dual_source_blend ? D3D12_BLEND_SRC1_ALPHA : D3D12_BLEND_SRC_ALPHA,
      (static_cast<GPUTransparencyMode>(transparency_mode) == GPUTransparencyMode::BackgroundMinusForeground &&
       static_cast<BatchRenderMode>(render_mode) != BatchRenderMode::TransparencyDisabled &&
       static_cast<BatchRenderMode>(render_mode) != BatchRenderMode::OnlyOpaque) ?
        D3D12_BLEND_OP_REV_SUBTRACT :
        D3D12_BLEND_OP_ADD,
      D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD);
  }

  ComPtr<ID3D12PipelineState> fresh_pso = gpbuilder.Create(g_d3d12_context->GetDevice(), m_shader_cache);
  if (!fresh_pso)
  {
    Log_ErrorPrintf("Lazy batch PSO compile failed for (f=%u, dt=%u, rm=%u, tm=%u, tr=%u)",
                    static_cast<uint8_t>(filter), depth_test, render_mode, texture_mode, transparency_mode);
    return {};
  }

  // Publish step. The loser of a race gets the winner's PSO and
  // drops its own.
  D3D12::SetObjectNameFormatted(fresh_pso.Get(), "Batch Pipeline f%u,%u,%u,%u,%u", filter_idx, depth_test, render_mode,
                                lookup_mode, transparency_mode);
  return ComPtr<ID3D12PipelineState>(PublishComSlot(fast_slot, fresh_pso));
}

// ----------------------------------------------------------------------
// Non-batch lazy helpers. Same fast-path / slow-path layout as
// GetBatchPipeline: an acquire-load on the slot, and on a miss a
// build published by compare-exchange. The fast path is one
// uncontended atomic load per call once the slot is filled.
//
// The shared fullscreen-quad vertex shader is pre-baked DXBC (see
// GetFullscreenQuadVertexShader and src/common/d3d_common/embedded_shaders.h),
// statically linked - no compile, no caching state, no mutex. PSO
// helpers below treat it as a free dependency.
// ----------------------------------------------------------------------

D3D12_SHADER_BYTECODE GPU_HW_D3D12::GetFullscreenQuadVertexShader()
{
  // Pre-baked DXBC blob - no compile, no caching state, no mutex. The
  // blob is statically linked from src/common/d3d12/embedded_dxbc/
  // (generated offline by tools/regen_d3d_common_dxbc.py from
  // data/shaders/d3d_common/fullscreen_quad.vs.hlsl). All callers receive
  // the same const view; the storage outlives every PSO that binds it.
  return D3D12_SHADER_BYTECODE{
    D3DCommon::EmbeddedShaders::k_fullscreen_quad_vs,
    D3DCommon::EmbeddedShaders::k_fullscreen_quad_vs_size_bytes,
  };
}

GPU_HW_D3D12::ComPtr<ID3D12PipelineState> GPU_HW_D3D12::GetVRAMFillPipeline(uint8_t wrapped, uint8_t interlaced)
{
  AtomicSlot<ID3D12PipelineState*>& fast_slot = m_vram_fill_pipelines_fastpath[wrapped][interlaced];
  if (ID3D12PipelineState* existing = fast_slot.Load())
    return ComPtr<ID3D12PipelineState>(existing);

  const D3D12_SHADER_BYTECODE vs = GetFullscreenQuadVertexShader();

  // Pre-baked DXBC blob - 8 variants on [pgxp][wrapped][interlaced].
  // m_pgxp_depth_buffer is the runtime PGXP state; wrapped and
  // interlaced come in as function parameters. The 3-bit composite
  // selects one of 8 .inc blobs. Same selection pattern as
  // GetVRAMCopyPipeline / GetVRAMWritePipeline, just with two more
  // axes. No D3DCompile / disk-shader-cache lookup needed; the .inc
  // files ARE the cache.
  static const D3D12_SHADER_BYTECODE k_vram_fill_blobs[2][2][2] = {
    // pgxp = 0
    {
      // wrapped = 0
      {{D3DCommon::EmbeddedShaders::k_vram_fill_ps_p0w0i0,
        D3DCommon::EmbeddedShaders::k_vram_fill_ps_p0w0i0_size_bytes},
       {D3DCommon::EmbeddedShaders::k_vram_fill_ps_p0w0i1,
        D3DCommon::EmbeddedShaders::k_vram_fill_ps_p0w0i1_size_bytes}},
      // wrapped = 1
      {{D3DCommon::EmbeddedShaders::k_vram_fill_ps_p0w1i0,
        D3DCommon::EmbeddedShaders::k_vram_fill_ps_p0w1i0_size_bytes},
       {D3DCommon::EmbeddedShaders::k_vram_fill_ps_p0w1i1,
        D3DCommon::EmbeddedShaders::k_vram_fill_ps_p0w1i1_size_bytes}},
    },
    // pgxp = 1
    {
      // wrapped = 0
      {{D3DCommon::EmbeddedShaders::k_vram_fill_ps_p1w0i0,
        D3DCommon::EmbeddedShaders::k_vram_fill_ps_p1w0i0_size_bytes},
       {D3DCommon::EmbeddedShaders::k_vram_fill_ps_p1w0i1,
        D3DCommon::EmbeddedShaders::k_vram_fill_ps_p1w0i1_size_bytes}},
      // wrapped = 1
      {{D3DCommon::EmbeddedShaders::k_vram_fill_ps_p1w1i0,
        D3DCommon::EmbeddedShaders::k_vram_fill_ps_p1w1i0_size_bytes},
       {D3DCommon::EmbeddedShaders::k_vram_fill_ps_p1w1i1,
        D3DCommon::EmbeddedShaders::k_vram_fill_ps_p1w1i1_size_bytes}},
    },
  };
  const D3D12_SHADER_BYTECODE fs =
    k_vram_fill_blobs[m_pgxp_depth_buffer ? 1 : 0][wrapped & 1][interlaced & 1];

  D3D12::GraphicsPipelineBuilder gpbuilder;
  gpbuilder.SetRootSignature(m_single_sampler_root_signature.Get());
  gpbuilder.SetRenderTarget(0, m_vram_texture.GetFormat());
  gpbuilder.SetDepthStencilFormat(m_vram_depth_texture.GetFormat());
  gpbuilder.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
  gpbuilder.SetNoCullRasterizationState();
  gpbuilder.SetNoBlendingState();
  gpbuilder.SetVertexShader(vs);
  gpbuilder.SetPixelShader(fs);
  gpbuilder.SetMultisamples(m_multisamples);
  gpbuilder.SetDepthState(true, true, D3D12_COMPARISON_FUNC_ALWAYS);

  ComPtr<ID3D12PipelineState> fresh = gpbuilder.Create(g_d3d12_context->GetDevice(), m_shader_cache, false);
  if (!fresh)
    return {};

  D3D12::SetObjectNameFormatted(fresh.Get(), "VRAM Fill Pipeline Wrapped=%u,Interlacing=%u", wrapped, interlaced);
  return ComPtr<ID3D12PipelineState>(PublishComSlot(fast_slot, fresh));
}

GPU_HW_D3D12::ComPtr<ID3D12PipelineState> GPU_HW_D3D12::GetVRAMCopyPipeline(uint8_t depth_test)
{
  AtomicSlot<ID3D12PipelineState*>& fast_slot = m_vram_copy_pipelines_fastpath[depth_test];
  if (ID3D12PipelineState* existing = fast_slot.Load())
    return ComPtr<ID3D12PipelineState>(existing);

  const D3D12_SHADER_BYTECODE vs = GetFullscreenQuadVertexShader();

  // Pre-baked DXBC blob - one per PGXP_DEPTH on/off. m_pgxp_depth_buffer
  // is the runtime PGXP state that determines which o_depth path the
  // shader takes (the legacy shadergen took m_pgxp_depth from the
  // shadergen ctor, which is m_pgxp_depth_buffer for non-batch shaders).
  // No D3DCompile / disk-shader-cache lookup needed; the .inc file IS
  // the cache. RESOLUTION_SCALE is cbuffer-routed (e56d4d4), so one
  // blob per PGXP value serves every resolution scale.
  const D3D12_SHADER_BYTECODE fs =
    m_pgxp_depth_buffer
      ? D3D12_SHADER_BYTECODE{D3DCommon::EmbeddedShaders::k_vram_copy_ps_pgxp1,
                              D3DCommon::EmbeddedShaders::k_vram_copy_ps_pgxp1_size_bytes}
      : D3D12_SHADER_BYTECODE{D3DCommon::EmbeddedShaders::k_vram_copy_ps_pgxp0,
                              D3DCommon::EmbeddedShaders::k_vram_copy_ps_pgxp0_size_bytes};

  D3D12::GraphicsPipelineBuilder gpbuilder;
  gpbuilder.SetRootSignature(m_single_sampler_root_signature.Get());
  gpbuilder.SetRenderTarget(0, m_vram_texture.GetFormat());
  gpbuilder.SetDepthStencilFormat(m_vram_depth_texture.GetFormat());
  gpbuilder.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
  gpbuilder.SetNoCullRasterizationState();
  gpbuilder.SetNoBlendingState();
  gpbuilder.SetVertexShader(vs);
  gpbuilder.SetPixelShader(fs);
  gpbuilder.SetMultisamples(m_multisamples);
  gpbuilder.SetDepthState((depth_test != 0), true,
                          (depth_test != 0) ? D3D12_COMPARISON_FUNC_GREATER_EQUAL : D3D12_COMPARISON_FUNC_ALWAYS);

  ComPtr<ID3D12PipelineState> fresh = gpbuilder.Create(g_d3d12_context->GetDevice(), m_shader_cache, false);
  if (!fresh)
    return {};

  D3D12::SetObjectNameFormatted(fresh.Get(), "VRAM Copy Pipeline Depth=%u", depth_test);
  return ComPtr<ID3D12PipelineState>(PublishComSlot(fast_slot, fresh));
}

GPU_HW_D3D12::ComPtr<ID3D12PipelineState> GPU_HW_D3D12::GetVRAMWritePipeline(uint8_t depth_test)
{
  AtomicSlot<ID3D12PipelineState*>& fast_slot = m_vram_write_pipelines_fastpath[depth_test];
  if (ID3D12PipelineState* existing = fast_slot.Load())
    return ComPtr<ID3D12PipelineState>(existing);

  const D3D12_SHADER_BYTECODE vs = GetFullscreenQuadVertexShader();

  // Pre-baked DXBC blob - one per PGXP_DEPTH on/off. Same selection
  // pattern as GetVRAMCopyPipeline (cb4b443). m_pgxp_depth_buffer is
  // the runtime PGXP state that determines which o_depth path the
  // shader takes. No D3DCompile / disk-shader-cache lookup needed;
  // the .inc file IS the cache. RESOLUTION_SCALE is cbuffer-routed
  // (9d2b49d), so one blob per PGXP value serves every resolution
  // scale.
  const D3D12_SHADER_BYTECODE fs =
    m_pgxp_depth_buffer
      ? D3D12_SHADER_BYTECODE{D3DCommon::EmbeddedShaders::k_vram_write_ps_pgxp1,
                              D3DCommon::EmbeddedShaders::k_vram_write_ps_pgxp1_size_bytes}
      : D3D12_SHADER_BYTECODE{D3DCommon::EmbeddedShaders::k_vram_write_ps_pgxp0,
                              D3DCommon::EmbeddedShaders::k_vram_write_ps_pgxp0_size_bytes};

  D3D12::GraphicsPipelineBuilder gpbuilder;
  gpbuilder.SetRootSignature(m_single_sampler_root_signature.Get());
  gpbuilder.SetRenderTarget(0, m_vram_texture.GetFormat());
  gpbuilder.SetDepthStencilFormat(m_vram_depth_texture.GetFormat());
  gpbuilder.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
  gpbuilder.SetNoCullRasterizationState();
  gpbuilder.SetNoBlendingState();
  gpbuilder.SetVertexShader(vs);
  gpbuilder.SetPixelShader(fs);
  gpbuilder.SetMultisamples(m_multisamples);
  gpbuilder.SetDepthState(true, true,
                          (depth_test != 0) ? D3D12_COMPARISON_FUNC_GREATER_EQUAL : D3D12_COMPARISON_FUNC_ALWAYS);

  ComPtr<ID3D12PipelineState> fresh = gpbuilder.Create(g_d3d12_context->GetDevice(), m_shader_cache, false);
  if (!fresh)
    return {};

  D3D12::SetObjectNameFormatted(fresh.Get(), "VRAM Write Pipeline Depth=%u", depth_test);
  return ComPtr<ID3D12PipelineState>(PublishComSlot(fast_slot, fresh));
}

GPU_HW_D3D12::ComPtr<ID3D12PipelineState> GPU_HW_D3D12::GetVRAMUpdateDepthPipeline()
{
  if (ID3D12PipelineState* existing = m_vram_update_depth_pipeline_fastpath.Load())
    return ComPtr<ID3D12PipelineState>(existing);

  const D3D12_SHADER_BYTECODE vs = GetFullscreenQuadVertexShader();

  // Pre-baked DXBC blob - 2 variants on MULTISAMPLING. Unlike the
  // PGXP / WRAPPED / INTERLACED axes on the other VRAM ops, MSAA
  // here means the two blobs have different *binding* types
  // (Texture2D vs Texture2DMS<float4>) plus a conditional
  // SV_SampleIndex input. The PSO's MSAA configuration must match
  // the shader's expectation - we pick the msaa1 blob when
  // m_multisamples > 1 (the same predicate the shadergen path's
  // UsingMSAA() helper uses). gpbuilder.SetMultisamples below
  // continues to drive the PSO MSAA state from m_multisamples
  // unchanged.
  const D3D12_SHADER_BYTECODE fs =
    (m_multisamples > 1)
      ? D3D12_SHADER_BYTECODE{D3DCommon::EmbeddedShaders::k_vram_update_depth_ps_msaa1,
                              D3DCommon::EmbeddedShaders::k_vram_update_depth_ps_msaa1_size_bytes}
      : D3D12_SHADER_BYTECODE{D3DCommon::EmbeddedShaders::k_vram_update_depth_ps_msaa0,
                              D3DCommon::EmbeddedShaders::k_vram_update_depth_ps_msaa0_size_bytes};

  // VRAM update depth differs from the other VRAM ops in three
  // ways: it uses m_batch_root_signature (the regular ops use the
  // single-sampler root sig), it writes to depth only (no render
  // targets - ClearRenderTargets() drops the RT[0] entry the
  // helper would otherwise inherit), and it carries an explicit
  // blend state. Everything else - VS, topology, cull, multisamples,
  // depth-stencil format, depth-state - matches the standard
  // fullscreen-quad pipeline the other VRAM ops use.
  D3D12::GraphicsPipelineBuilder gpbuilder;
  gpbuilder.SetRootSignature(m_batch_root_signature.Get());
  gpbuilder.SetDepthStencilFormat(m_vram_depth_texture.GetFormat());
  gpbuilder.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
  gpbuilder.SetNoCullRasterizationState();
  gpbuilder.SetVertexShader(vs);
  gpbuilder.SetPixelShader(fs);
  gpbuilder.SetMultisamples(m_multisamples);
  gpbuilder.SetDepthState(true, true, D3D12_COMPARISON_FUNC_ALWAYS);
  gpbuilder.SetBlendState(0, false, D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD, D3D12_BLEND_ONE,
                          D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD, 0);
  gpbuilder.ClearRenderTargets();

  ComPtr<ID3D12PipelineState> fresh = gpbuilder.Create(g_d3d12_context->GetDevice(), m_shader_cache, false);
  if (!fresh)
    return {};

  D3D12::SetObjectName(fresh.Get(), "VRAM Update Depth Pipeline");
  return ComPtr<ID3D12PipelineState>(PublishComSlot(m_vram_update_depth_pipeline_fastpath, fresh));
}

GPU_HW_D3D12::ComPtr<ID3D12PipelineState> GPU_HW_D3D12::GetVRAMReadbackPipeline()
{
  if (ID3D12PipelineState* existing = m_vram_readback_pipeline_fastpath.Load())
    return ComPtr<ID3D12PipelineState>(existing);

  const D3D12_SHADER_BYTECODE vs = GetFullscreenQuadVertexShader();

  // Pre-baked DXBC blob - 6 variants on MULTISAMPLES (1, 2, 4,
  // 8, 16, 32). Where vram_update_depth (91f8c32) was a binary
  // MSAA on/off split, vram_read_ps splits on the sample-count
  // cardinality itself: the [unroll] sample-resolve loop in the
  // shader's LoadVRAM helper unrolls a different number of times
  // per blob, so each power-of-2 count produces a distinct DXBC.
  // PSO MSAA configuration (gpbuilder.SetMultisamples below)
  // must match the shader's MULTISAMPLES constant - both come
  // from m_multisamples, so the consistency is automatic.
  //
  // The switch only handles power-of-2 values up to 32 because
  // GPU drivers only expose those as having quality levels > 0
  // (see m_max_multisamples detection loop in CreateResources),
  // and the libretro UI dropdown is restricted to those values.
  // The default branch is a safety net that falls back to m1
  // and warns - hitting it would indicate a future driver/UI
  // mismatch that wants investigation.
  D3D12_SHADER_BYTECODE fs;
  switch (m_multisamples)
  {
    case 1:
      fs = {D3DCommon::EmbeddedShaders::k_vram_read_ps_m1,
            D3DCommon::EmbeddedShaders::k_vram_read_ps_m1_size_bytes};
      break;
    case 2:
      fs = {D3DCommon::EmbeddedShaders::k_vram_read_ps_m2,
            D3DCommon::EmbeddedShaders::k_vram_read_ps_m2_size_bytes};
      break;
    case 4:
      fs = {D3DCommon::EmbeddedShaders::k_vram_read_ps_m4,
            D3DCommon::EmbeddedShaders::k_vram_read_ps_m4_size_bytes};
      break;
    case 8:
      fs = {D3DCommon::EmbeddedShaders::k_vram_read_ps_m8,
            D3DCommon::EmbeddedShaders::k_vram_read_ps_m8_size_bytes};
      break;
    case 16:
      fs = {D3DCommon::EmbeddedShaders::k_vram_read_ps_m16,
            D3DCommon::EmbeddedShaders::k_vram_read_ps_m16_size_bytes};
      break;
    case 32:
      fs = {D3DCommon::EmbeddedShaders::k_vram_read_ps_m32,
            D3DCommon::EmbeddedShaders::k_vram_read_ps_m32_size_bytes};
      break;
    default:
      Log_WarningPrintf(
        "GetVRAMReadbackPipeline: unexpected m_multisamples=%u not in "
        "{1,2,4,8,16,32}; falling back to m1 blob (VRAM readback may "
        "average samples incorrectly)",
        m_multisamples);
      fs = {D3DCommon::EmbeddedShaders::k_vram_read_ps_m1,
            D3DCommon::EmbeddedShaders::k_vram_read_ps_m1_size_bytes};
      break;
  }

  D3D12::GraphicsPipelineBuilder gpbuilder;
  gpbuilder.SetRootSignature(m_single_sampler_root_signature.Get());
  gpbuilder.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
  gpbuilder.SetVertexShader(vs);
  gpbuilder.SetPixelShader(fs);
  gpbuilder.SetNoCullRasterizationState();
  gpbuilder.SetNoDepthTestState();
  gpbuilder.SetNoBlendingState();
  gpbuilder.SetRenderTarget(0, m_vram_readback_texture.GetFormat());
  gpbuilder.ClearDepthStencilFormat();

  ComPtr<ID3D12PipelineState> fresh = gpbuilder.Create(g_d3d12_context->GetDevice(), m_shader_cache, false);
  if (!fresh)
    return {};

  D3D12::SetObjectName(fresh.Get(), "VRAM Readback Pipeline");
  return ComPtr<ID3D12PipelineState>(PublishComSlot(m_vram_readback_pipeline_fastpath, fresh));
}

GPU_HW_D3D12::ComPtr<ID3D12PipelineState> GPU_HW_D3D12::GetDisplayPipeline(uint8_t depth_24, uint8_t interlace_mode)
{
  AtomicSlot<ID3D12PipelineState*>& fast_slot = m_display_pipelines_fastpath[depth_24][interlace_mode];
  if (ID3D12PipelineState* existing = fast_slot.Load())
    return ComPtr<ID3D12PipelineState>(existing);

  const D3D12_SHADER_BYTECODE vs = GetFullscreenQuadVertexShader();

  // Pre-baked DXBC blob, selected by the shared PickDisplayFS picker
  // (embedded_shaders.cpp) - 54 variants on
  // depth_24 x interlace_mode x smooth_chroma x multisamples. This
  // replaced the inline k_display_d0 / k_display_d1 pointer tables +
  // the MultisamplesIndex lambda that used to live here; the D3D11
  // backend's RebuildDisplayPixelShaders calls the same picker, so the
  // selection logic (the ms_idx switch with the m1 fallback, and the
  // depth_24 ? d1[chroma] : d0 choice) is centralised in one place.
  // smooth_chroma is meaningful only on the depth_24 path - the picker
  // ignores it for depth_24 == 0 (the 16-bit body never touches the
  // chroma helpers, so fxc emits identical DXBC for c0/c1 there).
  const auto fs_bc =
    D3DCommon::EmbeddedShaders::PickDisplayFS(depth_24 != 0, interlace_mode, m_chroma_smoothing, m_multisamples);
  const D3D12_SHADER_BYTECODE fs{fs_bc.data, fs_bc.size};

  D3D12::GraphicsPipelineBuilder gpbuilder;
  gpbuilder.SetRootSignature(m_single_sampler_root_signature.Get());
  gpbuilder.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
  gpbuilder.SetVertexShader(vs);
  gpbuilder.SetPixelShader(fs);
  gpbuilder.SetNoCullRasterizationState();
  gpbuilder.SetNoDepthTestState();
  gpbuilder.SetNoBlendingState();
  gpbuilder.SetRenderTarget(0, m_display_texture.GetFormat());

  ComPtr<ID3D12PipelineState> fresh = gpbuilder.Create(g_d3d12_context->GetDevice(), m_shader_cache, false);
  if (!fresh)
    return {};

  D3D12::SetObjectNameFormatted(fresh.Get(), "Display Pipeline Depth=%u Interlace=%u", depth_24, interlace_mode);
  return ComPtr<ID3D12PipelineState>(PublishComSlot(fast_slot, fresh));
}

GPU_HW_D3D12::ComPtr<ID3D12PipelineState> GPU_HW_D3D12::GetCopyPipeline()
{
  if (ID3D12PipelineState* existing = m_copy_pipeline_fastpath.Load())
    return ComPtr<ID3D12PipelineState>(existing);

  const D3D12_SHADER_BYTECODE vs = GetFullscreenQuadVertexShader();

  // Pre-baked DXBC blob, same shape as the VS - statically linked
  // from src/common/d3d_common/embedded_dxbc/copy_ps.inc, single
  // variant, no shader_cache lookup, no D3DCompile, no shadergen
  // dependency. The PSO compile below is the only remaining work.
  const D3D12_SHADER_BYTECODE fs = {
    D3DCommon::EmbeddedShaders::k_copy_ps,
    D3DCommon::EmbeddedShaders::k_copy_ps_size_bytes,
  };

  D3D12::GraphicsPipelineBuilder gpbuilder;
  gpbuilder.SetRootSignature(m_single_sampler_root_signature.Get());
  gpbuilder.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
  gpbuilder.SetVertexShader(vs);
  gpbuilder.SetPixelShader(fs);
  gpbuilder.SetNoCullRasterizationState();
  gpbuilder.SetNoDepthTestState();
  gpbuilder.SetNoBlendingState();
  gpbuilder.SetRenderTarget(0, DXGI_FORMAT_R8G8B8A8_UNORM);

  ComPtr<ID3D12PipelineState> fresh = gpbuilder.Create(g_d3d12_context->GetDevice(), m_shader_cache, false);
  if (!fresh)
    return {};

  D3D12::SetObjectName(fresh.Get(), "Copy/Blit Pipeline");
  return ComPtr<ID3D12PipelineState>(PublishComSlot(m_copy_pipeline_fastpath, fresh));
}

void GPU_HW_D3D12::DestroyPipelines()
{
  // Tear down the background-compile worker before clearing the
  // matrix - otherwise it could still be publishing into it.
  StopShaderCompileThread();

  // The worker is stopped and the runloop isn't drawing
  // (UpdateSettings is the only call site that runs DestroyPipelines,
  // and it's on the runloop thread), so the slots are released in
  // place.
  const auto release = [](AtomicSlot<ID3D12PipelineState*>& slot) { ReleaseComSlot(slot); };
  m_batch_pipelines_fastpath.enumerate(release);
  m_vram_fill_pipelines_fastpath.enumerate(release);
  for (auto& slot : m_vram_copy_pipelines_fastpath)
    release(slot);
  for (auto& slot : m_vram_write_pipelines_fastpath)
    release(slot);
  m_display_pipelines_fastpath.enumerate(release);
  release(m_vram_readback_pipeline_fastpath);
  release(m_vram_update_depth_pipeline_fastpath);
  release(m_copy_pipeline_fastpath);

  m_downsample_pipeline.Reset();
  m_downsample_first_pass_pipeline.Reset();
  m_downsample_mid_pass_pipeline.Reset();
  m_downsample_blur_pass_pipeline.Reset();
  m_downsample_composite_pipeline.Reset();
}

void GPU_HW_D3D12::ClearDisplayPipelines()
{
  // Partial destroy: drop just the display PSO cache. Used by
  // UpdateSettings on a chroma_smoothing-only flip. chroma_smoothing
  // selects among the pre-baked display FS variants, so the batch
  // matrix and the VRAM ops PSOs stay valid; only the display PSOs
  // need rebuilding. The next UpdateDisplay call's GetDisplayPipeline
  // lazy-faults each (depth_24, interlace_mode) slot, picking the
  // pre-baked display DXBC for the new m_chroma_smoothing.
  //
  // No StopShaderCompileThread needed - the worker only walks the
  // batch matrix. UpdateSettings has already executed the command
  // list to completion via g_d3d12_context->ExecuteCommandList(true),
  // so no in-flight draw references these PSOs.
  m_display_pipelines_fastpath.enumerate([](AtomicSlot<ID3D12PipelineState*>& slot) { ReleaseComSlot(slot); });
}

bool GPU_HW_D3D12::CreateTextureReplacementStreamBuffer()
{
  if (m_texture_replacment_stream_buffer.IsValid())
    return true;
  if (!m_texture_replacment_stream_buffer.Create(TEXTURE_REPLACEMENT_BUFFER_SIZE))
    return false;
  return true;
}

bool GPU_HW_D3D12::BlitVRAMReplacementTexture(const TextureReplacementTexture* tex, uint32_t dst_x, uint32_t dst_y, uint32_t width,
                                              uint32_t height)
{
  if (!CreateTextureReplacementStreamBuffer())
    return false;

  if (m_vram_write_replacement_texture.GetWidth() < tex->GetWidth() ||
      m_vram_write_replacement_texture.GetHeight() < tex->GetHeight())
  {
    if (!m_vram_write_replacement_texture.Create(tex->GetWidth(), tex->GetHeight(), 1, DXGI_FORMAT_R8G8B8A8_UNORM,
                                                 DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN,
                                                 D3D12_RESOURCE_FLAG_NONE))
      return false;
  }

  const uint32_t copy_pitch = Common::AlignUpPow2<uint32_t>(tex->GetWidth() * sizeof(uint32_t), D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
  const uint32_t required_size = copy_pitch * tex->GetHeight();
  if (!m_texture_replacment_stream_buffer.ReserveMemory(required_size, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT))
  {
    g_d3d12_context->ExecuteCommandList(false);
    RestoreGraphicsAPIState();
    if (!m_texture_replacment_stream_buffer.ReserveMemory(required_size, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT))
      return false;
  }

  // buffer -> texture
  const uint32_t sb_offset = m_texture_replacment_stream_buffer.GetCurrentOffset();
  D3D12::Texture::CopyToUploadBuffer(tex->GetPixels(), tex->GetByteStride(), tex->GetHeight(),
                                     m_texture_replacment_stream_buffer.GetCurrentHostPointer(), copy_pitch);
  m_texture_replacment_stream_buffer.CommitMemory(sb_offset);
  m_vram_write_replacement_texture.CopyFromBuffer(0, 0, tex->GetWidth(), tex->GetHeight(), copy_pitch,
                                                  m_texture_replacment_stream_buffer.GetBuffer(), sb_offset);
  m_vram_write_replacement_texture.TransitionToState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

  // texture -> vram
  const float uniforms[] = {
    0.0f, 0.0f, static_cast<float>(tex->GetWidth()) / static_cast<float>(m_vram_write_replacement_texture.GetWidth()),
    static_cast<float>(tex->GetHeight()) / static_cast<float>(m_vram_write_replacement_texture.GetHeight())};
  ID3D12GraphicsCommandList* cmdlist = g_d3d12_context->GetCommandList();
  cmdlist->SetGraphicsRootSignature(m_single_sampler_root_signature.Get());
  cmdlist->SetGraphicsRoot32BitConstants(0, sizeof(uniforms) / sizeof(uint32_t), uniforms, 0);
  cmdlist->SetGraphicsRootDescriptorTable(1, m_vram_write_replacement_texture.GetSRVDescriptor());
  cmdlist->SetGraphicsRootDescriptorTable(2, m_linear_sampler.gpu_handle);
  ComPtr<ID3D12PipelineState> copy_pso = GetCopyPipeline();
  if (!copy_pso)
    return false;
  cmdlist->SetPipelineState(copy_pso.Get());
  D3D12::SetViewportAndScissor(cmdlist, dst_x, dst_y, width, height);
  cmdlist->DrawInstanced(3, 1, 0, 0);
  RestoreGraphicsAPIState();
  return true;
}

void GPU_HW_D3D12::DrawBatchVertices(BatchRenderMode render_mode, uint32_t base_vertex, uint32_t num_vertices)
{
  ID3D12GraphicsCommandList* cmdlist = g_d3d12_context->GetCommandList();

  // Fetch the batch PSO via the lazy helper. In 'Enabled' precompile
  // mode every slot was filled at CompilePipelines time so this is a
  // single acquire load. In 'Lazy' mode this either gets the
  // already-compiled PSO (background thread reached it first) or
  // compiles it now on the main thread (game raced ahead of the
  // worker). In 'Disabled' mode it always compiles on miss.
  //
  // [filter][depth_test][render_mode][texture_mode][transparency_mode]
  // m_texture_filtering selects the active filter's sub-cube. Filter
  // is the outermost dim so a filter toggle in UpdateSettings can
  // skip DestroyPipelines - the other filters' sub-cubes remain
  // valid and reachable, switching back to a previously-visited
  // filter is an atomic load on an already-filled slot.
  // The dithering bit (m_batch.dithering) reaches the PSO through
  // u_dithering on the batch UBO; the interlacing bit
  // (m_batch.interlacing) reaches it through u_interlacing. The
  // [dithering] and [interlacing] dims were dropped in the
  // matrix-collapse commits following their respective cbuffer
  // routing landings.
  const uint8_t depth_test = static_cast<uint8_t>(m_batch.check_mask_before_draw || m_batch.use_depth_buffer);
  ComPtr<ID3D12PipelineState> pipeline =
    GetBatchPipeline(m_texture_filtering, depth_test, static_cast<uint8_t>(render_mode), static_cast<uint8_t>(m_batch.texture_mode),
                     static_cast<uint8_t>(m_batch.transparency_mode));

  cmdlist->SetPipelineState(pipeline.Get());
  cmdlist->DrawInstanced(num_vertices, 1, base_vertex, 0);
}

void GPU_HW_D3D12::SetScissorFromDrawingArea()
{
  int left, top, right, bottom;
  CalcScissorRect(&left, &top, &right, &bottom);

  D3D12::SetScissor(g_d3d12_context->GetCommandList(), left, top, right - left, bottom - top);
}

void GPU_HW_D3D12::ClearDisplay()
{
  GPU_HW::ClearDisplay();

  m_host_display->ClearDisplayTexture();

  static constexpr float clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  m_display_texture.TransitionToState(D3D12_RESOURCE_STATE_RENDER_TARGET);
  g_d3d12_context->GetCommandList()->ClearRenderTargetView(m_display_texture.GetRTVOrDSVDescriptor(), clear_color, 0,
                                                           nullptr);
}

void GPU_HW_D3D12::UpdateDisplay()
{
  GPU_HW::UpdateDisplay();

  {
    m_host_display->SetDisplayParameters(m_crtc_state.display_width, m_crtc_state.display_height,
                                         m_crtc_state.display_origin_left, m_crtc_state.display_origin_top,
                                         m_crtc_state.display_vram_width, m_crtc_state.display_vram_height,
                                         GetDisplayAspectRatio());

    const uint32_t resolution_scale = m_GPUSTAT.display_area_color_depth_24 ? 1 : m_resolution_scale;
    const uint32_t vram_offset_x = m_crtc_state.display_vram_left;
    const uint32_t vram_offset_y = m_crtc_state.display_vram_top;
    const uint32_t scaled_vram_offset_x = vram_offset_x * resolution_scale;
    const uint32_t scaled_vram_offset_y = vram_offset_y * resolution_scale;
    const uint32_t display_width = m_crtc_state.display_vram_width;
    const uint32_t display_height = m_crtc_state.display_vram_height;
    const uint32_t scaled_display_width = display_width * resolution_scale;
    const uint32_t scaled_display_height = display_height * resolution_scale;
    const InterlacedRenderMode interlaced = GetInterlacedRenderMode();

    if (IsDisplayDisabled())
    {
      m_host_display->ClearDisplayTexture();
    }
    else if (!m_GPUSTAT.display_area_color_depth_24 && interlaced == InterlacedRenderMode::None &&
             !IsUsingMultisampling() && (scaled_vram_offset_x + scaled_display_width) <= m_vram_texture.GetWidth() &&
             (scaled_vram_offset_y + scaled_display_height) <= m_vram_texture.GetHeight())
    {
      m_vram_texture.TransitionToState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
      if (IsUsingDownsampling())
      {
        DownsampleFramebuffer(m_vram_texture, scaled_vram_offset_x, scaled_vram_offset_y, scaled_display_width,
                              scaled_display_height);
      }
      else
      {
        m_host_display->SetDisplayTexture(&m_vram_texture, HostDisplayPixelFormat::RGBA8, m_vram_texture.GetWidth(),
                                          m_vram_texture.GetHeight(), scaled_vram_offset_x, scaled_vram_offset_y,
                                          scaled_display_width, scaled_display_height);
      }
    }
    else
    {
      const uint32_t reinterpret_field_offset = (interlaced != InterlacedRenderMode::None) ? GetInterlacedDisplayField() : 0;
      const uint32_t reinterpret_start_x = m_crtc_state.regs.X * resolution_scale;
      const uint32_t reinterpret_crop_left = (m_crtc_state.display_vram_left - m_crtc_state.regs.X) * resolution_scale;
      // 6 DWORDs to match the post-RESOLUTION_SCALE-refactor display_ps
      // cbuffer (u_vram_offset.xy, u_crop_left, u_field_offset,
      // u_resolution_scale, u_pad0). m_resolution_scale is pushed,
      // NOT the local resolution_scale (which is forced to 1 in
      // 24-bit mode for coord scaling) - the shader's RESOLUTION_SCALE
      // macro has always been the session m_resolution_scale.
      const uint32_t uniforms[6] = {reinterpret_start_x, scaled_vram_offset_y + reinterpret_field_offset,
                               reinterpret_crop_left, reinterpret_field_offset,
                               m_resolution_scale, 0u /* u_pad0 */};

      ID3D12GraphicsCommandList* cmdlist = g_d3d12_context->GetCommandList();
      m_display_texture.TransitionToState(D3D12_RESOURCE_STATE_RENDER_TARGET);
      m_vram_texture.TransitionToState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

      cmdlist->OMSetRenderTargets(1, &m_display_texture.GetRTVOrDSVDescriptor().cpu_handle, FALSE, nullptr);
      cmdlist->SetGraphicsRootSignature(m_single_sampler_root_signature.Get());
      cmdlist->SetGraphicsRoot32BitConstants(0, sizeof(uniforms) / sizeof(uint32_t), uniforms, 0);
      cmdlist->SetGraphicsRootDescriptorTable(1, m_vram_texture.GetSRVDescriptor());
      ComPtr<ID3D12PipelineState> display_pso =
        GetDisplayPipeline(static_cast<uint8_t>(m_GPUSTAT.display_area_color_depth_24), static_cast<uint8_t>(interlaced));
      if (!display_pso)
        return;
      cmdlist->SetPipelineState(display_pso.Get());
      D3D12::SetViewportAndScissor(cmdlist, 0, 0, scaled_display_width, scaled_display_height);
      cmdlist->DrawInstanced(3, 1, 0, 0);

      m_display_texture.TransitionToState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
      m_vram_texture.TransitionToState(D3D12_RESOURCE_STATE_RENDER_TARGET);

      if (IsUsingDownsampling())
      {
        // DownsampleFramebuffer resolves m_display_texture into the native-
        // resolution m_downsample_texture and calls RestoreGraphicsAPIState
        // + SetDisplayTexture itself.
        DownsampleFramebuffer(m_display_texture, 0, 0, scaled_display_width, scaled_display_height);
      }
      else
      {
        m_host_display->SetDisplayTexture(&m_display_texture, HostDisplayPixelFormat::RGBA8,
                                          m_display_texture.GetWidth(), m_display_texture.GetHeight(), 0, 0,
                                          scaled_display_width, scaled_display_height);

        RestoreGraphicsAPIState();
      }
    }
  }
}

void GPU_HW_D3D12::DownsampleFramebuffer(D3D12::Texture& source, uint32_t left, uint32_t top, uint32_t width,
                                         uint32_t height)
{
  if (m_downsample_mode == GPUDownsampleMode::Adaptive)
    DownsampleFramebufferAdaptive(source, left, top, width, height);
  else
    DownsampleFramebufferBoxFilter(source, left, top, width, height);
}

void GPU_HW_D3D12::DownsampleFramebufferBoxFilter(D3D12::Texture& source, uint32_t left, uint32_t top, uint32_t width,
                                                  uint32_t height)
{
  // If the box downsample PSO is unavailable (CompileDownsamplePipeline
  // returned false and left m_downsample_pipeline null - e.g. a pre-baked
  // FS variant that D3D12 PSO validation rejects), do NOT proceed to
  // SetPipelineState below: ID3D12GraphicsCommandList::SetPipelineState
  // dereferences its argument and a null PSO faults. Downsampling is a
  // quality filter, not a correctness requirement, so degrade gracefully
  // by presenting the upscaled source rect directly - identical to the
  // non-downsampling path in UpdateDisplay.
  if (!m_downsample_pipeline)
  {
    m_host_display->SetDisplayTexture(&source, HostDisplayPixelFormat::RGBA8, source.GetWidth(), source.GetHeight(),
                                      left, top, width, height);
    return;
  }

  // Box-filter resolve of the upscaled source rect down to native PSX
  // resolution. The source rect is in scaled pixels; dividing by
  // m_resolution_scale gives the destination rect in the unscaled
  // m_downsample_texture. The box PS reads RESOLUTION_SCALE^2 source
  // texels per output pixel via Load (the scale is baked into the PSO),
  // so the only binding is the source SRV at root param 1 under
  // m_single_sampler_root_signature - the same Load-only binding shape as
  // the display draw, which is why no cbuffer (param 0) and no sampler
  // (param 2) are bound. The viewport is placed at ds_left/ds_top so the
  // PS's base_coords = v_pos * scale lands on the source rect's origin.
  const uint32_t ds_left = left / m_resolution_scale;
  const uint32_t ds_top = top / m_resolution_scale;
  const uint32_t ds_width = width / m_resolution_scale;
  const uint32_t ds_height = height / m_resolution_scale;
  static constexpr float clear_color[4] = {};

  ID3D12GraphicsCommandList* cmdlist = g_d3d12_context->GetCommandList();

  m_downsample_texture.TransitionToState(D3D12_RESOURCE_STATE_RENDER_TARGET);
  source.TransitionToState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

  cmdlist->ClearRenderTargetView(m_downsample_texture.GetRTVOrDSVDescriptor(), clear_color, 0, nullptr);
  cmdlist->OMSetRenderTargets(1, &m_downsample_texture.GetRTVOrDSVDescriptor().cpu_handle, FALSE, nullptr);
  cmdlist->SetGraphicsRootSignature(m_single_sampler_root_signature.Get());
  cmdlist->SetGraphicsRootDescriptorTable(1, source.GetSRVDescriptor());
  cmdlist->SetPipelineState(m_downsample_pipeline.Get());
  D3D12::SetViewportAndScissor(cmdlist, ds_left, ds_top, ds_width, ds_height);
  cmdlist->DrawInstanced(3, 1, 0, 0);

  m_downsample_texture.TransitionToState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

  RestoreGraphicsAPIState();

  m_host_display->SetDisplayTexture(&m_downsample_texture, HostDisplayPixelFormat::RGBA8,
                                    m_downsample_texture.GetWidth(), m_downsample_texture.GetHeight(), ds_left, ds_top,
                                    ds_width, ds_height);
}

void GPU_HW_D3D12::DownsampleFramebufferAdaptive(D3D12::Texture& source, uint32_t left, uint32_t top, uint32_t width,
                                                 uint32_t height)
{
  // Degrade gracefully if any adaptive PSO failed to build (a null PSO
  // faults SetPipelineState), mirroring the box-filter guard: present the
  // upscaled source rect directly.
  if (!m_downsample_first_pass_pipeline || !m_downsample_mid_pass_pipeline || !m_downsample_blur_pass_pipeline ||
      !m_downsample_composite_pipeline)
  {
    m_host_display->SetDisplayTexture(&source, HostDisplayPixelFormat::RGBA8, source.GetWidth(), source.GetHeight(),
                                      left, top, width, height);
    return;
  }

  ID3D12GraphicsCommandList* cmdlist = g_d3d12_context->GetCommandList();
  const uint32_t levels = m_downsample_mip_levels;
  const uint32_t tex_width = VRAM_WIDTH * m_resolution_scale;
  const uint32_t tex_height = VRAM_HEIGHT * m_resolution_scale;
  static constexpr float clear_color[4] = {};

  // Per-subresource (mip) transition. The pyramid is a single resource
  // but the generation loop reads mip[level-1] (PIXEL_SHADER_RESOURCE)
  // while writing mip[level] (RENDER_TARGET), so each mip's state is
  // tracked and barriered independently.
  const auto transition_mip = [&](uint32_t mip, D3D12_RESOURCE_STATES to) {
    if (m_downsample_mip_states[mip] == to)
      return;

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = m_downsample_mip_resource.Get();
    barrier.Transition.Subresource = mip;
    barrier.Transition.StateBefore = m_downsample_mip_states[mip];
    barrier.Transition.StateAfter = to;
    cmdlist->ResourceBarrier(1, &barrier);
    m_downsample_mip_states[mip] = to;
  };

  // Copy the source sub-rect into mip 0 (at the same offset, so the
  // generation viewports at left>>level / top>>level stay aligned).
  source.TransitionToState(D3D12_RESOURCE_STATE_COPY_SOURCE);
  transition_mip(0, D3D12_RESOURCE_STATE_COPY_DEST);
  {
    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = m_downsample_mip_resource.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = source.GetResource();
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;
    const D3D12_BOX src_box = {left, top, 0, left + width, top + height, 1};
    cmdlist->CopyTextureRegion(&dst, left, top, 0, &src, &src_box);
  }
  transition_mip(0, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

  // Build the energy mip pyramid: each level reads the previous mip and
  // writes its own, first pass at level 1, mid pass thereafter.
  for (uint32_t level = 1; level < levels; level++)
  {
    transition_mip(level, D3D12_RESOURCE_STATE_RENDER_TARGET);

    cmdlist->ClearRenderTargetView(m_downsample_mip_rtvs[level].cpu_handle, clear_color, 0, nullptr);
    cmdlist->OMSetRenderTargets(1, &m_downsample_mip_rtvs[level].cpu_handle, FALSE, nullptr);
    cmdlist->SetGraphicsRootSignature(m_single_sampler_root_signature.Get());

    const SmoothingUBOData ubo = GetSmoothingUBO(level, left, top, width, height, tex_width, tex_height);
    cmdlist->SetGraphicsRoot32BitConstants(0, sizeof(ubo) / sizeof(uint32_t), &ubo, 0);
    cmdlist->SetGraphicsRootDescriptorTable(1, m_downsample_mip_srvs[level - 1].gpu_handle);
    cmdlist->SetGraphicsRootDescriptorTable(2, m_point_sampler.gpu_handle);
    cmdlist->SetPipelineState((level == 1) ? m_downsample_first_pass_pipeline.Get()
                                           : m_downsample_mid_pass_pipeline.Get());
    D3D12::SetViewportAndScissor(cmdlist, left >> level, top >> level, width >> level, height >> level);
    cmdlist->DrawInstanced(3, 1, 0, 0);

    transition_mip(level, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  }

  // Blur the lowest mip into the single-channel weight texture.
  {
    const uint32_t last_level = levels - 1;
    m_downsample_weight_texture.TransitionToState(D3D12_RESOURCE_STATE_RENDER_TARGET);

    cmdlist->ClearRenderTargetView(m_downsample_weight_texture.GetRTVOrDSVDescriptor().cpu_handle, clear_color, 0,
                                   nullptr);
    cmdlist->OMSetRenderTargets(1, &m_downsample_weight_texture.GetRTVOrDSVDescriptor().cpu_handle, FALSE, nullptr);
    cmdlist->SetGraphicsRootSignature(m_single_sampler_root_signature.Get());

    const SmoothingUBOData ubo = GetSmoothingUBO(last_level, left, top, width, height, tex_width, tex_height);
    cmdlist->SetGraphicsRoot32BitConstants(0, sizeof(ubo) / sizeof(uint32_t), &ubo, 0);
    cmdlist->SetGraphicsRootDescriptorTable(1, m_downsample_mip_srvs[last_level].gpu_handle);
    cmdlist->SetGraphicsRootDescriptorTable(2, m_point_sampler.gpu_handle);
    cmdlist->SetPipelineState(m_downsample_blur_pass_pipeline.Get());
    D3D12::SetViewportAndScissor(cmdlist, left >> last_level, top >> last_level, width >> last_level,
                                 height >> last_level);
    cmdlist->DrawInstanced(3, 1, 0, 0);

    m_downsample_weight_texture.TransitionToState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  }

  // Composite the colour pyramid (trilinear) blended by the weight
  // (linear) into the display texture at the view offset.
  {
    m_display_texture.TransitionToState(D3D12_RESOURCE_STATE_RENDER_TARGET);

    cmdlist->OMSetRenderTargets(1, &m_display_texture.GetRTVOrDSVDescriptor().cpu_handle, FALSE, nullptr);
    cmdlist->SetGraphicsRootSignature(m_downsample_composite_root_signature.Get());

    // The composite FS does not read b0, but the root signature declares
    // it to match the shared downsample binding layout; push zeroes.
    const float composite_ubo[6] = {};
    cmdlist->SetGraphicsRoot32BitConstants(0, 6, composite_ubo, 0);
    cmdlist->SetGraphicsRootDescriptorTable(1, m_downsample_full_srv.gpu_handle);
    cmdlist->SetGraphicsRootDescriptorTable(2, m_downsample_weight_texture.GetSRVDescriptor().gpu_handle);
    cmdlist->SetGraphicsRootDescriptorTable(3, m_trilinear_sampler.gpu_handle);
    cmdlist->SetGraphicsRootDescriptorTable(4, m_linear_sampler.gpu_handle);
    cmdlist->SetPipelineState(m_downsample_composite_pipeline.Get());
    D3D12::SetViewportAndScissor(cmdlist, left, top, width, height);
    cmdlist->DrawInstanced(3, 1, 0, 0);

    m_display_texture.TransitionToState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  }

  RestoreGraphicsAPIState();

  m_host_display->SetDisplayTexture(&m_display_texture, HostDisplayPixelFormat::RGBA8, m_display_texture.GetWidth(),
                                    m_display_texture.GetHeight(), left, top, width, height);
}

void GPU_HW_D3D12::ReadVRAM(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{  if (IsUsingSoftwareRendererForReadbacks())
  {
    ReadSoftwareRendererVRAM(x, y, width, height);
    return;
  }

  // Get bounds with wrap-around handled.
  const Common::Rectangle<uint32_t> copy_rect = GetVRAMTransferBounds(x, y, width, height);
  const uint32_t encoded_width = (copy_rect.GetWidth() + 1) / 2;
  const uint32_t encoded_height = copy_rect.GetHeight();

  ID3D12GraphicsCommandList* cmdlist = g_d3d12_context->GetCommandList();
  m_vram_texture.TransitionToState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  m_vram_readback_texture.TransitionToState(D3D12_RESOURCE_STATE_RENDER_TARGET);

  // Encode the 24-bit texture as 16-bit.
  // 6 DWORDs to match the post-RESOLUTION_SCALE-refactor vram_read_ps
  // cbuffer (u_base_coords.xy, u_size.xy, u_resolution_scale, u_pad0).
  const uint32_t uniforms[6] = {copy_rect.left, copy_rect.top, copy_rect.GetWidth(), copy_rect.GetHeight(),
                                m_resolution_scale, 0u /* u_pad0 */};
  cmdlist->OMSetRenderTargets(1, &m_vram_readback_texture.GetRTVOrDSVDescriptor().cpu_handle, FALSE, nullptr);
  cmdlist->SetGraphicsRootSignature(m_single_sampler_root_signature.Get());
  cmdlist->SetGraphicsRoot32BitConstants(0, sizeof(uniforms) / sizeof(uint32_t), uniforms, 0);
  cmdlist->SetGraphicsRootDescriptorTable(1, m_vram_texture.GetSRVDescriptor());
  ComPtr<ID3D12PipelineState> readback_pso = GetVRAMReadbackPipeline();
  if (!readback_pso)
    return;
  cmdlist->SetPipelineState(readback_pso.Get());
  D3D12::SetViewportAndScissor(cmdlist, 0, 0, encoded_width, encoded_height);
  cmdlist->DrawInstanced(3, 1, 0, 0);

  m_vram_readback_texture.TransitionToState(D3D12_RESOURCE_STATE_COPY_SOURCE);
  m_vram_texture.TransitionToState(D3D12_RESOURCE_STATE_RENDER_TARGET);

  // Stage the readback.
  m_vram_readback_staging_texture.CopyFromTexture(m_vram_readback_texture, 0, 0, 0, 0, 0, encoded_width,
                                                  encoded_height);

  // And copy it into our shadow buffer (will execute command buffer and stall).
  m_vram_readback_staging_texture.ReadPixels(0, 0, encoded_width, encoded_height,
                                             &m_vram_shadow[copy_rect.top * VRAM_WIDTH + copy_rect.left],
                                             VRAM_WIDTH * sizeof(uint16_t));

  RestoreGraphicsAPIState();
}

void GPU_HW_D3D12::FillVRAM(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color)
{
  if (IsUsingSoftwareRendererForReadbacks())
    FillSoftwareRendererVRAM(x, y, width, height, color);

  // TODO: Use fast clear
  GPU_HW::FillVRAM(x, y, width, height, color);

  const VRAMFillUBOData uniforms = GetVRAMFillUBOData(x, y, width, height, color);
  ID3D12GraphicsCommandList* cmdlist = g_d3d12_context->GetCommandList();

  const bool wrapped = IsVRAMFillOversized(x, y, width, height);
  const bool interlaced = IsInterlacedRenderingEnabled();
  if (!wrapped && !interlaced)
  {
    const D3D12_RECT rc = {static_cast<LONG>(x * m_resolution_scale), static_cast<LONG>(y * m_resolution_scale),
                           static_cast<LONG>((x + width) * m_resolution_scale),
                           static_cast<LONG>((y + height) * m_resolution_scale)};
    cmdlist->ClearRenderTargetView(m_vram_texture.GetRTVOrDSVDescriptor(), uniforms.u_fill_color, 1, &rc);
    cmdlist->ClearDepthStencilView(m_vram_depth_texture.GetRTVOrDSVDescriptor(), D3D12_CLEAR_FLAG_DEPTH,
                                   uniforms.u_fill_color[3], 0, 1, &rc);
    return;
  }

  cmdlist->SetGraphicsRootSignature(m_single_sampler_root_signature.Get());
  cmdlist->SetGraphicsRoot32BitConstants(0, sizeof(uniforms) / sizeof(uint32_t), &uniforms, 0);
  cmdlist->SetGraphicsRootDescriptorTable(1, g_d3d12_context->GetNullSRVDescriptor());
  ComPtr<ID3D12PipelineState> fill_pso =
    GetVRAMFillPipeline(static_cast<uint8_t>(IsVRAMFillOversized(x, y, width, height)),
                        static_cast<uint8_t>(IsInterlacedRenderingEnabled()));
  if (!fill_pso)
    return;
  cmdlist->SetPipelineState(fill_pso.Get());

  const Common::Rectangle<uint32_t> bounds(GetVRAMTransferBounds(x, y, width, height));
  D3D12::SetViewportAndScissor(cmdlist, bounds.left * m_resolution_scale, bounds.top * m_resolution_scale,
                               bounds.GetWidth() * m_resolution_scale, bounds.GetHeight() * m_resolution_scale);

  cmdlist->DrawInstanced(3, 1, 0, 0);

  RestoreGraphicsAPIState();
}

void GPU_HW_D3D12::UpdateVRAM(uint32_t x, uint32_t y, uint32_t width, uint32_t height, const void* data, bool set_mask, bool check_mask)
{
  if (IsUsingSoftwareRendererForReadbacks())
    UpdateSoftwareRendererVRAM(x, y, width, height, data, set_mask, check_mask);

  const Common::Rectangle<uint32_t> bounds = GetVRAMTransferBounds(x, y, width, height);
  GPU_HW::UpdateVRAM(bounds.left, bounds.top, bounds.GetWidth(), bounds.GetHeight(), data, set_mask, check_mask);

  if (!check_mask)
  {
    const TextureReplacementTexture* rtex = g_texture_replacements.GetVRAMWriteReplacement(width, height, data);
    if (rtex && BlitVRAMReplacementTexture(rtex, x * m_resolution_scale, y * m_resolution_scale,
                                           width * m_resolution_scale, height * m_resolution_scale))
    {
      return;
    }
  }

  const uint32_t data_size = width * height * sizeof(uint16_t);
  const uint32_t alignment = D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT; // ???
  if (!m_texture_stream_buffer.ReserveMemory(data_size, alignment))
  {
    g_d3d12_context->ExecuteCommandList(false);
    RestoreGraphicsAPIState();
    if (!m_texture_stream_buffer.ReserveMemory(data_size, alignment))
      return;
  }

  const uint32_t start_index = m_texture_stream_buffer.GetCurrentOffset() / sizeof(uint16_t);
  std::memcpy(m_texture_stream_buffer.GetCurrentHostPointer(), data, data_size);
  m_texture_stream_buffer.CommitMemory(data_size);

  const VRAMWriteUBOData uniforms = GetVRAMWriteUBOData(x, y, width, height, start_index, set_mask, check_mask);

  ID3D12GraphicsCommandList* cmdlist = g_d3d12_context->GetCommandList();
  cmdlist->SetGraphicsRootSignature(m_single_sampler_root_signature.Get());
  cmdlist->SetGraphicsRoot32BitConstants(0, sizeof(uniforms) / sizeof(uint32_t), &uniforms, 0);
  cmdlist->SetGraphicsRootDescriptorTable(1, m_texture_stream_buffer_srv);
  ComPtr<ID3D12PipelineState> write_pso = GetVRAMWritePipeline(static_cast<uint8_t>(check_mask));
  if (!write_pso)
    return;
  cmdlist->SetPipelineState(write_pso.Get());

  // the viewport should already be set to the full vram, so just adjust the scissor
  const Common::Rectangle<uint32_t> scaled_bounds = bounds * m_resolution_scale;
  D3D12::SetScissor(cmdlist, scaled_bounds.left, scaled_bounds.top, scaled_bounds.GetWidth(),
                    scaled_bounds.GetHeight());

  cmdlist->DrawInstanced(3, 1, 0, 0);

  RestoreGraphicsAPIState();
}

void GPU_HW_D3D12::CopyVRAM(uint32_t src_x, uint32_t src_y, uint32_t dst_x, uint32_t dst_y, uint32_t width, uint32_t height)
{
  if (IsUsingSoftwareRendererForReadbacks())
    CopySoftwareRendererVRAM(src_x, src_y, dst_x, dst_y, width, height);

  if (UseVRAMCopyShader(src_x, src_y, dst_x, dst_y, width, height) || IsUsingMultisampling())
  {
    const Common::Rectangle<uint32_t> src_bounds = GetVRAMTransferBounds(src_x, src_y, width, height);
    const Common::Rectangle<uint32_t> dst_bounds = GetVRAMTransferBounds(dst_x, dst_y, width, height);
    if (m_vram_dirty_rect.Intersects(src_bounds))
      UpdateVRAMReadTexture();
    IncludeVRAMDirtyRectangle(dst_bounds);

    const VRAMCopyUBOData uniforms(GetVRAMCopyUBOData(src_x, src_y, dst_x, dst_y, width, height));
    const Common::Rectangle<uint32_t> dst_bounds_scaled(dst_bounds * m_resolution_scale);

    ID3D12GraphicsCommandList* cmdlist = g_d3d12_context->GetCommandList();
    cmdlist->SetGraphicsRootSignature(m_single_sampler_root_signature.Get());
    cmdlist->SetGraphicsRoot32BitConstants(0, sizeof(uniforms) / sizeof(uint32_t), &uniforms, 0);
    cmdlist->SetGraphicsRootDescriptorTable(1, m_vram_read_texture.GetSRVDescriptor());
    ComPtr<ID3D12PipelineState> copy_pso = GetVRAMCopyPipeline(static_cast<uint8_t>(m_GPUSTAT.check_mask_before_draw));
    if (!copy_pso)
      return;
    cmdlist->SetPipelineState(copy_pso.Get());
    D3D12::SetViewportAndScissor(cmdlist, dst_bounds_scaled.left, dst_bounds_scaled.top, dst_bounds_scaled.GetWidth(),
                                 dst_bounds_scaled.GetHeight());
    cmdlist->DrawInstanced(3, 1, 0, 0);

    RestoreGraphicsAPIState();

    if (m_GPUSTAT.check_mask_before_draw)
      m_current_depth++;

    return;
  }

  if (m_vram_dirty_rect.Intersects(Common::Rectangle<uint32_t>::FromExtents(src_x, src_y, width, height)))
    UpdateVRAMReadTexture();

  GPU_HW::CopyVRAM(src_x, src_y, dst_x, dst_y, width, height);

  src_x *= m_resolution_scale;
  src_y *= m_resolution_scale;
  dst_x *= m_resolution_scale;
  dst_y *= m_resolution_scale;
  width *= m_resolution_scale;
  height *= m_resolution_scale;

  const D3D12_TEXTURE_COPY_LOCATION src = {m_vram_read_texture.GetResource(),
                                           D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
  const D3D12_TEXTURE_COPY_LOCATION dst = {m_vram_texture.GetResource(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
  const D3D12_BOX src_box = {src_x, src_y, 0u, src_x + width, src_y + height, 1u};

  m_vram_texture.TransitionToState(D3D12_RESOURCE_STATE_COPY_DEST);
  m_vram_read_texture.TransitionToState(D3D12_RESOURCE_STATE_COPY_SOURCE);

  g_d3d12_context->GetCommandList()->CopyTextureRegion(&dst, dst_x, dst_y, 0, &src, &src_box);

  m_vram_read_texture.TransitionToState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  m_vram_texture.TransitionToState(D3D12_RESOURCE_STATE_RENDER_TARGET);
}

void GPU_HW_D3D12::UpdateVRAMReadTexture()
{
  ID3D12GraphicsCommandList* cmdlist = g_d3d12_context->GetCommandList();

  const auto scaled_rect = m_vram_dirty_rect * m_resolution_scale;

  if (m_vram_texture.IsMultisampled())
  {
    m_vram_texture.TransitionToState(D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
    m_vram_read_texture.TransitionToState(D3D12_RESOURCE_STATE_RESOLVE_DEST);
    cmdlist->ResolveSubresource(m_vram_read_texture, 0, m_vram_texture, 0, m_vram_texture.GetFormat());
  }
  else
  {
    const D3D12_TEXTURE_COPY_LOCATION src = {m_vram_texture.GetResource(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    const D3D12_TEXTURE_COPY_LOCATION dst = {m_vram_read_texture.GetResource(),
                                             D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    const D3D12_BOX src_box = {scaled_rect.left, scaled_rect.top, 0u, scaled_rect.right, scaled_rect.bottom, 1u};
    m_vram_texture.TransitionToState(D3D12_RESOURCE_STATE_COPY_SOURCE);
    m_vram_read_texture.TransitionToState(D3D12_RESOURCE_STATE_COPY_DEST);
    cmdlist->CopyTextureRegion(&dst, scaled_rect.left, scaled_rect.top, 0, &src, &src_box);
  }

  m_vram_read_texture.TransitionToState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  m_vram_texture.TransitionToState(D3D12_RESOURCE_STATE_RENDER_TARGET);

  GPU_HW::UpdateVRAMReadTexture();
}

void GPU_HW_D3D12::UpdateDepthBufferFromMaskBit()
{
  ID3D12GraphicsCommandList* cmdlist = g_d3d12_context->GetCommandList();

  m_vram_texture.TransitionToState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

  cmdlist->OMSetRenderTargets(0, nullptr, FALSE, &m_vram_depth_texture.GetRTVOrDSVDescriptor().cpu_handle);
  cmdlist->SetGraphicsRootDescriptorTable(1, m_vram_texture.GetSRVDescriptor());
  ComPtr<ID3D12PipelineState> update_depth_pso = GetVRAMUpdateDepthPipeline();
  if (!update_depth_pso)
    return;
  cmdlist->SetPipelineState(update_depth_pso.Get());
  D3D12::SetViewportAndScissor(cmdlist, 0, 0, m_vram_texture.GetWidth(), m_vram_texture.GetHeight());
  cmdlist->DrawInstanced(3, 1, 0, 0);

  m_vram_texture.TransitionToState(D3D12_RESOURCE_STATE_RENDER_TARGET);

  RestoreGraphicsAPIState();
}

void GPU_HW_D3D12::ClearDepthBuffer()
{
  ID3D12GraphicsCommandList* cmdlist = g_d3d12_context->GetCommandList();
  cmdlist->ClearDepthStencilView(m_vram_depth_texture.GetRTVOrDSVDescriptor(), D3D12_CLEAR_FLAG_DEPTH,
                                 m_pgxp_depth_buffer ? 1.0f : 0.0f, 0, 0, nullptr);
}

std::unique_ptr<GPU> GPU::CreateHardwareD3D12Renderer()
{
  return std::make_unique<GPU_HW_D3D12>();
}
