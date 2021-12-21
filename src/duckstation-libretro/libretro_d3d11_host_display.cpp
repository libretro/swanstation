#include "libretro_d3d11_host_display.h"
#include "common/align.h"
#include "common/assert.h"
#include "common/d3d11/shader_cache.h"
#include "common/d3d11/shader_compiler.h"
#include "common/log.h"
#include "common/string_util.h"
#include "core/host_interface.h"
#include "core/settings.h"
#include "core/shader_cache_version.h"
#include "frontend-common/display_ps.hlsl.h"
#include "frontend-common/display_ps_alpha.hlsl.h"
#include "frontend-common/display_vs.hlsl.h"
#include "libretro_host_interface.h"
#include <array>
Log_SetChannel(LibretroD3D11HostDisplay);

#define HAVE_D3D11
#include "libretro_d3d.h"

class LibretroD3D11HostDisplayTexture : public HostDisplayTexture
{
public:
  LibretroD3D11HostDisplayTexture(D3D11::Texture texture, HostDisplayPixelFormat format, bool dynamic)
    : m_texture(std::move(texture)), m_format(format), m_dynamic(dynamic)
  {
  }
  ~LibretroD3D11HostDisplayTexture() override = default;

  void* GetHandle() const override { return m_texture.GetD3DSRV(); }
  u32 GetWidth() const override { return m_texture.GetWidth(); }
  u32 GetHeight() const override { return m_texture.GetHeight(); }
  u32 GetLayers() const override { return 1; }
  u32 GetLevels() const override { return m_texture.GetLevels(); }
  u32 GetSamples() const override { return m_texture.GetSamples(); }
  HostDisplayPixelFormat GetFormat() const override { return m_format; }

  ALWAYS_INLINE ID3D11Texture2D* GetD3DTexture() const { return m_texture.GetD3DTexture(); }
  ALWAYS_INLINE ID3D11ShaderResourceView* GetD3DSRV() const { return m_texture.GetD3DSRV(); }
  ALWAYS_INLINE ID3D11ShaderResourceView* const* GetD3DSRVArray() const { return m_texture.GetD3DSRVArray(); }
  ALWAYS_INLINE bool IsDynamic() const { return m_dynamic; }

private:
  D3D11::Texture m_texture;
  HostDisplayPixelFormat m_format;
  bool m_dynamic;
};

LibretroD3D11HostDisplay::LibretroD3D11HostDisplay() = default;

LibretroD3D11HostDisplay::~LibretroD3D11HostDisplay()
{
  AssertMsg(!m_context, "Context should have been destroyed by now");
}

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

bool LibretroD3D11HostDisplay::HasRenderDevice() const
{
  return static_cast<bool>(m_device);
}

bool LibretroD3D11HostDisplay::HasRenderSurface() const
{
  return true;
}

static constexpr std::array<DXGI_FORMAT, static_cast<u32>(HostDisplayPixelFormat::Count)>
  s_display_pixel_format_mapping = {{DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM,
                                     DXGI_FORMAT_B5G6R5_UNORM, DXGI_FORMAT_B5G5R5A1_UNORM}};

std::unique_ptr<HostDisplayTexture> LibretroD3D11HostDisplay::CreateTexture(u32 width, u32 height, u32 layers,
                                                                            u32 levels, u32 samples,
                                                                            HostDisplayPixelFormat format,
                                                                            const void* data, u32 data_stride,
                                                                            bool dynamic /* = false */)
{
  if (layers != 1)
    return {};

  D3D11::Texture tex;
  if (!tex.Create(m_device.Get(), width, height, levels, samples,
                  s_display_pixel_format_mapping[static_cast<u32>(format)], D3D11_BIND_SHADER_RESOURCE, data,
                  data_stride, dynamic))
  {
    return {};
  }

  return std::make_unique<LibretroD3D11HostDisplayTexture>(std::move(tex), format, dynamic);
}

void LibretroD3D11HostDisplay::UpdateTexture(HostDisplayTexture* texture, u32 x, u32 y, u32 width, u32 height,
                                             const void* texture_data, u32 texture_data_stride)
{
  LibretroD3D11HostDisplayTexture* d3d11_texture = static_cast<LibretroD3D11HostDisplayTexture*>(texture);
  if (!d3d11_texture->IsDynamic())
  {
    const CD3D11_BOX dst_box(x, y, 0, x + width, y + height, 1);
    m_context->UpdateSubresource(d3d11_texture->GetD3DTexture(), 0, &dst_box, texture_data, texture_data_stride,
                                 texture_data_stride * height);
  }
  else
  {
    D3D11_MAPPED_SUBRESOURCE sr;
    HRESULT hr = m_context->Map(d3d11_texture->GetD3DTexture(), 0, D3D11_MAP_WRITE_DISCARD, 0, &sr);
    if (FAILED(hr))
      Panic("Failed to map dynamic host display texture");

    char* dst_ptr = static_cast<char*>(sr.pData) + (y * sr.RowPitch) + (x * sizeof(u32));
    const char* src_ptr = static_cast<const char*>(texture_data);
    if (sr.RowPitch == texture_data_stride)
    {
      std::memcpy(dst_ptr, src_ptr, texture_data_stride * height);
    }
    else
    {
      for (u32 row = 0; row < height; row++)
      {
        std::memcpy(dst_ptr, src_ptr, width * sizeof(u32));
        src_ptr += texture_data_stride;
        dst_ptr += sr.RowPitch;
      }
    }

    m_context->Unmap(d3d11_texture->GetD3DTexture(), 0);
  }
}

