#include "gpu_hw_d3d11.h"
#include "common/d3d11/shader_compiler.h"
#include "common/d3d_common/embedded_shaders.h"
#include "common/display.hlsl.h"
#include "common/log.h"
#include "common/state_wrapper.h"
#include "common/thread_priority.h"
#include "common/timer.h"
#include "gpu_sw_backend.h"
#include "host_display.h"
#include "host_interface.h"
#include "core/host_interface.h"
#include "system.h"
Log_SetChannel(GPU_HW_D3D11);

#define HAVE_D3D11
#include <cstring>
#include <libretro_d3d.h>
class LibretroD3D11HostDisplayTexture : public HostDisplayTexture
{
public:
  LibretroD3D11HostDisplayTexture(D3D11::Texture texture)
    : m_texture(std::move(texture))
  {
  }
  ~LibretroD3D11HostDisplayTexture() override = default;

  void* GetHandle() const override { return m_texture.GetD3DSRV(); }
  uint32_t GetWidth() const override { return m_texture.GetWidth(); }
  uint32_t GetHeight() const override { return m_texture.GetHeight(); }
  uint32_t GetSamples() const override { return m_texture.GetSamples(); }

  ALWAYS_INLINE ID3D11Texture2D* GetD3DTexture() const { return m_texture.GetD3DTexture(); }
  ALWAYS_INLINE ID3D11ShaderResourceView* GetD3DSRV() const { return m_texture.GetD3DSRV(); }
  ALWAYS_INLINE ID3D11ShaderResourceView* const* GetD3DSRVArray() const { return m_texture.GetD3DSRVArray(); }

private:
  D3D11::Texture m_texture;
};

LibretroD3D11HostDisplay::LibretroD3D11HostDisplay() = default;

LibretroD3D11HostDisplay::~LibretroD3D11HostDisplay() { }

HostDisplay::RenderAPI LibretroD3D11HostDisplay::GetRenderAPI() const
{
  return HostDisplay::RenderAPI::D3D11;
}

void* LibretroD3D11HostDisplay::GetRenderDevice() const
{
  return m_device.Get();
}

void* LibretroD3D11HostDisplay::GetRenderContext() const
{
  return m_context.Get();
}

static constexpr std::array<DXGI_FORMAT, static_cast<uint32_t>(HostDisplayPixelFormat::Count)>
  s_display_pixel_format_mapping = {{DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM,
                                     DXGI_FORMAT_B5G6R5_UNORM, DXGI_FORMAT_B5G5R5A1_UNORM}};

std::unique_ptr<HostDisplayTexture> LibretroD3D11HostDisplay::CreateTexture(uint32_t width, uint32_t height, uint32_t layers,
                                                                            uint32_t levels, uint32_t samples,
                                                                            HostDisplayPixelFormat format,
                                                                            const void* data, uint32_t data_stride,
                                                                            bool dynamic /* = false */)
{
  if (layers != 1)
    return {};

  D3D11::Texture tex;
  if (!tex.Create(m_device.Get(), width, height, levels, samples,
                  s_display_pixel_format_mapping[static_cast<uint32_t>(format)], D3D11_BIND_SHADER_RESOURCE, data,
                  data_stride, dynamic))
  {
    return {};
  }

  return std::make_unique<LibretroD3D11HostDisplayTexture>(std::move(tex));
}

bool LibretroD3D11HostDisplay::SupportsDisplayPixelFormat(HostDisplayPixelFormat format) const
{
  const DXGI_FORMAT dfmt = s_display_pixel_format_mapping[static_cast<uint32_t>(format)];
  if (dfmt == DXGI_FORMAT_UNKNOWN)
    return false;

  UINT support = 0;
  const UINT required = D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_SHADER_SAMPLE;
  return (SUCCEEDED(m_device->CheckFormatSupport(dfmt, &support)) && ((support & required) == required));
}

bool LibretroD3D11HostDisplay::BeginSetDisplayPixels(HostDisplayPixelFormat format, uint32_t width, uint32_t height,
                                                     void** out_buffer, uint32_t* out_pitch)
{
  ClearDisplayTexture();

  const DXGI_FORMAT dxgi_format = s_display_pixel_format_mapping[static_cast<uint32_t>(format)];
  if (m_display_pixels_texture.GetWidth() < width || m_display_pixels_texture.GetHeight() < height ||
      m_display_pixels_texture.GetFormat() != dxgi_format)
  {
    if (!m_display_pixels_texture.Create(m_device.Get(), width, height, 1, 1, dxgi_format, D3D11_BIND_SHADER_RESOURCE,
                                         nullptr, 0, true))
    {
      return false;
    }
  }

  D3D11_MAPPED_SUBRESOURCE sr;
  HRESULT hr = m_context->Map(m_display_pixels_texture.GetD3DTexture(), 0, D3D11_MAP_WRITE_DISCARD, 0, &sr);
  if (FAILED(hr))
  {
    Log_ErrorPrintf("Map pixels texture failed: %08X", hr);
    return false;
  }

  *out_buffer = sr.pData;
  *out_pitch = sr.RowPitch;

  SetDisplayTexture(m_display_pixels_texture.GetD3DSRV(), format, m_display_pixels_texture.GetWidth(),
                    m_display_pixels_texture.GetHeight(), 0, 0, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
  return true;
}

void LibretroD3D11HostDisplay::EndSetDisplayPixels()
{
  m_context->Unmap(m_display_pixels_texture.GetD3DTexture(), 0);
}

bool LibretroD3D11HostDisplay::RequestHardwareRendererContext(retro_hw_render_callback* cb)
{
  cb->cache_context = false;
  cb->bottom_left_origin = false;
  cb->context_type = RETRO_HW_CONTEXT_D3D11;
  cb->version_major = 11;
  cb->version_minor = 0;

  return g_retro_environment_callback(RETRO_ENVIRONMENT_SET_HW_RENDER, cb);
}

bool LibretroD3D11HostDisplay::CreateRenderDevice(const WindowInfo& wi, std::string_view adapter_name,
                                                  bool debug_device, bool threaded_presentation)
{
  retro_hw_render_interface* ri = nullptr;
  if (!g_retro_environment_callback(RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE, &ri))
  {
    Log_ErrorPrint("Failed to get HW render interface");
    return false;
  }
  else if (ri->interface_type != RETRO_HW_RENDER_INTERFACE_D3D11 ||
           ri->interface_version != RETRO_HW_RENDER_INTERFACE_D3D11_VERSION)
  {
    Log_ErrorPrintf("Unexpected HW interface - type %u version %u", static_cast<unsigned>(ri->interface_type),
                    static_cast<unsigned>(ri->interface_version));
    return false;
  }

  const retro_hw_render_interface_d3d11* d3d11_ri = reinterpret_cast<const retro_hw_render_interface_d3d11*>(ri);
  if (!d3d11_ri->device || !d3d11_ri->context)
  {
    Log_ErrorPrintf("Missing D3D device or context");
    return false;
  }

  m_device = d3d11_ri->device;
  m_context = d3d11_ri->context;
  return true;
}

bool LibretroD3D11HostDisplay::InitializeRenderDevice(std::string_view shader_cache_directory, bool debug_device,
		bool threaded_presentation)
{
  return CreateResources();
}

void LibretroD3D11HostDisplay::DestroyRenderDevice()
{
  ClearSoftwareCursor();
  DestroyResources();
  m_context.Reset();
  m_device.Reset();
}

void LibretroD3D11HostDisplay::ResizeRenderWindow(int32_t new_window_width, int32_t new_window_height)
{
  m_window_info.surface_width = static_cast<uint32_t>(new_window_width);
  m_window_info.surface_height = static_cast<uint32_t>(new_window_height);
}

bool LibretroD3D11HostDisplay::ChangeRenderWindow(const WindowInfo& new_wi)
{
  // Check that the device hasn't changed.
  retro_hw_render_interface* ri = nullptr;
  if (!g_retro_environment_callback(RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE, &ri))
  {
    Log_ErrorPrint("Failed to get HW render interface");
    return false;
  }
  else if (ri->interface_type != RETRO_HW_RENDER_INTERFACE_D3D11 ||
           ri->interface_version != RETRO_HW_RENDER_INTERFACE_D3D11_VERSION)
  {
    Log_ErrorPrintf("Unexpected HW interface - type %u version %u", static_cast<unsigned>(ri->interface_type),
                    static_cast<unsigned>(ri->interface_version));
    return false;
  }

  const retro_hw_render_interface_d3d11* d3d11_ri = reinterpret_cast<const retro_hw_render_interface_d3d11*>(ri);
  if (d3d11_ri->device != m_device.Get() || d3d11_ri->context != m_context.Get())
  {
    Log_ErrorPrintf("D3D device/context changed outside our control");
    return false;
  }

  m_window_info = new_wi;
  return true;
}

bool LibretroD3D11HostDisplay::CreateResources()
{
  HRESULT hr;

  m_display_vertex_shader =
    D3D11::ShaderCompiler::CreateVertexShader(m_device.Get(), s_display_vs_bytecode, sizeof(s_display_vs_bytecode));
  m_display_pixel_shader =
    D3D11::ShaderCompiler::CreatePixelShader(m_device.Get(), s_display_ps_bytecode, sizeof(s_display_ps_bytecode));
  m_display_alpha_pixel_shader = D3D11::ShaderCompiler::CreatePixelShader(m_device.Get(), s_display_ps_alpha_bytecode,
                                                                          sizeof(s_display_ps_alpha_bytecode));
  if (!m_display_vertex_shader || !m_display_pixel_shader || !m_display_alpha_pixel_shader)
    return false;

  if (!m_display_uniform_buffer.Create(m_device.Get(), D3D11_BIND_CONSTANT_BUFFER, DISPLAY_UNIFORM_BUFFER_SIZE))
    return false;

  CD3D11_RASTERIZER_DESC rasterizer_desc = CD3D11_RASTERIZER_DESC(CD3D11_DEFAULT());
  rasterizer_desc.CullMode = D3D11_CULL_NONE;
  hr = m_device->CreateRasterizerState(&rasterizer_desc, m_display_rasterizer_state.GetAddressOf());
  if (FAILED(hr))
    return false;

  CD3D11_DEPTH_STENCIL_DESC depth_stencil_desc = CD3D11_DEPTH_STENCIL_DESC(CD3D11_DEFAULT());
  depth_stencil_desc.DepthEnable = FALSE;
  depth_stencil_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
  hr = m_device->CreateDepthStencilState(&depth_stencil_desc, m_display_depth_stencil_state.GetAddressOf());
  if (FAILED(hr))
    return false;

  CD3D11_BLEND_DESC blend_desc = CD3D11_BLEND_DESC(CD3D11_DEFAULT());
  hr = m_device->CreateBlendState(&blend_desc, m_display_blend_state.GetAddressOf());
  if (FAILED(hr))
    return false;

  blend_desc.RenderTarget[0] = {TRUE,
                                D3D11_BLEND_SRC_ALPHA,
                                D3D11_BLEND_INV_SRC_ALPHA,
                                D3D11_BLEND_OP_ADD,
                                D3D11_BLEND_ONE,
                                D3D11_BLEND_ZERO,
                                D3D11_BLEND_OP_ADD,
                                D3D11_COLOR_WRITE_ENABLE_ALL};
  hr = m_device->CreateBlendState(&blend_desc, m_software_cursor_blend_state.GetAddressOf());
  if (FAILED(hr))
    return false;

  CD3D11_SAMPLER_DESC sampler_desc = CD3D11_SAMPLER_DESC(CD3D11_DEFAULT());
  sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
  hr = m_device->CreateSamplerState(&sampler_desc, m_point_sampler.GetAddressOf());
  if (FAILED(hr))
    return false;

  sampler_desc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
  hr = m_device->CreateSamplerState(&sampler_desc, m_linear_sampler.GetAddressOf());
  if (FAILED(hr))
    return false;

  return true;
}

void LibretroD3D11HostDisplay::DestroyResources()
{
  m_framebuffer.Destroy();
  m_display_uniform_buffer.Release();
  m_linear_sampler.Reset();
  m_point_sampler.Reset();
  m_display_alpha_pixel_shader.Reset();
  m_display_pixel_shader.Reset();
  m_display_vertex_shader.Reset();
  m_display_blend_state.Reset();
  m_display_depth_stencil_state.Reset();
  m_display_rasterizer_state.Reset();
}

void LibretroD3D11HostDisplay::RenderSoftwareCursor(int32_t left, int32_t top, int32_t width, int32_t height,
                                            HostDisplayTexture* texture_handle)
{
  m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  m_context->VSSetShader(m_display_vertex_shader.Get(), nullptr, 0);
  m_context->PSSetShader(m_display_alpha_pixel_shader.Get(), nullptr, 0);
  m_context->PSSetShaderResources(0, 1, static_cast<LibretroD3D11HostDisplayTexture*>(texture_handle)->GetD3DSRVArray());
  m_context->PSSetSamplers(0, 1, m_linear_sampler.GetAddressOf());

  const float uniforms[4] = {0.0f, 0.0f, 1.0f, 1.0f};
  const auto map = m_display_uniform_buffer.Map(m_context.Get(), m_display_uniform_buffer.GetSize(), sizeof(uniforms));
  std::memcpy(map.pointer, uniforms, sizeof(uniforms));
  m_display_uniform_buffer.Unmap(m_context.Get(), sizeof(uniforms));
  m_context->VSSetConstantBuffers(0, 1, m_display_uniform_buffer.GetD3DBufferArray());

  D3D11_VIEWPORT vp;
  vp.TopLeftX = static_cast<float>(left);
  vp.TopLeftY = static_cast<float>(top);
  vp.Width = static_cast<float>(width);
  vp.Height = static_cast<float>(height);
  vp.MinDepth = 0;
  vp.MaxDepth = 1;
  m_context->RSSetViewports(1, &vp);
  m_context->RSSetState(m_display_rasterizer_state.Get());
  m_context->OMSetDepthStencilState(m_display_depth_stencil_state.Get(), 0);
  m_context->OMSetBlendState(m_software_cursor_blend_state.Get(), nullptr, 0xFFFFFFFFu);

  m_context->Draw(3, 0);
}

bool LibretroD3D11HostDisplay::Render()
{
  // No display texture this frame -> send the libretro frame-dupe
  // signal (NULL frame), matching the SW path in
  // LibretroHostDisplay::Render(). See the equivalent comment in
  // gpu_hw_opengl.cpp::Render().
  if (!HasDisplayTexture())
  {
    g_retro_video_refresh_callback(nullptr, 0, 0, 0);
    return true;
  }

  const uint32_t resolution_scale = g_host_interface_storage.GetResolutionScale();
  const uint32_t display_width = static_cast<uint32_t>(m_display_width) * resolution_scale;
  const uint32_t display_height = static_cast<uint32_t>(m_display_height) * resolution_scale;
  // Lightgun state was cached at controller-update time; do NOT call
  // g_retro_input_state_callback() from the renderer - see the matching
  // comment in gpu_hw_opengl.cpp::Render().
  const int16_t  gun_x     = GetLightgunRawX();
  const int16_t  gun_y     = GetLightgunRawY();
  const bool offscreen = IsLightgunOffscreen();
  const int32_t pos_x = offscreen ? 0 : (((static_cast<int32_t>(gun_x) + 0x7FFF) * display_width)  / 0xFFFF);
  const int32_t pos_y = offscreen ? 0 : (((static_cast<int32_t>(gun_y) + 0x7FFF) * display_height) / 0xFFFF);
  if (!CheckFramebufferSize(display_width, display_height))
    return false;

  // Ensure we're not currently bound.
  ID3D11ShaderResourceView* null_srv = nullptr;
  m_context->PSSetShaderResources(0, 1, &null_srv);
  m_context->OMSetRenderTargets(1u, m_framebuffer.GetD3DRTVArray(), nullptr);

  {
    const auto [left, top, width, height] = CalculateDrawRect(display_width, display_height, 0, false);
    RenderDisplay(left, top, width, height, m_display_texture_handle, m_display_texture_width, m_display_texture_height,
                  m_display_texture_view_x, m_display_texture_view_y, m_display_texture_view_width,
                  m_display_texture_view_height);
  }

  if (g_settings.controller_show_crosshair && HasSoftwareCursor() && (pos_x > 0 || pos_y > 0))
  {
    const float width_scale = (display_width / 2400.0f);
    const float height_scale = (display_height / 1920.0f);
    const uint32_t cursor_extents_x = static_cast<uint32_t>(static_cast<float>(m_cursor_texture->GetWidth()) * width_scale);
    const uint32_t cursor_extents_y = static_cast<uint32_t>(static_cast<float>(m_cursor_texture->GetHeight()) * height_scale);

    const int32_t out_left = pos_x - cursor_extents_x;
    const int32_t out_top = pos_y - cursor_extents_y;
    const int32_t out_width = cursor_extents_x * 2u;
    const int32_t out_height = cursor_extents_y * 2u;

    RenderSoftwareCursor(out_left, out_top, out_width, out_height, m_cursor_texture.get());
  }

  // NOTE: libretro frontend expects the data bound to PS SRV slot 0.
  m_context->OMSetRenderTargets(0, nullptr, nullptr);
  m_context->PSSetShaderResources(0, 1, m_framebuffer.GetD3DSRVArray());
  g_retro_video_refresh_callback(RETRO_HW_FRAME_BUFFER_VALID, display_width, display_height, 0);
  return true;
}

void LibretroD3D11HostDisplay::RenderDisplay(int32_t left, int32_t top, int32_t width, int32_t height, void* texture_handle,
                                             uint32_t texture_width, int32_t texture_height, int32_t texture_view_x,
                                             int32_t texture_view_y, int32_t texture_view_width, int32_t texture_view_height)
{
  m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  m_context->VSSetShader(m_display_vertex_shader.Get(), nullptr, 0);
  m_context->PSSetShader(m_display_pixel_shader.Get(), nullptr, 0);
  m_context->PSSetShaderResources(0, 1, reinterpret_cast<ID3D11ShaderResourceView**>(&texture_handle));
  m_context->PSSetSamplers(0, 1, m_point_sampler.GetAddressOf());

  const float uniforms[4] = {
    static_cast<float>(texture_view_x) / static_cast<float>(texture_width),
    static_cast<float>(texture_view_y) / static_cast<float>(texture_height),
    static_cast<float>(texture_view_width) / static_cast<float>(texture_width),
    static_cast<float>(texture_view_height) / static_cast<float>(texture_height)};
  const auto map = m_display_uniform_buffer.Map(m_context.Get(), m_display_uniform_buffer.GetSize(), sizeof(uniforms));
  std::memcpy(map.pointer, uniforms, sizeof(uniforms));
  m_display_uniform_buffer.Unmap(m_context.Get(), sizeof(uniforms));
  m_context->VSSetConstantBuffers(0, 1, m_display_uniform_buffer.GetD3DBufferArray());

  D3D11_VIEWPORT vp;
  vp.TopLeftX = static_cast<float>(left);
  vp.TopLeftY = static_cast<float>(top);
  vp.Width = static_cast<float>(width);
  vp.Height = static_cast<float>(height);
  vp.MinDepth = 0;
  vp.MaxDepth = 1;
  m_context->RSSetViewports(1, &vp);
  m_context->RSSetState(m_display_rasterizer_state.Get());
  m_context->OMSetDepthStencilState(m_display_depth_stencil_state.Get(), 0);
  m_context->OMSetBlendState(m_display_blend_state.Get(), nullptr, 0xFFFFFFFFu);

  m_context->Draw(3, 0);
}

bool LibretroD3D11HostDisplay::CheckFramebufferSize(uint32_t width, uint32_t height)
{
  if (m_framebuffer.GetWidth() == width && m_framebuffer.GetHeight() == height)
    return true;

  return m_framebuffer.Create(m_device.Get(), width, height, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
                              D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET);
}

GPU_HW_D3D11::GPU_HW_D3D11() = default;

GPU_HW_D3D11::~GPU_HW_D3D11()
{
  if (m_host_display)
    m_host_display->ClearDisplayTexture();

  if (m_context)
    m_context->ClearState();

  DestroyShaders();
  DestroyStateObjects();
}

bool GPU_HW_D3D11::Initialize(HostDisplay* host_display)
{
  if (host_display->GetRenderAPI() != HostDisplay::RenderAPI::D3D11)
  {
    Log_ErrorPrintf("Host render API is incompatible");
    return false;
  }

  m_device = static_cast<ID3D11Device*>(host_display->GetRenderDevice());
  m_context = static_cast<ID3D11DeviceContext*>(host_display->GetRenderContext());
  if (!m_device || !m_context)
    return false;

  SetCapabilities();

  if (!GPU_HW::Initialize(host_display))
    return false;

  if (!CreateFramebuffer())
  {
    Log_ErrorPrintf("Failed to create framebuffer");
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

  if (!CreateStateObjects())
  {
    Log_ErrorPrintf("Failed to create state objects");
    return false;
  }

  if (!CompileShaders())
  {
    Log_ErrorPrintf("Failed to compile shaders");
    return false;
  }

  RestoreGraphicsAPIState();
  return true;
}

void GPU_HW_D3D11::Reset(bool clear_vram)
{
  GPU_HW::Reset(clear_vram);

  if (clear_vram)
    ClearFramebuffer();
}

bool GPU_HW_D3D11::DoState(StateWrapper& sw, HostDisplayTexture** host_texture, bool update_display)
{
  if (host_texture)
  {
    ComPtr<ID3D11Resource> resource;

    HostDisplayTexture* tex = *host_texture;
    if (sw.IsReading())
    {
      if (tex->GetWidth() != m_vram_texture.GetWidth() || tex->GetHeight() != m_vram_texture.GetHeight() ||
          tex->GetSamples() != m_vram_texture.GetSamples())
      {
        return false;
      }

      static_cast<ID3D11ShaderResourceView*>(tex->GetHandle())->GetResource(resource.GetAddressOf());
      m_context->CopySubresourceRegion(m_vram_texture.GetD3DTexture(), 0, 0, 0, 0, resource.Get(), 0, nullptr);
    }
    else
    {
      if (!tex || tex->GetWidth() != m_vram_texture.GetWidth() || tex->GetHeight() != m_vram_texture.GetHeight() ||
          tex->GetSamples() != m_vram_texture.GetSamples())
      {
        delete tex;

        tex = m_host_display
                ->CreateTexture(m_vram_texture.GetWidth(), m_vram_texture.GetHeight(), 1, 1,
                                m_vram_texture.GetSamples(), HostDisplayPixelFormat::RGBA8, nullptr, 0, false)
                .release();
        *host_texture = tex;
        if (!tex)
          return false;
      }

      static_cast<ID3D11ShaderResourceView*>(tex->GetHandle())->GetResource(resource.GetAddressOf());
      m_context->CopySubresourceRegion(resource.Get(), 0, 0, 0, 0, m_vram_texture.GetD3DTexture(), 0, nullptr);
    }
  }

  return GPU_HW::DoState(sw, host_texture, update_display);
}

void GPU_HW_D3D11::ResetGraphicsAPIState()
{
  GPU_HW::ResetGraphicsAPIState();

  m_context->GSSetShader(nullptr, nullptr, 0);

  // In D3D11 we can't leave a buffer mapped across a Present() call.
  FlushRender();
}

void GPU_HW_D3D11::RestoreGraphicsAPIState()
{
  const UINT stride = sizeof(BatchVertex);
  const UINT offset = 0;
  m_context->IASetVertexBuffers(0, 1, m_vertex_stream_buffer.GetD3DBufferArray(), &stride, &offset);
  m_context->IASetInputLayout(m_batch_input_layout.Get());
  m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  m_context->GSSetShader(nullptr, nullptr, 0);
  m_context->PSSetShaderResources(0, 1, m_vram_read_texture.GetD3DSRVArray());
  m_context->PSSetSamplers(0, 1, m_point_sampler_state.GetAddressOf());
  m_context->OMSetRenderTargets(1, m_vram_texture.GetD3DRTVArray(), m_vram_depth_view.Get());
  m_context->RSSetState(m_cull_none_rasterizer_state.Get());
  SetViewport(0, 0, m_vram_texture.GetWidth(), m_vram_texture.GetHeight());
  SetScissorFromDrawingArea();
  m_batch_ubo_dirty = true;
}

void GPU_HW_D3D11::UpdateSettings()
{
  GPU_HW::UpdateSettings();

  // Stop the background batch-compile worker BEFORE UpdateHWSettings
  // writes m_texture_filtering. The worker captures m_texture_filtering
  // at launch and uses it for every GetBatchPixelShader call - if
  // the runloop thread flips it mid-iteration the worker would split
  // its filter-index read across old / new values, installing a
  // pixel shader for one filter into a sub-cube indexed by another.
  // Joining first eliminates the race; the next
  // CompileShaders below restarts a worker for the new filter as
  // appropriate. StopShaderCompileThread is idempotent, so the
  // matching call inside DestroyShaders just becomes a no-op on
  // this path. Mirrors the D3D12 ordering in 10c53b8.
  StopShaderCompileThread();

  // See GPU_HW_D3D12::UpdateSettings for the rationale on
  // shader_source_changed vs shaders_changed: the cbuffer-refactor
  // patch made true_color / scaled_dithering / resolution_scale
  // invariant under HLSL source, so toggling them no longer requires
  // DestroyShaders + CompileShaders. The DXBC blobs, state objects,
  // and Direct3D11 shader handles all stay valid; the new values
  // ride the per-batch UBO upload on the next FlushRender. State
  // objects also stay valid through those toggles because their
  // descriptions key on MSAA / dual-source / depth-test / blend
  // mode - none of which are in the cbuffer-only set.
  //
  // only_dim_changed: dim-cube setting (filter / true_color /
  // scaled_dithering) changed and nothing in non_dim_diff. With the
  // dim cache (filter outermost) this means "filter sub-cube can
  // be lazy-populated, other filters' sub-cubes stay valid". When
  // set alongside shader_source_changed it picks out the
  // filter-only-changed case.
  //
  // display_only_source_changed: chroma_smoothing flipped and
  // nothing else affecting shader source changed. The batch pixel
  // shader matrix, VRAM ops pixel shaders, and state objects all
  // stay valid; only the 6-slot display pixel shader cache needs
  // to go. Mirrors the D3D12 partial-clear from 57ac62e.
  bool framebuffer_changed, shaders_changed, only_dim_changed, downsample_changed, shader_source_changed,
    display_only_source_changed;
  UpdateHWSettings(&framebuffer_changed, &shaders_changed, &only_dim_changed, &downsample_changed,
                   &shader_source_changed, &display_only_source_changed);

  // A downsample-mode change that UpdateHWSettings did not already fold
  // into framebuffer_changed (Disabled <-> Box - it only sets
  // framebuffer_changed when Adaptive is involved) still needs the
  // downsample texture created or freed for the new mode. D3D11's
  // CreateFramebuffer is monolithic (it rebuilds the VRAM/display
  // textures too, unlike the Vulkan backend's separate
  // CreateDownsampleResources), so route it through the normal
  // ReadVRAM -> CreateFramebuffer -> UpdateVRAM framebuffer round-trip
  // rather than recreating just the downsample texture in isolation.
  // Downsample toggling is a rare user action, so the extra round-trip
  // is not a concern.
  if (downsample_changed && !framebuffer_changed)
    framebuffer_changed = true;

  if (framebuffer_changed)
  {
    RestoreGraphicsAPIState();
    ReadVRAM(0, 0, VRAM_WIDTH, VRAM_HEIGHT);
    ResetGraphicsAPIState();
    m_host_display->ClearDisplayTexture();
    CreateFramebuffer();
  }

  if (shader_source_changed)
  {
    if (display_only_source_changed)
    {
      // chroma_smoothing flipped and nothing else - rebuild the six
      // display pixel shaders against the new m_chroma_smoothing
      // value (which UpdateHWSettings has already written into the
      // member). The 144-cell batch pixel shader matrix, the VRAM
      // ops pixel shaders, and the blend / depth-stencil / input
      // layout state objects all stay valid. Cost is 6 D3DCompile
      // + 6 CreatePixelShader calls, a fraction of a second
      // instead of the full CompileShaders pass.
      (void)RebuildDisplayPixelShaders();

      // Relaunch the Lazy worker StopShaderCompileThread joined at
      // the top of UpdateSettings; this branch doesn't go through
      // CompileShaders so the launch site there isn't hit. Mirrors
      // the D3D12 chroma partial-clear path.
      if (g_settings.gpu_shader_precompile_mode == GPUShaderPrecompileMode::Lazy)
      {
        StartShaderCompileThread();
      }
    }
    else if (only_dim_changed)
    {
      // Filter changed but nothing in non_dim_diff (and the cbuffer-
      // only members in dim_diff don't move HLSL, so this is
      // effectively "only filter changed"). m_batch_pixel_shader_fastpath is
      // filter-dimensioned: the previous filter's sub-cube remains
      // populated and reachable, so DestroyShaders would just throw
      // away valid pixel shaders.
      //
      // Cycling back to a previously-visited filter is instant -
      // no D3DCompile, no CreatePixelShader, just an atomic load of
      // an already-filled slot. State objects (blend / depth /
      // input layout) are filter-independent and also stay valid;
      // CreateStateObjects is skipped on this branch.
      //
      // The non-batch pixel shaders (copy / VRAM ops / display /
      // downsample), the vertex shaders, and the input layout are
      // all filter-independent - the texture filter is an axis of the
      // batch FS only. They keep working with the handles they
      // already have, so a full CompileShaders pass here would just
      // re-wrap the same pre-baked DXBC blobs into identical ComPtrs
      // for no functional benefit. Instead, build a progress tracker
      // sized for the batch matrix only and call PrecompileBatchShaders
      // directly. PrecompileBatchShaders walks the new filter sub-
      // cube via GetBatchPixelShader (which is dim-cache aware -
      // already-populated cells from a previous visit short-circuit on
      // the lock-free atomic load, and unvisited cells wrap their
      // pre-baked DXBC via the EmbeddedShaders pickers) and relaunches
      // the Lazy worker for the new filter.
      const uint32_t batch_progress_units =
        (g_settings.gpu_shader_precompile_mode == GPUShaderPrecompileMode::Enabled)
          ? CountReachableBatchShaders(m_supports_dual_source_blend)
          : 0u;
      ShaderCompileProgressTracker progress("Compiling Shaders", batch_progress_units);
      (void)PrecompileBatchShaders(progress);
    }
    else
    {
      // Full flush: a non-dim shader-affecting change
      // (multisamples / per-sample shading / UV limits / PGXP depth
      // / colour perspective / precompile mode) invalidates EVERY
      // sub-cube because those settings bake into the HLSL
      // identically for every filter slot.
      DestroyShaders();
      DestroyStateObjects();
      CreateStateObjects();
      CompileShaders();
    }
  }

  // Downsample-mode transition. GPU_HW::UpdateHWSettings deliberately
  // keeps downsample mode out of shaders_changed entirely (it does not
  // touch the batch matrix) and out of framebuffer_changed unless
  // Adaptive is on one side, surfacing it via downsample_changed for the
  // backend to service. shader_source_changed was therefore false for a
  // downsample-only change, so CompileShaders did not run and the new
  // mode's downsample pixel shaders are still null - Adaptive would
  // composite a black mip pyramid (black screen) and Box would draw with
  // a null pixel shader. The texture side is handled by the
  // downsample_changed -> framebuffer_changed promotion above (which
  // routes Box/Disabled transitions through the same ReadVRAM ->
  // CreateFramebuffer -> UpdateVRAM round-trip that keys the downsample
  // texture on the new mode); here we just rebuild the shaders.
  if (downsample_changed)
    CompileDownsampleShaders();

  if (framebuffer_changed)
  {
    RestoreGraphicsAPIState();
    UpdateVRAM(0, 0, VRAM_WIDTH, VRAM_HEIGHT, m_vram_ptr, false, false);
    UpdateDepthBufferFromMaskBit();
    UpdateDisplay();
    ResetGraphicsAPIState();
  }
}

void GPU_HW_D3D11::MapBatchVertexPointer(uint32_t required_vertices)
{
  const D3D11::StreamBuffer::MappingResult res =
    m_vertex_stream_buffer.Map(m_context.Get(), sizeof(BatchVertex), required_vertices * sizeof(BatchVertex));

  m_batch_start_vertex_ptr = static_cast<BatchVertex*>(res.pointer);
  m_batch_current_vertex_ptr = m_batch_start_vertex_ptr;
  m_batch_end_vertex_ptr = m_batch_start_vertex_ptr + res.space_aligned;
  m_batch_base_vertex = res.index_aligned;
}

void GPU_HW_D3D11::UnmapBatchVertexPointer(uint32_t used_vertices)
{
  m_vertex_stream_buffer.Unmap(m_context.Get(), used_vertices * sizeof(BatchVertex));
  m_batch_start_vertex_ptr = nullptr;
  m_batch_end_vertex_ptr = nullptr;
  m_batch_current_vertex_ptr = nullptr;
}

void GPU_HW_D3D11::SetCapabilities()
{
  const uint32_t max_texture_size = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
  const uint32_t max_texture_scale = max_texture_size / VRAM_WIDTH;

  m_max_resolution_scale = max_texture_scale;
  m_supports_dual_source_blend = true;
  m_supports_per_sample_shading = (m_device->GetFeatureLevel() >= D3D_FEATURE_LEVEL_10_1);
  m_supports_adaptive_downsampling = true;
  m_supports_disable_color_perspective = true;

  m_max_multisamples = 1;
  for (uint32_t multisamples = 2; multisamples < D3D11_MAX_MULTISAMPLE_SAMPLE_COUNT; multisamples++)
  {
    UINT num_quality_levels;
    if (SUCCEEDED(
          m_device->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, multisamples, &num_quality_levels)) &&
        num_quality_levels > 0)
    {
      m_max_multisamples = multisamples;
    }
  }
}

bool GPU_HW_D3D11::CreateFramebuffer()
{
  DestroyFramebuffer();

  // scale vram size to internal resolution
  const uint32_t texture_width = VRAM_WIDTH * m_resolution_scale;
  const uint32_t texture_height = VRAM_HEIGHT * m_resolution_scale;
  const uint16_t samples = static_cast<uint16_t>(m_multisamples);
  const DXGI_FORMAT texture_format = DXGI_FORMAT_R8G8B8A8_UNORM;
  /* D32_FLOAT rather than D16_UNORM: in PGXP depth-buffer mode the value
   * written to this attachment is the reconstructed perspective W, carried at
   * full float32 through the vertex path (a_pos is R32G32B32A32_FLOAT) - a
   * 16-bit UNORM depth quantizes exactly the precision PGXP exists to recover.
   * D32_FLOAT is lossless for the legacy ordered-depth path too (the 16-bit
   * counter's 0..1 range is trivially representable and ordering is preserved),
   * so the format is made unconditional: a format conditional on the PGXP-depth
   * setting would go stale on a mid-session toggle, since that toggle recompiles
   * shaders but does not set framebuffer_changed / recreate this texture. */
  const DXGI_FORMAT depth_format = DXGI_FORMAT_D32_FLOAT;

  if (!m_vram_texture.Create(m_device.Get(), texture_width, texture_height, 1, samples, texture_format,
                             D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET) ||
      !m_vram_depth_texture.Create(m_device.Get(), texture_width, texture_height, 1, samples, depth_format,
                                   D3D11_BIND_DEPTH_STENCIL) ||
      !m_vram_read_texture.Create(m_device.Get(), texture_width, texture_height, 1, 1, texture_format,
                                  D3D11_BIND_SHADER_RESOURCE) ||
      !m_display_texture.Create(
        m_device.Get(),
        ((m_downsample_mode == GPUDownsampleMode::Adaptive) ? VRAM_WIDTH : GPU_MAX_DISPLAY_WIDTH) * m_resolution_scale,
        GPU_MAX_DISPLAY_HEIGHT * m_resolution_scale, 1, 1, texture_format,
        D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET) ||
      !m_vram_encoding_texture.Create(m_device.Get(), VRAM_WIDTH / 2, VRAM_HEIGHT, 1, 1, texture_format,
                                      D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET) ||
      !m_vram_readback_texture.Create(m_device.Get(), VRAM_WIDTH / 2, VRAM_HEIGHT, texture_format, false))
  {
    return false;
  }

  D3D11_DEPTH_STENCIL_VIEW_DESC depth_view_desc;

  depth_view_desc.Format = depth_format;
  depth_view_desc.ViewDimension = (samples > 1) ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D;
  depth_view_desc.Flags = 0;
  depth_view_desc.Texture2D.MipSlice = 0;
  HRESULT hr =
    m_device->CreateDepthStencilView(m_vram_depth_texture, &depth_view_desc, m_vram_depth_view.GetAddressOf());
  if (FAILED(hr))
    return false;

  if (m_downsample_mode == GPUDownsampleMode::Adaptive)
  {
    const uint32_t levels = GetAdaptiveDownsamplingMipLevels();

    if (!m_downsample_texture.Create(m_device.Get(), texture_width, texture_height, static_cast<uint16_t>(levels), 1,
                                     texture_format, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET) ||
        !m_downsample_weight_texture.Create(m_device.Get(), texture_width >> (levels - 1),
                                            texture_height >> (levels - 1), 1, 1, DXGI_FORMAT_R8_UNORM,
                                            D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET))
    {
      return false;
    }

    m_downsample_mip_views.resize(levels);
    for (uint32_t i = 0; i < levels; i++)
    {
      D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
      srv_desc.Format = texture_format;
      srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
      srv_desc.Texture2D.MostDetailedMip = i;
      srv_desc.Texture2D.MipLevels = 1;

      D3D11_RENDER_TARGET_VIEW_DESC rtv_desc;
      rtv_desc.Format = texture_format;
      rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
      rtv_desc.Texture2D.MipSlice = i;

      hr = m_device->CreateShaderResourceView(m_downsample_texture, &srv_desc,
                                              m_downsample_mip_views[i].first.GetAddressOf());
      if (FAILED(hr))
        return false;

      hr = m_device->CreateRenderTargetView(m_downsample_texture, &rtv_desc,
                                            m_downsample_mip_views[i].second.GetAddressOf());
      if (FAILED(hr))
        return false;
    }
  }
  else if (m_downsample_mode == GPUDownsampleMode::Box)
  {
    if (!m_downsample_texture.Create(m_device.Get(), VRAM_WIDTH, VRAM_HEIGHT, 1, 1, texture_format,
                                     D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET))
    {
      return false;
    }
  }

  m_context->OMSetRenderTargets(1, m_vram_texture.GetD3DRTVArray(), nullptr);
  SetFullVRAMDirtyRectangle();
  return true;
}

void GPU_HW_D3D11::ClearFramebuffer()
{
  static constexpr std::array<float, 4> color = {};
  m_context->ClearRenderTargetView(m_vram_texture.GetD3DRTV(), color.data());
  m_context->ClearDepthStencilView(m_vram_depth_view.Get(), D3D11_CLEAR_DEPTH, m_pgxp_depth_buffer ? 1.0f : 0.0f, 0);
  m_context->ClearRenderTargetView(m_display_texture, color.data());
  SetFullVRAMDirtyRectangle();
  m_last_depth_z = 1.0f;
}

void GPU_HW_D3D11::DestroyFramebuffer()
{
  m_downsample_mip_views.clear();
  m_downsample_weight_texture.Destroy();
  m_downsample_texture.Destroy();

  m_vram_read_texture.Destroy();
  m_vram_depth_view.Reset();
  m_vram_depth_texture.Destroy();
  m_vram_texture.Destroy();
  m_vram_encoding_texture.Destroy();
  m_display_texture.Destroy();
  m_vram_readback_texture.Destroy();
}

bool GPU_HW_D3D11::CreateVertexBuffer()
{
  return m_vertex_stream_buffer.Create(m_device.Get(), D3D11_BIND_VERTEX_BUFFER, VERTEX_BUFFER_SIZE);
}

bool GPU_HW_D3D11::CreateUniformBuffer()
{
  return m_uniform_stream_buffer.Create(m_device.Get(), D3D11_BIND_CONSTANT_BUFFER, MAX_UNIFORM_BUFFER_SIZE);
}

bool GPU_HW_D3D11::CreateTextureBuffer()
{
  if (!m_texture_stream_buffer.Create(m_device.Get(), D3D11_BIND_SHADER_RESOURCE, VRAM_UPDATE_TEXTURE_BUFFER_SIZE))
    return false;

  const CD3D11_SHADER_RESOURCE_VIEW_DESC srv_desc(D3D11_SRV_DIMENSION_BUFFER, DXGI_FORMAT_R16_UINT, 0,
                                                  VRAM_UPDATE_TEXTURE_BUFFER_SIZE / sizeof(uint16_t));
  const HRESULT hr = m_device->CreateShaderResourceView(m_texture_stream_buffer.GetD3DBuffer(), &srv_desc,
                                                        m_texture_stream_buffer_srv_r16ui.ReleaseAndGetAddressOf());
  if (FAILED(hr))
  {
    Log_ErrorPrintf("Creation of texture buffer SRV failed: 0x%08X", hr);
    return false;
  }

  return true;
}

bool GPU_HW_D3D11::CreateStateObjects()
{
  HRESULT hr;

  CD3D11_RASTERIZER_DESC rs_desc = CD3D11_RASTERIZER_DESC(CD3D11_DEFAULT());
  rs_desc.CullMode = D3D11_CULL_NONE;
  rs_desc.ScissorEnable = TRUE;
  rs_desc.MultisampleEnable = IsUsingMultisampling();
  rs_desc.DepthClipEnable = FALSE;
  hr = m_device->CreateRasterizerState(&rs_desc, m_cull_none_rasterizer_state.ReleaseAndGetAddressOf());
  if (FAILED(hr))
    return false;
  if (IsUsingMultisampling())
  {
    rs_desc.MultisampleEnable = FALSE;
    hr = m_device->CreateRasterizerState(&rs_desc, m_cull_none_rasterizer_state_no_msaa.ReleaseAndGetAddressOf());
    if (FAILED(hr))
      return false;
  }
  else
  {
    m_cull_none_rasterizer_state_no_msaa = m_cull_none_rasterizer_state;
  }

  CD3D11_DEPTH_STENCIL_DESC ds_desc = CD3D11_DEPTH_STENCIL_DESC(CD3D11_DEFAULT());
  ds_desc.DepthEnable = FALSE;
  ds_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
  hr = m_device->CreateDepthStencilState(&ds_desc, m_depth_disabled_state.ReleaseAndGetAddressOf());
  if (FAILED(hr))
    return false;

  ds_desc.DepthEnable = TRUE;
  ds_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
  ds_desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
  hr = m_device->CreateDepthStencilState(&ds_desc, m_depth_test_always_state.ReleaseAndGetAddressOf());
  if (FAILED(hr))
    return false;

  ds_desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
  hr = m_device->CreateDepthStencilState(&ds_desc, m_depth_test_less_state.ReleaseAndGetAddressOf());
  if (FAILED(hr))
    return false;

  ds_desc.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;
  hr = m_device->CreateDepthStencilState(&ds_desc, m_depth_test_greater_state.ReleaseAndGetAddressOf());
  if (FAILED(hr))
    return false;

  CD3D11_BLEND_DESC bl_desc = CD3D11_BLEND_DESC(CD3D11_DEFAULT());
  hr = m_device->CreateBlendState(&bl_desc, m_blend_disabled_state.ReleaseAndGetAddressOf());
  if (FAILED(hr))
    return false;

  bl_desc.RenderTarget[0].RenderTargetWriteMask = 0;
  hr = m_device->CreateBlendState(&bl_desc, m_blend_no_color_writes_state.ReleaseAndGetAddressOf());
  if (FAILED(hr))
    return false;

  CD3D11_SAMPLER_DESC sampler_desc = CD3D11_SAMPLER_DESC(CD3D11_DEFAULT());
  sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
  sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
  sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
  hr = m_device->CreateSamplerState(&sampler_desc, m_point_sampler_state.ReleaseAndGetAddressOf());
  if (FAILED(hr))
    return false;

  sampler_desc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
  hr = m_device->CreateSamplerState(&sampler_desc, m_linear_sampler_state.ReleaseAndGetAddressOf());
  if (FAILED(hr))
    return false;

  sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  hr = m_device->CreateSamplerState(&sampler_desc, m_trilinear_sampler_state.ReleaseAndGetAddressOf());
  if (FAILED(hr))
    return false;

  for (uint8_t transparency_mode = 0; transparency_mode < 5; transparency_mode++)
  {
    bl_desc = CD3D11_BLEND_DESC(CD3D11_DEFAULT());
    if (transparency_mode != static_cast<uint8_t>(GPUTransparencyMode::Disabled) ||
        m_texture_filtering != GPUTextureFilter::Nearest)
    {
      bl_desc.RenderTarget[0].BlendEnable = TRUE;
      bl_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
      bl_desc.RenderTarget[0].DestBlend = D3D11_BLEND_SRC1_ALPHA;
      bl_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
      bl_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
      bl_desc.RenderTarget[0].BlendOp =
        (transparency_mode == static_cast<uint8_t>(GPUTransparencyMode::BackgroundMinusForeground)) ?
          D3D11_BLEND_OP_REV_SUBTRACT :
          D3D11_BLEND_OP_ADD;
      bl_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    }

    hr = m_device->CreateBlendState(&bl_desc, m_batch_blend_states[transparency_mode].ReleaseAndGetAddressOf());
    if (FAILED(hr))
      return false;
  }

  return true;
}

void GPU_HW_D3D11::DestroyStateObjects()
{
  m_batch_blend_states = {};
  m_linear_sampler_state.Reset();
  m_point_sampler_state.Reset();
  m_trilinear_sampler_state.Reset();
  m_blend_no_color_writes_state.Reset();
  m_blend_disabled_state.Reset();
  m_depth_test_greater_state.Reset();
  m_depth_test_less_state.Reset();
  m_depth_test_always_state.Reset();
  m_depth_disabled_state.Reset();
  m_cull_none_rasterizer_state.Reset();
  m_cull_none_rasterizer_state_no_msaa.Reset();
}

bool GPU_HW_D3D11::CompileShaders()
{
  // Make sure no previous background-compile worker is still alive
  // from a prior CompileShaders call (UpdateSettings triggers
  // DestroyShaders -> CompileShaders, and the worker from the
  // previous incarnation has to be joined before we start a new one
  // so it doesn't keep writing into the matrix the new run is about
  // to fill in).
  StopShaderCompileThread();

  // Whether to walk the full batch-fragment-shader matrix
  // synchronously from this thread, hand it to a background thread,
  // or skip it entirely. See the comment on GPUShaderPrecompileMode
  // in core/types.h.
  const GPUShaderPrecompileMode precompile_mode = g_settings.gpu_shader_precompile_mode;
  const bool precompile_sync = (precompile_mode == GPUShaderPrecompileMode::Enabled);
  // batch_progress_units counts only reachable cells in the (render,
  // texture, dither, interlace) matrix - see IsBatchShaderReachable in
  // gpu_hw.h. This lets the progress bar end at exactly the number of
  // compiles the precompile loop will perform.
  const uint32_t batch_progress_units =
    precompile_sync ? CountReachableBatchShaders(m_supports_dual_source_blend) : 0u;

  ShaderCompileProgressTracker progress("Compiling Shaders",
                                        1 + 1 + 2 + batch_progress_units + 1 + (2 * 2) + 4 + (2 * 3) + 1);

  // input layout
  {
    static constexpr std::array<D3D11_INPUT_ELEMENT_DESC, 5> attributes = {
      {{"ATTR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(BatchVertex, x), D3D11_INPUT_PER_VERTEX_DATA, 0},
       {"ATTR", 1, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(BatchVertex, color), D3D11_INPUT_PER_VERTEX_DATA, 0},
       {"ATTR", 2, DXGI_FORMAT_R32_UINT, 0, offsetof(BatchVertex, u), D3D11_INPUT_PER_VERTEX_DATA, 0},
       {"ATTR", 3, DXGI_FORMAT_R32_UINT, 0, offsetof(BatchVertex, texpage), D3D11_INPUT_PER_VERTEX_DATA, 0},
       {"ATTR", 4, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(BatchVertex, uv_limits), D3D11_INPUT_PER_VERTEX_DATA, 0}}};

    // we need a vertex shader's bytecode for the input layout. Use the
    // pre-baked textured batch VS DXBC directly - the input layout only
    // needs the bytecode's input signature, and the textured variant
    // carries the full ATTR0..ATTR4 set.
    const auto vs_bc = D3DCommon::EmbeddedShaders::PickBatchVertexShader(true);

    // num_attributes is now unconditionally attributes.size(). Before
    // the UV_LIMITS-to-cbuffer routing commit, this used to drop the
    // ATTR4 / a_uv_limits binding when m_using_uv_limits was false
    // (matching the shadergen's conditional emission of the input).
    // Post-routing, the shadergen always emits a_uv_limits as a VS
    // input when textured, and BatchVertex always carries the
    // uv_limits field, so the input layout always binds ATTR4. The
    // FS gates whether to actually consume the value via the
    // u_uv_limits cbuffer scalar.
    const UINT num_attributes = static_cast<UINT>(attributes.size());
    const HRESULT hr =
      m_device->CreateInputLayout(attributes.data(), num_attributes, vs_bc.data,
                                  vs_bc.size, m_batch_input_layout.ReleaseAndGetAddressOf());
    if (FAILED(hr))
    {
      Log_ErrorPrintf("CreateInputLayout failed: 0x%08X", hr);
      return false;
    }
  }

  progress.Increment();

  {
    // screen-quad VS: byte-equivalent to the fullscreen-quad VS on D3D
    // (identical SV_VertexID triangle; the shadergen's GL-only y-flip
    // is dead here), so reuse the pre-baked k_fullscreen_quad_vs blob.
    m_screen_quad_vertex_shader = D3D11::ShaderCompiler::CreateVertexShader(
      m_device.Get(), D3DCommon::EmbeddedShaders::k_fullscreen_quad_vs,
      D3DCommon::EmbeddedShaders::k_fullscreen_quad_vs_size_bytes);
    // uv-quad VS: dedicated pre-baked blob (cbuffer-driven UV sub-rect).
    m_uv_quad_vertex_shader = D3D11::ShaderCompiler::CreateVertexShader(
      m_device.Get(), D3DCommon::EmbeddedShaders::k_uv_quad_vs,
      D3DCommon::EmbeddedShaders::k_uv_quad_vs_size_bytes);
  }
  if (!m_screen_quad_vertex_shader || !m_uv_quad_vertex_shader)
    return false;

  progress.Increment();

  for (uint8_t textured = 0; textured < 2; textured++)
  {
    // Pre-baked batch VS DXBC (PickBatchVertexShader); wrap into an
    // ID3D11VertexShader via CreateVertexShader instead of compiling
    // GenerateBatchVertexShader through shader_cache.
    const auto bc = D3DCommon::EmbeddedShaders::PickBatchVertexShader(static_cast<bool>(textured));
    m_batch_vertex_shaders[textured] =
      D3D11::ShaderCompiler::CreateVertexShader(m_device.Get(), bc.data, bc.size);
    if (!m_batch_vertex_shaders[textured])
      return false;

    progress.Increment();
  }

  // Batch fragment shader matrix - see PrecompileBatchShaders for
  // the Enabled / Lazy / Disabled behaviour, the
  // Reserved_*Direct16Bit dedup, the IsBatchShaderReachable filter,
  // and the dim cache's "walk current m_texture_filtering sub-cube
  // only" rule. progress is sized so the bar lands at 100% across
  // the reachable cell count (CountReachableBatchShaders).
  if (!PrecompileBatchShaders(progress))
    return false;
  // Lazy worker launch lives inside PrecompileBatchShaders now -
  // safe to start before the non-batch builds below because the
  // worker only walks m_batch_pixel_shader_fastpath.

  {
    const auto bc = D3DCommon::EmbeddedShaders::PickCopyFS();
    m_copy_pixel_shader = D3D11::ShaderCompiler::CreatePixelShader(m_device.Get(), bc.data, bc.size);
  }
  if (!m_copy_pixel_shader)
    return false;

  progress.Increment();

  for (uint8_t wrapped = 0; wrapped < 2; wrapped++)
  {
    for (uint8_t interlaced = 0; interlaced < 2; interlaced++)
    {
      const auto bc = D3DCommon::EmbeddedShaders::PickVRAMFillFS(
        m_pgxp_depth_buffer, static_cast<bool>(wrapped), static_cast<bool>(interlaced));
      m_vram_fill_pixel_shaders[wrapped][interlaced] =
        D3D11::ShaderCompiler::CreatePixelShader(m_device.Get(), bc.data, bc.size);
      if (!m_vram_fill_pixel_shaders[wrapped][interlaced])
        return false;

      progress.Increment();
    }
  }

  {
    const auto bc = D3DCommon::EmbeddedShaders::PickVRAMReadFS(m_multisamples);
    m_vram_read_pixel_shader =
      D3D11::ShaderCompiler::CreatePixelShader(m_device.Get(), bc.data, bc.size);
  }
  if (!m_vram_read_pixel_shader)
    return false;

  progress.Increment();

  {
    const auto bc = D3DCommon::EmbeddedShaders::PickVRAMWriteFS(m_pgxp_depth_buffer);
    m_vram_write_pixel_shader =
      D3D11::ShaderCompiler::CreatePixelShader(m_device.Get(), bc.data, bc.size);
  }
  if (!m_vram_write_pixel_shader)
    return false;

  progress.Increment();

  {
    const auto bc = D3DCommon::EmbeddedShaders::PickVRAMCopyFS(m_pgxp_depth_buffer);
    m_vram_copy_pixel_shader =
      D3D11::ShaderCompiler::CreatePixelShader(m_device.Get(), bc.data, bc.size);
  }
  if (!m_vram_copy_pixel_shader)
    return false;

  progress.Increment();

  {
    const auto bc = D3DCommon::EmbeddedShaders::PickVRAMUpdateDepthFS(m_multisamples > 1);
    m_vram_update_depth_pixel_shader =
      D3D11::ShaderCompiler::CreatePixelShader(m_device.Get(), bc.data, bc.size);
  }
  if (!m_vram_update_depth_pixel_shader)
    return false;

  progress.Increment();

  if (!RebuildDisplayPixelShaders())
    return false;
  for (uint8_t i = 0; i < 6; i++)
    progress.Increment();

  if (!CompileDownsampleShaders())
    return false;

  progress.Increment();

#undef UPDATE_PROGRESS

  return true;
}

bool GPU_HW_D3D11::CompileDownsampleShaders()
{
  // (Re)create the downsample-pass pixel shaders for the current
  // m_downsample_mode / m_resolution_scale. Split out of CompileShaders
  // so UpdateSettings can rebuild just these on a downsample-mode change:
  // GPU_HW::UpdateHWSettings deliberately keeps downsample mode out of
  // its shaders_changed diff (it does not touch the batch matrix) and
  // surfaces it via downsample_changed instead, so without this the
  // shaders stay null after a runtime Disabled/Box/Adaptive switch -
  // Adaptive then composites a black pyramid (black screen) and Box
  // draws with a null shader. Disabled is a no-op (the existing handles,
  // if any, are simply unused).
  if (m_downsample_mode == GPUDownsampleMode::Adaptive)
  {
    // Pre-baked path. 4 picker calls + 4 CreatePixelShader calls
    // replace the shadergen + D3DCompile + shader_cache lookup
    // pattern used pre-this-commit. The pickers consult the .inc
    // blobs at src/common/d3d_common/embedded_dxbc/ via
    // D3DCommon::EmbeddedShaders. No on-disk shader-cache entry
    // is produced for these passes - the bytecode is statically
    // linked and the device-object refcount is cheap to acquire
    // per session. The Adaptive composite picker takes
    // m_resolution_scale as input; the Adaptive mip picker takes
    // the FIRST_PASS bool (one true, one false to populate the
    // first / mid slots). The blur picker has no inputs.
    {
      const auto bc = D3DCommon::EmbeddedShaders::PickAdaptiveDownsampleMipFS(true);
      m_downsample_first_pass_pixel_shader =
        D3D11::ShaderCompiler::CreatePixelShader(m_device.Get(), bc.data, bc.size);
    }
    {
      const auto bc = D3DCommon::EmbeddedShaders::PickAdaptiveDownsampleMipFS(false);
      m_downsample_mid_pass_pixel_shader =
        D3D11::ShaderCompiler::CreatePixelShader(m_device.Get(), bc.data, bc.size);
    }
    {
      const auto bc = D3DCommon::EmbeddedShaders::PickAdaptiveDownsampleBlurFS();
      m_downsample_blur_pass_pixel_shader =
        D3D11::ShaderCompiler::CreatePixelShader(m_device.Get(), bc.data, bc.size);
    }
    {
      const auto bc = D3DCommon::EmbeddedShaders::PickAdaptiveDownsampleCompositeFS(m_resolution_scale);
      m_downsample_composite_pixel_shader =
        D3D11::ShaderCompiler::CreatePixelShader(m_device.Get(), bc.data, bc.size);
    }

    if (!m_downsample_first_pass_pixel_shader || !m_downsample_mid_pass_pixel_shader ||
        !m_downsample_blur_pass_pixel_shader || !m_downsample_composite_pixel_shader)
    {
      return false;
    }
  }
  else if (m_downsample_mode == GPUDownsampleMode::Box)
  {
    // Pre-baked Box-filter downsample. resolution_scale is the
    // only variant axis; the picker returns the matching .inc
    // blob from the 15-entry [scale=2..16] table.
    const auto bc = D3DCommon::EmbeddedShaders::PickBoxSampleDownsampleFS(m_resolution_scale);
    m_downsample_first_pass_pixel_shader =
      D3D11::ShaderCompiler::CreatePixelShader(m_device.Get(), bc.data, bc.size);
    if (!m_downsample_first_pass_pixel_shader)
      return false;
  }

  return true;
}

void GPU_HW_D3D11::StartShaderCompileThread()
{
  retro_atomic_store_release_int(&m_shader_compile_thread_quit, 0);
  m_shader_compile_thread = sthread_create(
    [](void* self) { static_cast<GPU_HW_D3D11*>(self)->ShaderCompileThreadEntryPoint(); }, this);
}

void GPU_HW_D3D11::StopShaderCompileThread()
{
  if (!m_shader_compile_thread)
    return;

  retro_atomic_store_release_int(&m_shader_compile_thread_quit, 1);
  sthread_join(m_shader_compile_thread);
  m_shader_compile_thread = nullptr;
  retro_atomic_store_release_int(&m_shader_compile_thread_quit, 0);
}

void GPU_HW_D3D11::ShaderCompileThreadEntryPoint()
{
  // Lower this worker's scheduling priority to "below normal" so
  // it doesn't compete with the runloop / CPU emulation / audio
  // threads on CPU-contended systems. Best-effort: if the platform
  // refuses we just keep going at default priority. See
  // common/thread_priority.h for the per-platform mechanics.
  ThreadPriority::LowerCurrentThreadPriority();

  // Walk the matrix in (render, texture, dither, interlace) order
  // and call GetBatchPixelShader on each cell. Each call wraps the
  // pre-baked DXBC blob into an ID3D11PixelShader via
  // CreatePixelShader and publishes it with a compare-exchange on
  // the slot. No D3DCompile runs -
  // the batch FS set is fully pre-baked (f2620c1), so per-cell cost
  // is just the CreatePixelShader wrap. The main thread can race
  // ahead and pre-fill any slot it actually needs at draw time
  // without waiting for the worker to reach them, and the worker's
  // race-loser detection picks up any slot the main thread filled
  // first. The quit flag is checked between cells so DestroyShaders
  // can bring the worker down within at most one CreatePixelShader
  // worth of latency.
  //
  // Structurally unreachable cells are skipped via
  // IsBatchShaderReachable - see the comment on that helper in
  // gpu_hw.h. Reserved texture modes alias the canonical slot
  // through the dedup logic in GetBatchPixelShader, so first-fault
  // on the alias only does a ComPtr copy, not a compile. The
  // two-pass-fallback render modes with no texture are never
  // selected by FlushRender. TransparentAndOpaque with a texture
  // mode is never selected on hardware without dual-source blend.
  //
  // The worker captures the runtime-current m_texture_filtering at
  // launch time and walks only that filter's sub-cube. Mirrors what
  // CompileShaders does in precompile_sync mode - both paths fill
  // the active filter's sub-cube and leave the other six sub-cubes
  // empty, to be lazy-faulted in via GetBatchPixelShader on the
  // main thread if a filter toggle later brings them into use.
  // UpdateSettings calls StopShaderCompileThread BEFORE
  // UpdateHWSettings writes m_texture_filtering, so the snapshot
  // here matches the filter the worker is supposed to be warming -
  // the next CompileShaders starts a fresh worker for the new
  // filter.
  const bool dual_source = m_supports_dual_source_blend;
  const GPUTextureFilter cur_filter = m_texture_filtering;
  for (uint8_t render_mode = 0; render_mode < 4; render_mode++)
  {
    for (uint8_t texture_mode = 0; texture_mode < 9; texture_mode++)
    {
      if (!IsBatchShaderReachable(static_cast<BatchRenderMode>(render_mode), texture_mode, dual_source))
        continue;

      for (uint8_t dithering = 0; dithering < 2; dithering++)
      {
        for (uint8_t interlacing = 0; interlacing < 2; interlacing++)
        {
          if (retro_atomic_load_acquire_int(&m_shader_compile_thread_quit))
            return;

          GetBatchPixelShader(cur_filter, render_mode, texture_mode, static_cast<bool>(dithering),
                              static_cast<bool>(interlacing));
        }
      }
    }
  }
}

ID3D11PixelShader* GPU_HW_D3D11::GetBatchPixelShader(GPUTextureFilter filter, uint8_t render_mode, uint8_t texture_mode, bool dithering, bool interlacing)
{
  // Apply the Reserved_*Direct16Bit dedup at the matrix level. The
  // shader source for texture_mode 3 / 7 is byte-for-byte identical
  // to 2 / 6 after macro expansion, so they share the canonical slot.
  const uint8_t lookup_mode = (texture_mode == static_cast<uint8_t>(GPUTextureMode::Reserved_Direct16Bit))    ? 2u :
                         (texture_mode == static_cast<uint8_t>(GPUTextureMode::Reserved_RawDirect16Bit)) ? 6u :
                                                                                                      texture_mode;
  const uint8_t filter_idx = static_cast<uint8_t>(filter);

  // Fast path: lock-free atomic acquire-load of the caller's slot.
  // If it's filled (the worker reached it first, or an earlier
  // main-thread fault-in did), we're done with no mutex / kernel
  // call / contention against the worker.
  AtomicSlot<ID3D11PixelShader*>& fast_slot =
    m_batch_pixel_shader_fastpath[filter_idx][render_mode][lookup_mode][static_cast<uint8_t>(dithering)][static_cast<uint8_t>(interlacing)];
  ID3D11PixelShader* existing = fast_slot.Load();
  if (existing)
    return existing;

  // Slow path. ID3D11Device::CreatePixelShader is documented free-threaded by
  // Microsoft, so multiple threads can wrap different pre-baked DXBC
  // blobs into ID3D11PixelShader objects here in parallel. Two
  // threads racing to wrap the SAME slot both produce equivalent
  // objects; the loser's ComPtr is released when it falls out of
  // scope below.
  //
  // Every batch FS variant is pre-baked (the batch FS pre-bake arc
  // completed at f2620c1). The shared pickers in
  // D3DCommon::EmbeddedShaders return the matching .inc-supplied
  // DXBC blob, which we hand straight to
  // D3D11::ShaderCompiler::CreatePixelShader (a thin wrapper over
  // ID3D11Device::CreatePixelShader with the byte / size pair):
  //   - Untextured (texture_mode == Disabled): PickBatchUntexturedFS
  //   - Textured + Nearest:   PickBatchTexturedNearestFS
  //   - Textured + Bilinear:  PickBatchTexturedBilinearFS
  //   - Textured + JINC2:     PickBatchTexturedJINC2FS
  //   - Textured + xBR:       PickBatchTexturedXBRFS
  // These are the same blobs the D3D12 backend consumes; fxc emits
  // ps_5_0 that both APIs honour identically. No shadergen, no
  // D3DCompile, no shader cache - every D3D11 shader (batch FS / VS,
  // screen / UV quad VS, copy / vram / display PS) is pre-baked and
  // wrapped straight into a shader object, so the runtime HLSL
  // compiler and the disk bytecode cache have both been removed.
  //
  // The use_dual_source bit is the same shadergen formula the
  // pickers consume on D3D12 (m_supports_dual_source_blend AND
  // (transparent render_mode OR non-Nearest filter)). Computed
  // once at the call site so the pre-baked picker and any future
  // PSO blend-state branching see the same value.
  const bool use_dual_source =
    m_supports_dual_source_blend &&
    ((render_mode != static_cast<uint8_t>(BatchRenderMode::TransparencyDisabled) &&
      render_mode != static_cast<uint8_t>(BatchRenderMode::OnlyOpaque)) ||
     filter != GPUTextureFilter::Nearest);
  const bool untextured =
    (static_cast<GPUTextureMode>(lookup_mode) == GPUTextureMode::Disabled);
  const bool textured_nearest =
    !untextured && (filter == GPUTextureFilter::Nearest);
  const bool textured_bilinear =
    !untextured && (filter == GPUTextureFilter::Bilinear ||
                    filter == GPUTextureFilter::BilinearBinAlpha);
  const bool textured_jinc2 =
    !untextured && (filter == GPUTextureFilter::JINC2 ||
                    filter == GPUTextureFilter::JINC2BinAlpha);

  ComPtr<ID3D11PixelShader> fresh;
  if (untextured)
  {
    const auto bc = D3DCommon::EmbeddedShaders::PickBatchUntexturedFS(
      use_dual_source, m_multisamples, m_per_sample_shading,
      m_disable_color_perspective);
    fresh = D3D11::ShaderCompiler::CreatePixelShader(m_device.Get(), bc.data, bc.size);
  }
  else if (textured_nearest)
  {
    const auto bc = D3DCommon::EmbeddedShaders::PickBatchTexturedNearestFS(
      lookup_mode, use_dual_source, m_multisamples, m_per_sample_shading,
      m_disable_color_perspective);
    fresh = D3D11::ShaderCompiler::CreatePixelShader(m_device.Get(), bc.data, bc.size);
  }
  else if (textured_bilinear)
  {
    // Third pre-baked batch FS slice. Mirror of the D3D12 branch
    // at GetBatchPipeline (gpu_hw_d3d12.cpp; same picker, same
    // input set). binalpha drives the BINALPHA -D macro arm of
    // the Bilinear template: BilinearBinAlpha => true (b1 suffix),
    // Bilinear => false (b0 suffix).
    const bool binalpha = (filter == GPUTextureFilter::BilinearBinAlpha);
    const auto bc = D3DCommon::EmbeddedShaders::PickBatchTexturedBilinearFS(
      lookup_mode, binalpha, use_dual_source, m_multisamples,
      m_per_sample_shading, m_disable_color_perspective);
    fresh = D3D11::ShaderCompiler::CreatePixelShader(m_device.Get(), bc.data, bc.size);
  }
  else if (textured_jinc2)
  {
    // Fourth pre-baked batch FS slice. Picker structure identical
    // to Bilinear's; the BINALPHA -D arm distinguishes JINC2 vs
    // JINC2BinAlpha. The HLSL body's 16-tap sinc-windowed
    // resampler with anti-ringing is what differentiates the
    // runtime behaviour from Bilinear's 4-tap weighted average.
    const bool binalpha = (filter == GPUTextureFilter::JINC2BinAlpha);
    const auto bc = D3DCommon::EmbeddedShaders::PickBatchTexturedJINC2FS(
      lookup_mode, binalpha, use_dual_source, m_multisamples,
      m_per_sample_shading, m_disable_color_perspective);
    fresh = D3D11::ShaderCompiler::CreatePixelShader(m_device.Get(), bc.data, bc.size);
  }
  else
  {
    // Fifth and final pre-baked batch FS slice (e07ce04 foundation
    // + this commit's activation). The remaining filter values are
    // xBR / xBRBinAlpha by elimination. Picker structure identical
    // to Bilinear / JINC2; the body of the picked DXBC is xBR's
    // 5x5 neighbourhood + 4-quadrant blend decision tree + per-
    // quadrant line-blend special cases. binalpha drives the
    // BINALPHA -D arm: xBRBinAlpha quantises the blend-weighted
    // alpha to {0, 1} before the `ialpha < 0.5 ? discard : ...`
    // test.
    //
    // GPUTextureFilter::Count is unreachable here (the filter enum
    // is set from settings and validated at parse time), so this
    // arm covers exactly {xBR, xBRBinAlpha}. The shadergen +
    // tmp_shadergen + m_shader_cache.GetPixelShader fallback path
    // that used to live here is deleted - all batch FS variants
    // now consume pre-baked DXBC via D3DCommon::EmbeddedShaders
    // pickers.
    const bool binalpha = (filter == GPUTextureFilter::xBRBinAlpha);
    const auto bc = D3DCommon::EmbeddedShaders::PickBatchTexturedXBRFS(
      lookup_mode, binalpha, use_dual_source, m_multisamples,
      m_per_sample_shading, m_disable_color_perspective);
    fresh = D3D11::ShaderCompiler::CreatePixelShader(m_device.Get(), bc.data, bc.size);
  }

  if (!fresh)
  {
    Log_ErrorPrintf("Lazy batch pixel shader compile failed for (f=%u, rm=%u, tm=%u, d=%u, i=%u)",
                    static_cast<uint8_t>(filter), render_mode, texture_mode,
                    static_cast<uint8_t>(dithering), static_cast<uint8_t>(interlacing));
    return nullptr;
  }

  // Publish step. The loser of a race gets the winner's shader and
  // drops its own.
  return PublishComSlot(fast_slot, fresh);
}

void GPU_HW_D3D11::DestroyShaders()
{
  // Tear the background compile thread down before clearing the
  // matrix - otherwise the worker would be writing into ComPtrs we're
  // about to default-construct.
  StopShaderCompileThread();

  m_downsample_composite_pixel_shader.Reset();
  m_downsample_blur_pass_pixel_shader.Reset();
  m_downsample_mid_pass_pixel_shader.Reset();
  m_downsample_first_pass_pixel_shader.Reset();
  m_display_pixel_shaders = {};
  m_vram_update_depth_pixel_shader.Reset();
  m_vram_copy_pixel_shader.Reset();
  m_vram_write_pixel_shader.Reset();
  m_vram_read_pixel_shader.Reset();
  m_vram_fill_pixel_shaders = {};
  m_copy_pixel_shader.Reset();
  m_uv_quad_vertex_shader.Reset();
  m_screen_quad_vertex_shader.Reset();

  // The worker is stopped (StopShaderCompileThread above) and the
  // runloop isn't drawing, so the slots can be released in place.
  for (auto& a : m_batch_pixel_shader_fastpath)
    for (auto& b : a)
      for (auto& c : b)
        for (auto& d : c)
          for (auto& slot : d)
            ReleaseComSlot(slot);

  m_batch_vertex_shaders = {};
  m_batch_input_layout.Reset();
}

bool GPU_HW_D3D11::RebuildDisplayPixelShaders()
{
  // (Re)select the 2x3 display pixel shader matrix
  // (m_display_pixel_shaders[depth_24bit][interlacing]) from the
  // pre-baked DXBC via D3DCommon::EmbeddedShaders::PickDisplayFS.
  // Called from CompileShaders during the initial build and from
  // UpdateSettings on a chroma_smoothing-only flip - smooth_chroma is
  // an axis of the display FS only, so the batch pixel shader matrix
  // and the VRAM ops pixel shaders stay valid through a chroma toggle
  // and don't need rebuilding. Cost is 6 CreatePixelShader calls
  // wrapping pre-baked blobs (no D3DCompile) instead of a full
  // CompileShaders pass walking the entire batch matrix.
  //
  // smooth_chroma only takes effect on the depth_24bit paths, so the
  // three depth_24bit=false cells pass smooth_chroma=false (PickDisplayFS
  // ignores the axis there - the picker's d0 table has no chroma
  // dimension), and the three depth_24bit=true cells pick up the
  // current m_chroma_smoothing. The session m_multisamples is the
  // fourth picker axis. No m_shadergen dependency - the display FS is
  // fully pre-baked.
  for (uint8_t depth_24bit = 0; depth_24bit < 2; depth_24bit++)
  {
    for (uint8_t interlacing = 0; interlacing < 3; interlacing++)
    {
      const bool smooth_chroma = static_cast<bool>(depth_24bit) && m_chroma_smoothing;
      const auto bc = D3DCommon::EmbeddedShaders::PickDisplayFS(
        static_cast<bool>(depth_24bit), interlacing, smooth_chroma, m_multisamples);
      m_display_pixel_shaders[depth_24bit][interlacing] =
        D3D11::ShaderCompiler::CreatePixelShader(m_device.Get(), bc.data, bc.size);
      if (!m_display_pixel_shaders[depth_24bit][interlacing])
        return false;
    }
  }
  return true;
}

bool GPU_HW_D3D11::PrecompileBatchShaders(ShaderCompileProgressTracker& progress)
{
  // Walk the current m_texture_filtering sub-cube of
  // m_batch_pixel_shader_fastpath synchronously in Enabled mode; launch the
  // background-thread batch-fragment-shader fill in Lazy mode; do
  // nothing in Disabled. Caller is responsible for joining any
  // previous worker (StopShaderCompileThread). There is no shader
  // cache to open or shadergen to construct any more - every batch FS
  // cell is wrapped from pre-baked DXBC.
  //
  // Extracted from CompileShaders so the only_dim_changed fast path
  // in UpdateSettings can call just this helper without paying the
  // ~10-50ms of cache-hit-but-still-wasted ComPtr churn that
  // CompileShaders' non-batch shader rebuild block incurs on every
  // filter flip. None of the non-batch pixel shaders (copy / VRAM
  // ops / display / downsample) or vertex shaders depend on filter,
  // so the existing ID3D11PixelShader / ID3D11VertexShader handles
  // in those slots stay valid across a filter toggle - dropping
  // them just to rebuild equivalent ones via the shader cache's
  // disk-backed DXBC blob path is pure overhead. See gpu_hw_
  // shadergen.cpp - GenerateBatchFragmentShader and
  // WriteBatchTextureFilter (lines ~706, ~728, ~839) are the only
  // callers that read m_texture_filter from the shadergen state.
  //
  // Structurally unreachable cells (reserved texture modes, two-pass
  // fallback modes for untextured polys, single-pass dual-source
  // on hardware that lacks it) are skipped via IsBatchShaderReachable.
  // Progress is ticked once per reachable cell so the bar lands at
  // batch_progress_units = CountReachableBatchShaders(dual_source).
  const GPUShaderPrecompileMode precompile_mode = g_settings.gpu_shader_precompile_mode;
  const bool precompile_sync = (precompile_mode == GPUShaderPrecompileMode::Enabled);

  if (precompile_sync)
  {
    const bool dual_source = m_supports_dual_source_blend;
    // The dim cache makes m_batch_pixel_shader_fastpath filter-dimensioned.
    // precompile_sync walks ONLY the current m_texture_filtering
    // sub-cube, not the full 7-filter matrix - pre-filling six
    // unused sub-cubes would multiply the cold-cache D3DCompile
    // pass by 7x for no gain (the game can only be running under
    // one filter at a time, and the other sub-cubes get faulted in
    // on demand if the user later flips the filter setting and
    // UpdateSettings calls into this helper again).
    const GPUTextureFilter cur_filter = m_texture_filtering;
    for (uint8_t render_mode = 0; render_mode < 4; render_mode++)
    {
      for (uint8_t texture_mode = 0; texture_mode < 9; texture_mode++)
      {
        if (!IsBatchShaderReachable(static_cast<BatchRenderMode>(render_mode), texture_mode, dual_source))
          continue;

        for (uint8_t dithering = 0; dithering < 2; dithering++)
        {
          for (uint8_t interlacing = 0; interlacing < 2; interlacing++)
          {
            ID3D11PixelShader* shader = GetBatchPixelShader(cur_filter, render_mode, texture_mode,
                                                            static_cast<bool>(dithering),
                                                            static_cast<bool>(interlacing));
            if (!shader)
              return false;

            progress.Increment();
          }
        }
      }
    }
  }

  if (precompile_mode == GPUShaderPrecompileMode::Lazy)
  {
    // Kick off the background-thread batch-fragment-shader fill so
    // gameplay can start while the rest of the matrix compiles. The
    // worker just walks the (render, texture, dither, interlace)
    // matrix in order, calling the same GetBatchPixelShader helper
    // the draw path uses, so any slot the game touches in the
    // meantime is just skipped here (the slot's compare-exchange
    // finds it already filled). DestroyShaders signals
    // m_shader_compile_thread_quit and joins.
    StartShaderCompileThread();
  }

  return true;
}

void GPU_HW_D3D11::UploadUniformBuffer(const void* data, uint32_t data_size)
{
  const auto res = m_uniform_stream_buffer.Map(m_context.Get(), MAX_UNIFORM_BUFFER_SIZE, data_size);
  std::memcpy(res.pointer, data, data_size);
  m_uniform_stream_buffer.Unmap(m_context.Get(), data_size);

  m_context->VSSetConstantBuffers(0, 1, m_uniform_stream_buffer.GetD3DBufferArray());
  m_context->PSSetConstantBuffers(0, 1, m_uniform_stream_buffer.GetD3DBufferArray());
}

void GPU_HW_D3D11::SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
  D3D11_VIEWPORT vp;
  vp.TopLeftX = static_cast<float>(x);
  vp.TopLeftY = static_cast<float>(y);
  vp.Width = static_cast<float>(width);
  vp.Height = static_cast<float>(height);
  vp.MinDepth = 0;
  vp.MaxDepth = 1;
  m_context->RSSetViewports(1, &vp);
}

void GPU_HW_D3D11::SetScissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
  D3D11_RECT rc;
  rc.left = x;
  rc.top = y;
  rc.right = (x + width);
  rc.bottom = (y + height);
  m_context->RSSetScissorRects(1, &rc);
}