bool LibretroD3D11HostDisplay::DownloadTexture(const void* texture_handle, HostDisplayPixelFormat texture_format, u32 x,
                                               u32 y, u32 width, u32 height, void* out_data, u32 out_data_stride)
{
  ID3D11ShaderResourceView* srv =
    const_cast<ID3D11ShaderResourceView*>(static_cast<const ID3D11ShaderResourceView*>(texture_handle));
  ComPtr<ID3D11Resource> srv_resource;
  D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
  srv->GetResource(srv_resource.GetAddressOf());
  srv->GetDesc(&srv_desc);

  if (!m_readback_staging_texture.EnsureSize(m_context.Get(), width, height, srv_desc.Format, false))
    return false;

  m_readback_staging_texture.CopyFromTexture(m_context.Get(), srv_resource.Get(), 0, x, y, 0, 0, width, height);

  if (srv_desc.Format == DXGI_FORMAT_B5G6R5_UNORM || srv_desc.Format == DXGI_FORMAT_B5G5R5A1_UNORM)
  {
    return m_readback_staging_texture.ReadPixels<u16>(m_context.Get(), 0, 0, width, height, out_data_stride,
                                                      static_cast<u16*>(out_data));
  }
  else
  {
    return m_readback_staging_texture.ReadPixels<u32>(m_context.Get(), 0, 0, width, height, out_data_stride,
                                                      static_cast<u32*>(out_data));
  }
}

bool LibretroD3D11HostDisplay::SupportsDisplayPixelFormat(HostDisplayPixelFormat format) const
{
  const DXGI_FORMAT dfmt = s_display_pixel_format_mapping[static_cast<u32>(format)];
  if (dfmt == DXGI_FORMAT_UNKNOWN)
    return false;

  UINT support = 0;
  const UINT required = D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_SHADER_SAMPLE;
  return (SUCCEEDED(m_device->CheckFormatSupport(dfmt, &support)) && ((support & required) == required));
}

bool LibretroD3D11HostDisplay::BeginSetDisplayPixels(HostDisplayPixelFormat format, u32 width, u32 height,
                                                     void** out_buffer, u32* out_pitch)
{
  ClearDisplayTexture();

  const DXGI_FORMAT dxgi_format = s_display_pixel_format_mapping[static_cast<u32>(format)];
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
                    m_display_pixels_texture.GetHeight(), 0, 0, static_cast<u32>(width), static_cast<u32>(height));
  return true;
}

void LibretroD3D11HostDisplay::EndSetDisplayPixels()
{
  m_context->Unmap(m_display_pixels_texture.GetD3DTexture(), 0);
}

void LibretroD3D11HostDisplay::SetVSync(bool enabled)
{
  // The libretro frontend controls this.
  Log_DevPrintf("Ignoring SetVSync(%u)", BoolToUInt32(enabled));
}

bool LibretroD3D11HostDisplay::RequestHardwareRendererContext(retro_hw_render_callback* cb)
{
  cb->cache_context = false;
  cb->bottom_left_origin = false;
  cb->context_type = RETRO_HW_CONTEXT_DIRECT3D;
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
  if (!CreateResources())
    return false;

  return true;
}

void LibretroD3D11HostDisplay::DestroyRenderDevice()
{
  ClearSoftwareCursor();
  DestroyResources();
  DestroyRenderSurface();
  m_context.Reset();
  m_device.Reset();
}

bool LibretroD3D11HostDisplay::MakeRenderContextCurrent()
{
  return true;
}

bool LibretroD3D11HostDisplay::DoneRenderContextCurrent()
{
  return true;
}