void GPU_HW_D3D11::SetViewportAndScissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
  SetViewport(x, y, width, height);
  SetScissor(x, y, width, height);
}

void GPU_HW_D3D11::DrawUtilityShader(ID3D11PixelShader* shader, const void* uniforms, uint32_t uniforms_size)
{
  if (uniforms)
  {
    UploadUniformBuffer(uniforms, uniforms_size);
    m_batch_ubo_dirty = true;
  }

  m_context->VSSetShader(m_screen_quad_vertex_shader.Get(), nullptr, 0);
  m_context->GSSetShader(nullptr, nullptr, 0);
  m_context->PSSetShader(shader, nullptr, 0);
  m_context->OMSetBlendState(m_blend_disabled_state.Get(), nullptr, 0xFFFFFFFFu);

  m_context->Draw(3, 0);
}

bool GPU_HW_D3D11::BlitVRAMReplacementTexture(const TextureReplacementTexture* tex, uint32_t dst_x, uint32_t dst_y, uint32_t width,
                                              uint32_t height)
{
  if (m_vram_replacement_texture.GetWidth() < tex->GetWidth() ||
      m_vram_replacement_texture.GetHeight() < tex->GetHeight())
  {
    if (!m_vram_replacement_texture.Create(m_device.Get(), tex->GetWidth(), tex->GetHeight(), 1, 1,
                                           DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, tex->GetPixels(),
                                           tex->GetByteStride(), true))
    {
      return false;
    }
  }
  else
  {
    D3D11_MAPPED_SUBRESOURCE sr;
    HRESULT hr = m_context->Map(m_vram_replacement_texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &sr);
    if (FAILED(hr))
    {
      Log_ErrorPrintf("Texture map failed: %08X", hr);
      return false;
    }

    const uint32_t copy_size = std::min(tex->GetByteStride(), sr.RowPitch);
    const uint8_t* src_ptr = reinterpret_cast<const uint8_t*>(tex->GetPixels());
    uint8_t* dst_ptr = static_cast<uint8_t*>(sr.pData);
    for (uint32_t i = 0; i < tex->GetHeight(); i++)
    {
      std::memcpy(dst_ptr, src_ptr, copy_size);
      src_ptr += tex->GetByteStride();
      dst_ptr += sr.RowPitch;
    }

    m_context->Unmap(m_vram_replacement_texture, 0);
  }

  m_context->OMSetDepthStencilState(m_depth_disabled_state.Get(), 0);
  m_context->PSSetShaderResources(0, 1, m_vram_replacement_texture.GetD3DSRVArray());
  m_context->PSSetSamplers(0, 1, m_linear_sampler_state.GetAddressOf());
  SetViewportAndScissor(dst_x, dst_y, width, height);

  const float uniforms[] = {0.0f, 0.0f, 1.0f, 1.0f};
  DrawUtilityShader(m_copy_pixel_shader.Get(), uniforms, sizeof(uniforms));
  RestoreGraphicsAPIState();
  return true;
}

void GPU_HW_D3D11::DrawBatchVertices(BatchRenderMode render_mode, uint32_t base_vertex, uint32_t num_vertices)
{
  const bool textured = (m_batch.texture_mode != GPUTextureMode::Disabled);

  m_context->VSSetShader(m_batch_vertex_shaders[static_cast<uint8_t>(textured)].Get(), nullptr, 0);

  // Fetch the batch pixel shader via the lazy helper. In 'Enabled'
  // precompile mode every slot was already filled at CompileShaders
  // time so this is a single acquire load. In 'Lazy' mode this
  // either gets the already-compiled shader (background thread
  // reached it first) or compiles it now on the main thread (game
  // raced ahead of the worker). In 'Disabled' mode it always
  // compiles on miss.
  //
  // m_texture_filtering selects the active filter's sub-cube. Filter
  // is the outermost dim so a filter toggle in UpdateSettings can
  // skip DestroyShaders - the other filters' sub-cubes remain valid
  // and reachable, switching back to a previously-visited filter is
  // an atomic load on an already-filled slot.
  ID3D11PixelShader* batch_pixel_shader =
    GetBatchPixelShader(m_texture_filtering, static_cast<uint8_t>(render_mode), static_cast<uint8_t>(m_batch.texture_mode), m_batch.dithering,
                        m_batch.interlacing);
  m_context->PSSetShader(batch_pixel_shader, nullptr, 0);

  const GPUTransparencyMode transparency_mode =
    (render_mode == BatchRenderMode::OnlyOpaque) ? GPUTransparencyMode::Disabled : m_batch.transparency_mode;
  m_context->OMSetBlendState(m_batch_blend_states[static_cast<uint8_t>(transparency_mode)].Get(), nullptr, 0xFFFFFFFFu);

  m_context->OMSetDepthStencilState(
    (m_batch.use_depth_buffer ?
       m_depth_test_less_state.Get() :
       (m_batch.check_mask_before_draw ? m_depth_test_greater_state.Get() : m_depth_test_always_state.Get())),
    0);

  m_context->Draw(num_vertices, base_vertex);
}