void LibretroD3D11HostDisplay::ResizeRenderWindow(s32 new_window_width, s32 new_window_height)
{
  m_window_info.surface_width = static_cast<u32>(new_window_width);
  m_window_info.surface_height = static_cast<u32>(new_window_height);
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

void LibretroD3D11HostDisplay::DestroyRenderSurface() {}

bool LibretroD3D11HostDisplay::SetPostProcessingChain(const std::string_view& config)
{
  return false;
}

bool LibretroD3D11HostDisplay::SupportsFullscreen() const
{
  return false;
}

bool LibretroD3D11HostDisplay::IsFullscreen()
{
  return false;
}

bool LibretroD3D11HostDisplay::SetFullscreen(bool fullscreen, u32 width, u32 height, float refresh_rate)
{
  return false;
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

bool LibretroD3D11HostDisplay::CreateImGuiContext()
{
  return false;
}

void LibretroD3D11HostDisplay::DestroyImGuiContext() {}

bool LibretroD3D11HostDisplay::UpdateImGuiFontTexture()
{
  return false;
}

bool LibretroD3D11HostDisplay::Render()
{
  const u32 resolution_scale = g_libretro_host_interface.GetResolutionScale();
  const u32 display_width = static_cast<u32>(m_display_width) * resolution_scale;
  const u32 display_height = static_cast<u32>(m_display_height) * resolution_scale;
  if (!CheckFramebufferSize(display_width, display_height))
    return false;

  // Ensure we're not currently bound.
  ID3D11ShaderResourceView* null_srv = nullptr;
  m_context->PSSetShaderResources(0, 1, &null_srv);
  m_context->OMSetRenderTargets(1u, m_framebuffer.GetD3DRTVArray(), nullptr);

  if (HasDisplayTexture())
  {
    const auto [left, top, width, height] = CalculateDrawRect(display_width, display_height, 0, false);
    RenderDisplay(left, top, width, height, m_display_texture_handle, m_display_texture_width, m_display_texture_height,
                  m_display_texture_view_x, m_display_texture_view_y, m_display_texture_view_width,
                  m_display_texture_view_height, m_display_linear_filtering);
  }

  // NOTE: libretro frontend expects the data bound to PS SRV slot 0.
  m_context->OMSetRenderTargets(0, nullptr, nullptr);
  m_context->PSSetShaderResources(0, 1, m_framebuffer.GetD3DSRVArray());
  g_retro_video_refresh_callback(RETRO_HW_FRAME_BUFFER_VALID, display_width, display_height, 0);
  return true;
}

bool LibretroD3D11HostDisplay::RenderScreenshot(u32 width, u32 height, std::vector<u32>* out_pixels, u32* out_stride,
                                                HostDisplayPixelFormat* out_format)
{
  return false;
}

void LibretroD3D11HostDisplay::RenderDisplay(s32 left, s32 top, s32 width, s32 height, void* texture_handle,
                                             u32 texture_width, s32 texture_height, s32 texture_view_x,
                                             s32 texture_view_y, s32 texture_view_width, s32 texture_view_height,
                                             bool linear_filter)
{
  m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  m_context->VSSetShader(m_display_vertex_shader.Get(), nullptr, 0);
  m_context->PSSetShader(m_display_pixel_shader.Get(), nullptr, 0);
  m_context->PSSetShaderResources(0, 1, reinterpret_cast<ID3D11ShaderResourceView**>(&texture_handle));
  m_context->PSSetSamplers(0, 1, linear_filter ? m_linear_sampler.GetAddressOf() : m_point_sampler.GetAddressOf());

  const float position_adjust = m_display_linear_filtering ? 0.5f : 0.0f;
  const float size_adjust = m_display_linear_filtering ? 1.0f : 0.0f;
  const float uniforms[4] = {
    (static_cast<float>(texture_view_x) + position_adjust) / static_cast<float>(texture_width),
    (static_cast<float>(texture_view_y) + position_adjust) / static_cast<float>(texture_height),
    (static_cast<float>(texture_view_width) - size_adjust) / static_cast<float>(texture_width),
    (static_cast<float>(texture_view_height) - size_adjust) / static_cast<float>(texture_height)};
  const auto map = m_display_uniform_buffer.Map(m_context.Get(), m_display_uniform_buffer.GetSize(), sizeof(uniforms));
  std::memcpy(map.pointer, uniforms, sizeof(uniforms));
  m_display_uniform_buffer.Unmap(m_context.Get(), sizeof(uniforms));
  m_context->VSSetConstantBuffers(0, 1, m_display_uniform_buffer.GetD3DBufferArray());

  const CD3D11_VIEWPORT vp(static_cast<float>(left), static_cast<float>(top), static_cast<float>(width),
                           static_cast<float>(height));
  m_context->RSSetViewports(1, &vp);
  m_context->RSSetState(m_display_rasterizer_state.Get());
  m_context->OMSetDepthStencilState(m_display_depth_stencil_state.Get(), 0);
  m_context->OMSetBlendState(m_display_blend_state.Get(), nullptr, 0xFFFFFFFFu);

  m_context->Draw(3, 0);
}

HostDisplay::AdapterAndModeList LibretroD3D11HostDisplay::GetAdapterAndModeList()
{
  return {};
}

bool LibretroD3D11HostDisplay::CheckFramebufferSize(u32 width, u32 height)
{
  if (m_framebuffer.GetWidth() == width && m_framebuffer.GetHeight() == height)
    return true;

  return m_framebuffer.Create(m_device.Get(), width, height, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
                              D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET);
}