void GPU_HW_D3D11::SetScissorFromDrawingArea()
{
  int left, top, right, bottom;
  CalcScissorRect(&left, &top, &right, &bottom);

  D3D11_RECT rc;
  rc.left = left;
  rc.top = top;
  rc.right = right;
  rc.bottom = bottom;
  m_context->RSSetScissorRects(1, &rc);
}

void GPU_HW_D3D11::ClearDisplay()
{
  GPU_HW::ClearDisplay();

  m_host_display->ClearDisplayTexture();

  static constexpr std::array<float, 4> clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
  m_context->ClearRenderTargetView(m_display_texture.GetD3DRTV(), clear_color.data());
}

void GPU_HW_D3D11::UpdateDisplay()
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

      if (IsUsingDownsampling())
      {
        DownsampleFramebuffer(m_vram_texture, scaled_vram_offset_x, scaled_vram_offset_y, scaled_display_width,
                              scaled_display_height);
      }
      else
      {
        m_host_display->SetDisplayTexture(m_vram_texture.GetD3DSRV(), HostDisplayPixelFormat::RGBA8,
                                          m_vram_texture.GetWidth(), m_vram_texture.GetHeight(), scaled_vram_offset_x,
                                          scaled_vram_offset_y, scaled_display_width, scaled_display_height);
      }
    }
    else
    {
      m_context->RSSetState(m_cull_none_rasterizer_state_no_msaa.Get());
      m_context->OMSetRenderTargets(1, m_display_texture.GetD3DRTVArray(), nullptr);
      m_context->OMSetDepthStencilState(m_depth_disabled_state.Get(), 0);
      m_context->PSSetShaderResources(0, 1, m_vram_texture.GetD3DSRVArray());

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
      ID3D11PixelShader* display_pixel_shader =
        m_display_pixel_shaders[static_cast<uint8_t>(m_GPUSTAT.display_area_color_depth_24)][static_cast<uint8_t>(interlaced)].Get();

      SetViewportAndScissor(0, 0, scaled_display_width, scaled_display_height);
      DrawUtilityShader(display_pixel_shader, uniforms, sizeof(uniforms));

      if (IsUsingDownsampling())
      {
        DownsampleFramebuffer(m_display_texture, 0, 0, scaled_display_width, scaled_display_height);
      }
      else
      {
        m_host_display->SetDisplayTexture(m_display_texture.GetD3DSRV(), HostDisplayPixelFormat::RGBA8,
                                          m_display_texture.GetWidth(), m_display_texture.GetHeight(), 0, 0,
                                          scaled_display_width, scaled_display_height);
      }

      RestoreGraphicsAPIState();
    }
  }
}

void GPU_HW_D3D11::ReadVRAM(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
  if (IsUsingSoftwareRendererForReadbacks())
  {
    ReadSoftwareRendererVRAM(x, y, width, height);
    return;
  }

  // Get bounds with wrap-around handled.
  const Common::Rectangle<uint32_t> copy_rect = GetVRAMTransferBounds(x, y, width, height);
  const uint32_t encoded_width = (copy_rect.GetWidth() + 1) / 2;
  const uint32_t encoded_height = copy_rect.GetHeight();

  // Encode the 24-bit texture as 16-bit.
  // 6 DWORDs to match the post-RESOLUTION_SCALE-refactor vram_read_ps
  // cbuffer (u_base_coords.xy, u_size.xy, u_resolution_scale, u_pad0).
  const uint32_t uniforms[6] = {copy_rect.left, copy_rect.top, copy_rect.GetWidth(), copy_rect.GetHeight(),
                                m_resolution_scale, 0u /* u_pad0 */};
  m_context->RSSetState(m_cull_none_rasterizer_state_no_msaa.Get());
  m_context->OMSetRenderTargets(1, m_vram_encoding_texture.GetD3DRTVArray(), nullptr);
  m_context->OMSetDepthStencilState(m_depth_disabled_state.Get(), 0);
  m_context->PSSetShaderResources(0, 1, m_vram_texture.GetD3DSRVArray());
  SetViewportAndScissor(0, 0, encoded_width, encoded_height);
  DrawUtilityShader(m_vram_read_pixel_shader.Get(), uniforms, sizeof(uniforms));

  // Stage the readback.
  m_vram_readback_texture.CopyFromTexture(m_context.Get(), m_vram_encoding_texture.GetD3DTexture(), 0, 0, 0, 0, 0,
                                          encoded_width, encoded_height);
  // And copy it into our shadow buffer.
  if (m_vram_readback_texture.Map(m_context.Get(), false))
  {
    m_vram_readback_texture.ReadPixels<uint32_t>(
      0, 0, encoded_width, encoded_height, VRAM_WIDTH * sizeof(uint16_t),
      reinterpret_cast<uint32_t*>(&m_vram_shadow[copy_rect.top * VRAM_WIDTH + copy_rect.left]));
    m_vram_readback_texture.Unmap(m_context.Get());
  }
  else
  {
    Log_ErrorPrintf("Failed to map VRAM readback texture");
  }

  RestoreGraphicsAPIState();
}

void GPU_HW_D3D11::FillVRAM(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color)
{
  if (IsUsingSoftwareRendererForReadbacks())
    FillSoftwareRendererVRAM(x, y, width, height, color);

  GPU_HW::FillVRAM(x, y, width, height, color);

  m_context->OMSetDepthStencilState(m_depth_test_always_state.Get(), 0);

  const Common::Rectangle<uint32_t> bounds(GetVRAMTransferBounds(x, y, width, height));
  SetViewportAndScissor(bounds.left * m_resolution_scale, bounds.top * m_resolution_scale,
                        bounds.GetWidth() * m_resolution_scale, bounds.GetHeight() * m_resolution_scale);

  const VRAMFillUBOData uniforms = GetVRAMFillUBOData(x, y, width, height, color);
  DrawUtilityShader(m_vram_fill_pixel_shaders[static_cast<uint8_t>(IsVRAMFillOversized(x, y, width, height))]
                                             [static_cast<uint8_t>(IsInterlacedRenderingEnabled())]
                                               .Get(),
                    &uniforms, sizeof(uniforms));

  RestoreGraphicsAPIState();
}

void GPU_HW_D3D11::UpdateVRAM(uint32_t x, uint32_t y, uint32_t width, uint32_t height, const void* data, bool set_mask, bool check_mask)
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

  const uint32_t num_pixels = width * height;
  const auto map_result = m_texture_stream_buffer.Map(m_context.Get(), sizeof(uint16_t), num_pixels * sizeof(uint16_t));
  std::memcpy(map_result.pointer, data, num_pixels * sizeof(uint16_t));
  m_texture_stream_buffer.Unmap(m_context.Get(), num_pixels * sizeof(uint16_t));

  const VRAMWriteUBOData uniforms =
    GetVRAMWriteUBOData(x, y, width, height, map_result.index_aligned, set_mask, check_mask);
  m_context->OMSetDepthStencilState(
    (check_mask && !m_pgxp_depth_buffer) ? m_depth_test_greater_state.Get() : m_depth_test_always_state.Get(), 0);
  m_context->PSSetShaderResources(0, 1, m_texture_stream_buffer_srv_r16ui.GetAddressOf());

  // the viewport should already be set to the full vram, so just adjust the scissor
  const Common::Rectangle<uint32_t> scaled_bounds = bounds * m_resolution_scale;
  SetScissor(scaled_bounds.left, scaled_bounds.top, scaled_bounds.GetWidth(), scaled_bounds.GetHeight());

  DrawUtilityShader(m_vram_write_pixel_shader.Get(), &uniforms, sizeof(uniforms));

  RestoreGraphicsAPIState();
}

void GPU_HW_D3D11::CopyVRAM(uint32_t src_x, uint32_t src_y, uint32_t dst_x, uint32_t dst_y, uint32_t width, uint32_t height)
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

    const VRAMCopyUBOData uniforms = GetVRAMCopyUBOData(src_x, src_y, dst_x, dst_y, width, height);

    const Common::Rectangle<uint32_t> dst_bounds_scaled(dst_bounds * m_resolution_scale);
    SetViewportAndScissor(dst_bounds_scaled.left, dst_bounds_scaled.top, dst_bounds_scaled.GetWidth(),
                          dst_bounds_scaled.GetHeight());
    m_context->OMSetDepthStencilState((m_GPUSTAT.check_mask_before_draw && !m_pgxp_depth_buffer) ?
                                        m_depth_test_greater_state.Get() :
                                        m_depth_test_always_state.Get(),
                                      0);
    m_context->PSSetShaderResources(0, 1, m_vram_read_texture.GetD3DSRVArray());
    DrawUtilityShader(m_vram_copy_pixel_shader.Get(), &uniforms, sizeof(uniforms));
    RestoreGraphicsAPIState();

    if (m_GPUSTAT.check_mask_before_draw && !m_pgxp_depth_buffer)
      m_current_depth++;

    return;
  }

  // We can't CopySubresourceRegion to the same resource. So use the shadow texture if we can, but that may need to be
  // updated first. Copying to the same resource seemed to work on Windows 10, but breaks on Windows 7. But, it's
  // against the API spec, so better to be safe than sorry.
  if (m_vram_dirty_rect.Intersects(Common::Rectangle<uint32_t>::FromExtents(src_x, src_y, width, height)))
    UpdateVRAMReadTexture();

  GPU_HW::CopyVRAM(src_x, src_y, dst_x, dst_y, width, height);

  src_x *= m_resolution_scale;
  src_y *= m_resolution_scale;
  dst_x *= m_resolution_scale;
  dst_y *= m_resolution_scale;
  width *= m_resolution_scale;
  height *= m_resolution_scale;

  D3D11_BOX src_box;
  src_box.left = src_x;
  src_box.top = src_y;
  src_box.front = 0;
  src_box.right = (src_x + width);
  src_box.bottom = (src_y + height);
  src_box.back = 1;
  m_context->CopySubresourceRegion(m_vram_texture, 0, dst_x, dst_y, 0, m_vram_read_texture, 0, &src_box);
}

void GPU_HW_D3D11::UpdateVRAMReadTexture()
{
  const auto scaled_rect = m_vram_dirty_rect * m_resolution_scale;
  D3D11_BOX src_box;
  src_box.left = scaled_rect.left;
  src_box.top = scaled_rect.top;
  src_box.front = 0;
  src_box.right = scaled_rect.right;
  src_box.bottom = scaled_rect.bottom;
  src_box.back = 1;

  if (m_vram_texture.IsMultisampled())
  {
    m_context->ResolveSubresource(m_vram_read_texture.GetD3DTexture(), 0, m_vram_texture.GetD3DTexture(), 0,
                                  m_vram_texture.GetFormat());
  }
  else
  {
    m_context->CopySubresourceRegion(m_vram_read_texture, 0, scaled_rect.left, scaled_rect.top, 0, m_vram_texture, 0,
                                     &src_box);
  }

  GPU_HW::UpdateVRAMReadTexture();
}

void GPU_HW_D3D11::UpdateDepthBufferFromMaskBit()
{
  if (m_pgxp_depth_buffer)
    return;

  SetViewportAndScissor(0, 0, m_vram_texture.GetWidth(), m_vram_texture.GetHeight());

  m_context->OMSetRenderTargets(0, nullptr, m_vram_depth_view.Get());
  m_context->OMSetDepthStencilState(m_depth_test_always_state.Get(), 0);
  m_context->OMSetBlendState(m_blend_no_color_writes_state.Get(), nullptr, 0xFFFFFFFFu);

  m_context->PSSetShaderResources(0, 1, m_vram_texture.GetD3DSRVArray());
  DrawUtilityShader(m_vram_update_depth_pixel_shader.Get(), nullptr, 0);

  m_context->PSSetShaderResources(0, 1, m_vram_read_texture.GetD3DSRVArray());
  RestoreGraphicsAPIState();
}

void GPU_HW_D3D11::ClearDepthBuffer()
{
  m_context->ClearDepthStencilView(m_vram_depth_view.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
  m_last_depth_z = 1.0f;
}

void GPU_HW_D3D11::DownsampleFramebuffer(D3D11::Texture& source, uint32_t left, uint32_t top, uint32_t width, uint32_t height)
{
  if (m_downsample_mode == GPUDownsampleMode::Adaptive)
    DownsampleFramebufferAdaptive(source, left, top, width, height);
  else
    DownsampleFramebufferBoxFilter(source, left, top, width, height);
}

void GPU_HW_D3D11::DownsampleFramebufferAdaptive(D3D11::Texture& source, uint32_t left, uint32_t top, uint32_t width, uint32_t height)
{
  D3D11_BOX src_box;
  src_box.left = left;
  src_box.top = top;
  src_box.front = 0;
  src_box.right = (left + width);
  src_box.bottom = (top + height);
  src_box.back = 1;
  m_context->OMSetDepthStencilState(m_depth_disabled_state.Get(), 0);
  m_context->OMSetBlendState(m_blend_disabled_state.Get(), nullptr, 0xFFFFFFFFu);
  m_context->CopySubresourceRegion(m_downsample_texture, 0, left, top, 0, source, 0, &src_box);
  m_context->PSSetSamplers(0, 1, m_point_sampler_state.GetAddressOf());
  m_context->VSSetShader(m_uv_quad_vertex_shader.Get(), nullptr, 0);

  // create mip chain
  const uint32_t levels = m_downsample_texture.GetLevels();
  for (uint32_t level = 1; level < levels; level++)
  {
    static constexpr float clear_color[4] = {};

    SetViewportAndScissor(left >> level, top >> level, width >> level, height >> level);
    m_context->ClearRenderTargetView(m_downsample_mip_views[level].second.Get(), clear_color);
    m_context->OMSetRenderTargets(1, m_downsample_mip_views[level].second.GetAddressOf(), nullptr);
    m_context->PSSetShaderResources(0, 1, m_downsample_mip_views[level - 1].first.GetAddressOf());

    const SmoothingUBOData ubo = GetSmoothingUBO(level, left, top, width, height, m_downsample_texture.GetWidth(),
                                                 m_downsample_texture.GetHeight());
    m_context->PSSetShader(
      (level == 1) ? m_downsample_first_pass_pixel_shader.Get() : m_downsample_mid_pass_pixel_shader.Get(), nullptr, 0);
    UploadUniformBuffer(&ubo, sizeof(ubo));
    m_context->Draw(3, 0);
  }

  // blur pass at lowest level
  {
    const uint32_t last_level = levels - 1;
    static constexpr float clear_color[4] = {};

    SetViewportAndScissor(left >> last_level, top >> last_level, width >> last_level, height >> last_level);
    m_context->ClearRenderTargetView(m_downsample_weight_texture.GetD3DRTV(), clear_color);
    m_context->OMSetRenderTargets(1, m_downsample_weight_texture.GetD3DRTVArray(), nullptr);
    m_context->PSSetShaderResources(0, 1, m_downsample_mip_views.back().first.GetAddressOf());
    m_context->PSSetShader(m_downsample_blur_pass_pixel_shader.Get(), nullptr, 0);

    const SmoothingUBOData ubo = GetSmoothingUBO(last_level, left, top, width, height, m_downsample_texture.GetWidth(),
                                                 m_downsample_texture.GetHeight());
    m_context->PSSetShader(m_downsample_blur_pass_pixel_shader.Get(), nullptr, 0);
    UploadUniformBuffer(&ubo, sizeof(ubo));
    m_context->Draw(3, 0);
  }

  // composite downsampled and upsampled images together
  {
    SetViewportAndScissor(left, top, width, height);
    m_context->OMSetRenderTargets(1, m_display_texture.GetD3DRTVArray(), nullptr);

    ID3D11ShaderResourceView* const srvs[2] = {m_downsample_texture.GetD3DSRV(),
                                               m_downsample_weight_texture.GetD3DSRV()};
    ID3D11SamplerState* const samplers[2] = {m_trilinear_sampler_state.Get(), m_linear_sampler_state.Get()};
    m_context->PSSetShaderResources(0, countof(srvs), srvs);
    m_context->PSSetSamplers(0, countof(samplers), samplers);
    m_context->PSSetShader(m_downsample_composite_pixel_shader.Get(), nullptr, 0);
    m_context->Draw(3, 0);
  }

  ID3D11ShaderResourceView* const null_srvs[2] = {};
  m_context->PSSetShaderResources(0, countof(null_srvs), null_srvs);
  m_batch_ubo_dirty = true;

  RestoreGraphicsAPIState();

  m_host_display->SetDisplayTexture(m_display_texture.GetD3DSRV(), HostDisplayPixelFormat::RGBA8,
                                    m_display_texture.GetWidth(), m_display_texture.GetHeight(), left, top, width,
                                    height);
}

void GPU_HW_D3D11::DownsampleFramebufferBoxFilter(D3D11::Texture& source, uint32_t left, uint32_t top, uint32_t width, uint32_t height)
{
  const uint32_t ds_left = left / m_resolution_scale;
  const uint32_t ds_top = top / m_resolution_scale;
  const uint32_t ds_width = width / m_resolution_scale;
  const uint32_t ds_height = height / m_resolution_scale;
  static constexpr float clear_color[4] = {};

  m_context->ClearRenderTargetView(m_downsample_texture.GetD3DRTV(), clear_color);
  m_context->OMSetDepthStencilState(m_depth_disabled_state.Get(), 0);
  m_context->OMSetRenderTargets(1, m_downsample_texture.GetD3DRTVArray(), nullptr);
  m_context->OMSetBlendState(m_blend_disabled_state.Get(), nullptr, 0xFFFFFFFFu);
  m_context->VSSetShader(m_screen_quad_vertex_shader.Get(), nullptr, 0);
  m_context->PSSetShader(m_downsample_first_pass_pixel_shader.Get(), nullptr, 0);
  m_context->PSSetShaderResources(0, 1, source.GetD3DSRVArray());
  SetViewportAndScissor(ds_left, ds_top, ds_width, ds_height);
  m_context->Draw(3, 0);

  RestoreGraphicsAPIState();

  m_host_display->SetDisplayTexture(m_downsample_texture.GetD3DSRV(), HostDisplayPixelFormat::RGBA8,
                                    m_downsample_texture.GetWidth(), m_downsample_texture.GetHeight(), ds_left, ds_top,
                                    ds_width, ds_height);
}

std::unique_ptr<GPU> GPU::CreateHardwareD3D11Renderer()
{
  return std::make_unique<GPU_HW_D3D11>();
}
