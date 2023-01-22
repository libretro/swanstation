#include "libretro_vulkan_host_display.h"
#include "common/log.h"
#include "common/vulkan/builders.h"
#include "common/vulkan/context.h"
#include "common/vulkan/shader_cache.h"
#include "common/vulkan/staging_texture.h"
#include "common/vulkan/util.h"
#include "core/shader_cache_version.h"
#include "libretro_host_interface.h"
#include "vulkan_loader.h"
Log_SetChannel(LibretroVulkanHostDisplay);

class LibretroVulkanHostDisplayTexture : public HostDisplayTexture
{
public:
  LibretroVulkanHostDisplayTexture(Vulkan::Texture texture, Vulkan::StagingTexture staging_texture,
                                   HostDisplayPixelFormat format)
    : m_texture(std::move(texture)), m_staging_texture(std::move(staging_texture)), m_format(format)
  {
  }
  ~LibretroVulkanHostDisplayTexture() override = default;

  void* GetHandle() const override { return const_cast<Vulkan::Texture*>(&m_texture); }
  u32 GetWidth() const override { return m_texture.GetWidth(); }
  u32 GetHeight() const override { return m_texture.GetHeight(); }
  u32 GetLayers() const override { return m_texture.GetLayers(); }
  u32 GetLevels() const override { return m_texture.GetLevels(); }
  u32 GetSamples() const override { return m_texture.GetSamples(); }
  HostDisplayPixelFormat GetFormat() const override { return m_format; }

  const Vulkan::Texture& GetTexture() const { return m_texture; }
  Vulkan::Texture& GetTexture() { return m_texture; }
  Vulkan::StagingTexture& GetStagingTexture() { return m_staging_texture; }

private:
  Vulkan::Texture m_texture;
  Vulkan::StagingTexture m_staging_texture;
  HostDisplayPixelFormat m_format;
};

LibretroVulkanHostDisplay::LibretroVulkanHostDisplay() = default;

LibretroVulkanHostDisplay::~LibretroVulkanHostDisplay() = default;

HostDisplay::RenderAPI LibretroVulkanHostDisplay::GetRenderAPI() const
{
  return HostDisplay::RenderAPI::Vulkan;
}

void* LibretroVulkanHostDisplay::GetRenderDevice() const
{
  return nullptr;
}

void* LibretroVulkanHostDisplay::GetRenderContext() const
{
  return nullptr;
}

static constexpr std::array<VkFormat, static_cast<u32>(HostDisplayPixelFormat::Count)> s_display_pixel_format_mapping =
  {{VK_FORMAT_UNDEFINED, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R5G6B5_UNORM_PACK16,
    VK_FORMAT_A1R5G5B5_UNORM_PACK16}};

std::unique_ptr<HostDisplayTexture> LibretroVulkanHostDisplay::CreateTexture(u32 width, u32 height, u32 layers,
                                                                             u32 levels, u32 samples,
                                                                             HostDisplayPixelFormat format,
                                                                             const void* data, u32 data_stride,
                                                                             bool dynamic /* = false */)
{
  const VkFormat vk_format = s_display_pixel_format_mapping[static_cast<u32>(format)];
  if (vk_format == VK_FORMAT_UNDEFINED)
    return {};

  static constexpr VkImageUsageFlags usage =
    VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

  Vulkan::Texture texture;
  if (!texture.Create(width, height, levels, layers, vk_format, static_cast<VkSampleCountFlagBits>(samples),
                      (layers > 1) ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
                      usage))
  {
    return {};
  }

  Vulkan::StagingTexture staging_texture;
  if (data || dynamic)
  {
    if (!staging_texture.Create(dynamic ? Vulkan::StagingBuffer::Type::Mutable : Vulkan::StagingBuffer::Type::Upload,
                                vk_format, width, height))
    {
      return {};
    }
  }

  texture.TransitionToLayout(g_vulkan_context->GetCurrentCommandBuffer(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

  if (data)
  {
    staging_texture.WriteTexels(0, 0, width, height, data, data_stride);
    staging_texture.CopyToTexture(g_vulkan_context->GetCurrentCommandBuffer(), 0, 0, texture, 0, 0, 0, 0, width,
                                  height);
  }
  else
  {
    // clear it instead so we don't read uninitialized data (and keep the validation layer happy!)
    static constexpr VkClearColorValue ccv = {};
    static constexpr VkImageSubresourceRange isr = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u};
    vkCmdClearColorImage(g_vulkan_context->GetCurrentCommandBuffer(), texture.GetImage(), texture.GetLayout(), &ccv, 1u,
                         &isr);
  }

  texture.TransitionToLayout(g_vulkan_context->GetCurrentCommandBuffer(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

  // don't need to keep the staging texture around if we're not dynamic
  if (!dynamic)
    staging_texture.Destroy(true);

  return std::make_unique<LibretroVulkanHostDisplayTexture>(std::move(texture), std::move(staging_texture), format);
}

bool LibretroVulkanHostDisplay::SupportsDisplayPixelFormat(HostDisplayPixelFormat format) const
{
  const VkFormat vk_format = s_display_pixel_format_mapping[static_cast<u32>(format)];
  if (vk_format == VK_FORMAT_UNDEFINED)
    return false;

  VkFormatProperties fp = {};
  vkGetPhysicalDeviceFormatProperties(g_vulkan_context->GetPhysicalDevice(), vk_format, &fp);

  const VkFormatFeatureFlags required = (VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT);
  return ((fp.optimalTilingFeatures & required) == required);
}

bool LibretroVulkanHostDisplay::BeginSetDisplayPixels(HostDisplayPixelFormat format, u32 width, u32 height,
                                                      void** out_buffer, u32* out_pitch)
{
  const VkFormat vk_format = s_display_pixel_format_mapping[static_cast<u32>(format)];

  if (m_display_pixels_texture.GetWidth() < width || m_display_pixels_texture.GetHeight() < height ||
      m_display_pixels_texture.GetFormat() != vk_format)
  {
    if (!m_display_pixels_texture.Create(width, height, 1, 1, vk_format, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_VIEW_TYPE_2D,
                                         VK_IMAGE_TILING_OPTIMAL,
                                         VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT))
    {
      return false;
    }
  }

  if ((m_upload_staging_texture.GetWidth() < width || m_upload_staging_texture.GetHeight() < height) &&
      !m_upload_staging_texture.Create(Vulkan::StagingBuffer::Type::Upload, vk_format, width, height))
  {
    return false;
  }

  SetDisplayTexture(&m_display_pixels_texture, format, m_display_pixels_texture.GetWidth(),
                    m_display_pixels_texture.GetHeight(), 0, 0, width, height);

  *out_buffer = m_upload_staging_texture.GetMappedPointer();
  *out_pitch = m_upload_staging_texture.GetMappedStride();
  return true;
}

void LibretroVulkanHostDisplay::EndSetDisplayPixels()
{
  m_upload_staging_texture.CopyToTexture(0, 0, m_display_pixels_texture, 0, 0, 0, 0,
                                         static_cast<u32>(m_display_texture_view_width),
                                         static_cast<u32>(m_display_texture_view_height));
}

static bool RetroCreateVulkanDevice(struct retro_vulkan_context* context, VkInstance instance, VkPhysicalDevice gpu,
                                    VkSurfaceKHR surface, PFN_vkGetInstanceProcAddr get_instance_proc_addr,
                                    const char** required_device_extensions, unsigned num_required_device_extensions,
                                    const char** required_device_layers, unsigned num_required_device_layers,
                                    const VkPhysicalDeviceFeatures* required_features)
{
  // We need some module functions.
  vkGetInstanceProcAddr = get_instance_proc_addr;
  if (!Vulkan::LoadVulkanInstanceFunctions(instance))
  {
    Log_ErrorPrintf("Failed to load Vulkan instance functions");
    Vulkan::ResetVulkanLibraryFunctionPointers();
    return false;
  }

  if (gpu == VK_NULL_HANDLE)
  {
    Vulkan::Context::GPUList gpus = Vulkan::Context::EnumerateGPUs(instance);
    if (gpus.empty())
    {
      g_libretro_host_interface.ReportError("No GPU provided and none available, cannot create device");
      Vulkan::ResetVulkanLibraryFunctionPointers();
      return false;
    }

    Log_InfoPrintf("No GPU provided, using first/default");
    gpu = gpus[0];
  }

  if (!Vulkan::Context::CreateFromExistingInstance(
        instance, gpu, surface, false, false, false, required_device_extensions, num_required_device_extensions,
        required_device_layers, num_required_device_layers, required_features))
  {
    Vulkan::ResetVulkanLibraryFunctionPointers();
    return false;
  }

  context->gpu = g_vulkan_context->GetPhysicalDevice();
  context->device = g_vulkan_context->GetDevice();
  context->queue = g_vulkan_context->GetGraphicsQueue();
  context->queue_family_index = g_vulkan_context->GetGraphicsQueueFamilyIndex();
  context->presentation_queue = g_vulkan_context->GetPresentQueue();
  context->presentation_queue_family_index = g_vulkan_context->GetPresentQueueFamilyIndex();
  return true;
}

static retro_hw_render_context_negotiation_interface_vulkan s_vulkan_context_negotiation_interface = {
  RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN,         // interface_type
  RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN_VERSION, // interface_version
  nullptr,                                                      // get_application_info
  RetroCreateVulkanDevice,                                      // create_device
  nullptr                                                       // destroy_device
};

bool LibretroVulkanHostDisplay::RequestHardwareRendererContext(retro_hw_render_callback* cb)
{
  cb->cache_context = false;
  cb->bottom_left_origin = false;
  cb->context_type = RETRO_HW_CONTEXT_VULKAN;
  return g_retro_environment_callback(RETRO_ENVIRONMENT_SET_HW_RENDER, cb) &&
         g_retro_environment_callback(RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE,
                                      &s_vulkan_context_negotiation_interface);
}

bool LibretroVulkanHostDisplay::CreateRenderDevice(const WindowInfo& wi, std::string_view adapter_name,
                                                   bool debug_device, bool threaded_presentation)
{
  retro_hw_render_interface* ri = nullptr;
  if (!g_retro_environment_callback(RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE, &ri))
  {
    Log_ErrorPrint("Failed to get HW render interface");
    return false;
  }
  else if (ri->interface_type != RETRO_HW_RENDER_INTERFACE_VULKAN ||
           ri->interface_version != RETRO_HW_RENDER_INTERFACE_VULKAN_VERSION)
  {
    Log_ErrorPrintf("Unexpected HW interface - type %u version %u", static_cast<unsigned>(ri->interface_type),
                    static_cast<unsigned>(ri->interface_version));
    return false;
  }

  if (!g_vulkan_context)
  {
    Log_ErrorPrintf("Vulkan context was not negotiated/created");
    return false;
  }

  // TODO: Grab queue? it should be the same
  m_ri = reinterpret_cast<retro_hw_render_interface_vulkan*>(ri);
  return true;
}

bool LibretroVulkanHostDisplay::InitializeRenderDevice(std::string_view shader_cache_directory, bool debug_device,
                                                       bool threaded_presentation)
{
  Vulkan::ShaderCache::Create(shader_cache_directory, SHADER_CACHE_VERSION, debug_device);

  if (!CreateResources())
    return false;

  return true;
}

void LibretroVulkanHostDisplay::DestroyRenderDevice()
{
  if (!g_vulkan_context)
    return;

  g_vulkan_context->WaitForGPUIdle();

  ClearSoftwareCursor();
  DestroyResources();

  Vulkan::ShaderCache::Destroy();
  Vulkan::Context::Destroy();
  Vulkan::ResetVulkanLibraryFunctionPointers();
}

bool LibretroVulkanHostDisplay::CreateResources()
{
  static constexpr char fullscreen_quad_vertex_shader[] = R"(
#version 450 core

layout(push_constant) uniform PushConstants {
  uniform vec4 u_src_rect;
};

layout(location = 0) out vec2 v_tex0;

void main()
{
  vec2 pos = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
  v_tex0 = u_src_rect.xy + pos * u_src_rect.zw;
  gl_Position = vec4(pos * vec2(2.0f, -2.0f) + vec2(-1.0f, 1.0f), 0.0f, 1.0f);
  gl_Position.y = -gl_Position.y;
}
)";

  static constexpr char display_fragment_shader_src[] = R"(
#version 450 core

layout(set = 0, binding = 0) uniform sampler2D samp0;

layout(location = 0) in vec2 v_tex0;
layout(location = 0) out vec4 o_col0;

void main()
{
  o_col0 = vec4(texture(samp0, v_tex0).rgb, 1.0);
}
)";

  static constexpr char cursor_fragment_shader_src[] = R"(
#version 450 core

layout(set = 0, binding = 0) uniform sampler2D samp0;

layout(location = 0) in vec2 v_tex0;
layout(location = 0) out vec4 o_col0;

void main()
{
  o_col0 = texture(samp0, v_tex0);
}
)";

  VkDevice device = g_vulkan_context->GetDevice();
  VkPipelineCache pipeline_cache = g_vulkan_shader_cache->GetPipelineCache();

  m_frame_render_pass = g_vulkan_context->GetRenderPass(FRAMEBUFFER_FORMAT, VK_FORMAT_UNDEFINED, VK_SAMPLE_COUNT_1_BIT,
                                                        VK_ATTACHMENT_LOAD_OP_CLEAR);
  if (m_frame_render_pass == VK_NULL_HANDLE)
    return false;

  Vulkan::DescriptorSetLayoutBuilder dslbuilder;
  dslbuilder.AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
  m_descriptor_set_layout = dslbuilder.Create(device);
  if (m_descriptor_set_layout == VK_NULL_HANDLE)
    return false;

  Vulkan::PipelineLayoutBuilder plbuilder;
  plbuilder.AddDescriptorSet(m_descriptor_set_layout);
  plbuilder.AddPushConstants(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants));
  m_pipeline_layout = plbuilder.Create(device);
  if (m_pipeline_layout == VK_NULL_HANDLE)
    return false;

  VkShaderModule vertex_shader = g_vulkan_shader_cache->GetVertexShader(fullscreen_quad_vertex_shader);
  if (vertex_shader == VK_NULL_HANDLE)
    return false;

  VkShaderModule display_fragment_shader = g_vulkan_shader_cache->GetFragmentShader(display_fragment_shader_src);
  VkShaderModule cursor_fragment_shader = g_vulkan_shader_cache->GetFragmentShader(cursor_fragment_shader_src);
  if (display_fragment_shader == VK_NULL_HANDLE || cursor_fragment_shader == VK_NULL_HANDLE)
    return false;

  Vulkan::GraphicsPipelineBuilder gpbuilder;
  gpbuilder.SetVertexShader(vertex_shader);
  gpbuilder.SetFragmentShader(display_fragment_shader);
  gpbuilder.SetPrimitiveTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
  gpbuilder.SetNoCullRasterizationState();
  gpbuilder.SetNoDepthTestState();
  gpbuilder.SetNoBlendingState();
  gpbuilder.SetDynamicViewportAndScissorState();
  gpbuilder.SetPipelineLayout(m_pipeline_layout);
  gpbuilder.SetRenderPass(m_frame_render_pass, 0);

  m_display_pipeline = gpbuilder.Create(device, pipeline_cache, false);
  if (m_display_pipeline == VK_NULL_HANDLE)
    return false;

  gpbuilder.SetFragmentShader(cursor_fragment_shader);
  gpbuilder.SetBlendAttachment(0, true, VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD,
                               VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD);
  m_cursor_pipeline = gpbuilder.Create(device, pipeline_cache, false);
  if (m_cursor_pipeline == VK_NULL_HANDLE)
    return false;

  // don't need these anymore
  vkDestroyShaderModule(device, vertex_shader, nullptr);
  vkDestroyShaderModule(device, display_fragment_shader, nullptr);
  vkDestroyShaderModule(device, cursor_fragment_shader, nullptr);

  Vulkan::SamplerBuilder sbuilder;
  sbuilder.SetPointSampler(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER);
  m_point_sampler = sbuilder.Create(device, true);
  if (m_point_sampler == VK_NULL_HANDLE)
    return false;

  sbuilder.SetLinearSampler(false, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER);
  m_linear_sampler = sbuilder.Create(device);
  if (m_linear_sampler == VK_NULL_HANDLE)
    return false;

  return true;
}

void LibretroVulkanHostDisplay::DestroyResources()
{
  Vulkan::Util::SafeDestroyFramebuffer(m_frame_framebuffer);
  m_frame_texture.Destroy();

  m_display_pixels_texture.Destroy(false);
  m_readback_staging_texture.Destroy(false);
  m_upload_staging_texture.Destroy(false);

  Vulkan::Util::SafeDestroyPipeline(m_display_pipeline);
  Vulkan::Util::SafeDestroyPipeline(m_cursor_pipeline);
  Vulkan::Util::SafeDestroyPipelineLayout(m_pipeline_layout);
  Vulkan::Util::SafeDestroyDescriptorSetLayout(m_descriptor_set_layout);
  Vulkan::Util::SafeDestroySampler(m_point_sampler);
  Vulkan::Util::SafeDestroySampler(m_linear_sampler);

  m_frame_render_pass = VK_NULL_HANDLE;

  Vulkan::ShaderCompiler::DeinitializeGlslang();
}

void LibretroVulkanHostDisplay::RenderSoftwareCursor() {}

void LibretroVulkanHostDisplay::RenderSoftwareCursor(s32 left, s32 top, s32 width, s32 height, HostDisplayTexture* texture)
{
  VkCommandBuffer cmdbuffer = g_vulkan_context->GetCurrentCommandBuffer();
  //const Vulkan::Util::DebugScope debugScope(cmdbuffer, "VulkanHostDisplay::RenderSoftwareCursor: {%u,%u} %ux%u", left,
                                            //top, width, height);

  VkDescriptorSet ds = g_vulkan_context->AllocateDescriptorSet(m_descriptor_set_layout);
  if (ds == VK_NULL_HANDLE)
  {
    Log_ErrorPrintf("Skipping rendering software cursor because of no descriptor set");
    return;
  }

  {
    Vulkan::DescriptorSetUpdateBuilder dsupdate;
    dsupdate.AddCombinedImageSamplerDescriptorWrite(
      ds, 0, static_cast<LibretroVulkanHostDisplayTexture*>(texture)->GetTexture().GetView(), m_linear_sampler);
    dsupdate.Update(g_vulkan_context->GetDevice());
  }

  const PushConstants pc{0.0f, 0.0f, 1.0f, 1.0f};
  vkCmdBindPipeline(cmdbuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_cursor_pipeline);
  vkCmdPushConstants(cmdbuffer, m_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
  vkCmdBindDescriptorSets(cmdbuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline_layout, 0, 1, &ds, 0, nullptr);
  Vulkan::Util::SetViewportAndScissor(cmdbuffer, left, top, width, height);
  vkCmdDraw(cmdbuffer, 3, 1, 0, 0);
}

void LibretroVulkanHostDisplay::ResizeRenderWindow(s32 new_window_width, s32 new_window_height)
{
  m_window_info.surface_width = static_cast<u32>(new_window_width);
  m_window_info.surface_height = static_cast<u32>(new_window_height);
}

bool LibretroVulkanHostDisplay::ChangeRenderWindow(const WindowInfo& new_wi)
{
  // re-query hardware render interface - in vulkan, things get recreated without us being notified
  retro_hw_render_interface* ri = nullptr;
  if (!g_retro_environment_callback(RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE, &ri))
  {
    Log_ErrorPrint("Failed to get HW render interface");
    return false;
  }
  else if (ri->interface_type != RETRO_HW_RENDER_INTERFACE_VULKAN ||
           ri->interface_version != RETRO_HW_RENDER_INTERFACE_VULKAN_VERSION)
  {
    Log_ErrorPrintf("Unexpected HW interface - type %u version %u", static_cast<unsigned>(ri->interface_type),
                    static_cast<unsigned>(ri->interface_version));
    return false;
  }

  retro_hw_render_interface_vulkan* vri = reinterpret_cast<retro_hw_render_interface_vulkan*>(ri);
  if (vri != m_ri)
  {
    Log_WarningPrintf("HW render interface pointer changed without us being notified, this might cause issues?");
    m_ri = vri;
  }

  return true;
}

bool LibretroVulkanHostDisplay::Render()
{
  const u32 resolution_scale = g_libretro_host_interface.GetResolutionScale();
  const u32 display_width = static_cast<u32>(m_display_width) * resolution_scale;
  const u32 display_height = static_cast<u32>(m_display_height) * resolution_scale;
  const int16_t gun_x = g_retro_input_state_callback(0, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_SCREEN_X);
  const int16_t gun_y = g_retro_input_state_callback(0, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_SCREEN_Y);
  const s32 pos_x = (g_retro_input_state_callback(0, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_IS_OFFSCREEN) ? 0 : (((static_cast<s32>(gun_x) + 0x7FFF) * display_width) / 0xFFFF));
  const s32 pos_y = (g_retro_input_state_callback(0, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_IS_OFFSCREEN) ? 0 : (((static_cast<s32>(gun_y) + 0x7FFF) * display_height) / 0xFFFF));
  if (display_width == 0 || display_height == 0 || !CheckFramebufferSize(display_width, display_height))
    return false;

  VkCommandBuffer cmdbuffer = g_vulkan_context->GetCurrentCommandBuffer();
  m_frame_texture.OverrideImageLayout(m_frame_view.image_layout);
  m_frame_texture.TransitionToLayout(cmdbuffer, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

  const VkClearValue clear_value = {};
  const VkRenderPassBeginInfo rp = {
    VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,  nullptr, m_frame_render_pass, m_frame_framebuffer,
    {{0, 0}, {display_width, display_height}}, 1u,      &clear_value};
  vkCmdBeginRenderPass(cmdbuffer, &rp, VK_SUBPASS_CONTENTS_INLINE);

  if (HasDisplayTexture())
  {
    const auto [left, top, width, height] = CalculateDrawRect(display_width, display_height, 0, false);
    RenderDisplay(left, top, width, height, m_display_texture_handle, m_display_texture_width, m_display_texture_height,
                  m_display_texture_view_x, m_display_texture_view_y, m_display_texture_view_width,
                  m_display_texture_view_height, m_display_linear_filtering);
  }

  if (HasSoftwareCursor() && HasDisplayTexture())
  {
    const float width_scale = (display_width / 1120.0f);
    const float height_scale = (display_height / 960.0f);
    const u32 cursor_extents_x = static_cast<u32>(static_cast<float>(m_cursor_texture->GetWidth()) * width_scale * 0.5f);
    const u32 cursor_extents_y = static_cast<u32>(static_cast<float>(m_cursor_texture->GetHeight()) * height_scale * 0.5f);

    const s32 out_left = pos_x - cursor_extents_x;
    const s32 out_top = pos_y - cursor_extents_y;
    const s32 out_width = cursor_extents_x * 2u;
    const s32 out_height = cursor_extents_y * 2u;

    RenderSoftwareCursor(out_left, out_top, out_width, out_height, m_cursor_texture.get());
  }

  vkCmdEndRenderPass(cmdbuffer);
  m_frame_texture.TransitionToLayout(cmdbuffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  m_frame_view.image_layout = m_frame_texture.GetLayout();
  m_ri->set_image(m_ri->handle, &m_frame_view, 0, nullptr, VK_QUEUE_FAMILY_IGNORED);

  // TODO: We can't use this because it doesn't support passing fences...
  // m_ri.set_command_buffers(m_ri.handle, 1, &cmdbuffer);
  m_ri->lock_queue(m_ri->handle);
  g_vulkan_context->SubmitCommandBuffer();
  m_ri->unlock_queue(m_ri->handle);
  g_vulkan_context->MoveToNextCommandBuffer();

  g_retro_video_refresh_callback(RETRO_HW_FRAME_BUFFER_VALID, display_width, display_height, 0);
  return true;
}

void LibretroVulkanHostDisplay::RenderDisplay(s32 left, s32 top, s32 width, s32 height, void* texture_handle,
                                              u32 texture_width, s32 texture_height, s32 texture_view_x,
                                              s32 texture_view_y, s32 texture_view_width, s32 texture_view_height,
                                              bool linear_filter)
{
  VkCommandBuffer cmdbuffer = g_vulkan_context->GetCurrentCommandBuffer();

  VkDescriptorSet ds = g_vulkan_context->AllocateDescriptorSet(m_descriptor_set_layout);
  if (ds == VK_NULL_HANDLE)
    return;

  {
    const Vulkan::Texture* vktex = static_cast<Vulkan::Texture*>(texture_handle);
    Vulkan::DescriptorSetUpdateBuilder dsupdate;
    dsupdate.AddCombinedImageSamplerDescriptorWrite(
      ds, 0, vktex->GetView(), linear_filter ? m_linear_sampler : m_point_sampler, vktex->GetLayout());
    dsupdate.Update(g_vulkan_context->GetDevice());
  }

  const float position_adjust = m_display_linear_filtering ? 0.5f : 0.0f;
  const float size_adjust = m_display_linear_filtering ? 1.0f : 0.0f;
  const PushConstants pc{(static_cast<float>(texture_view_x) + position_adjust) / static_cast<float>(texture_width),
                         (static_cast<float>(texture_view_y) + position_adjust) / static_cast<float>(texture_height),
                         (static_cast<float>(texture_view_width) - size_adjust) / static_cast<float>(texture_width),
                         (static_cast<float>(texture_view_height) - size_adjust) / static_cast<float>(texture_height)};

  vkCmdBindPipeline(cmdbuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_display_pipeline);
  vkCmdPushConstants(cmdbuffer, m_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
  vkCmdBindDescriptorSets(cmdbuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline_layout, 0, 1, &ds, 0, nullptr);
  Vulkan::Util::SetViewportAndScissor(cmdbuffer, left, top, width, height);
  vkCmdDraw(cmdbuffer, 3, 1, 0, 0);
}

bool LibretroVulkanHostDisplay::CheckFramebufferSize(u32 width, u32 height)
{
  static constexpr VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                             VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  static constexpr VkImageViewType view_type = VK_IMAGE_VIEW_TYPE_2D;
  static constexpr VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;

  if (m_frame_texture.GetWidth() == width && m_frame_texture.GetHeight() == height)
    return true;

  g_vulkan_context->DeferFramebufferDestruction(m_frame_framebuffer);
  m_frame_texture.Destroy(true);

  if (!m_frame_texture.Create(width, height, 1, 1, FRAMEBUFFER_FORMAT, VK_SAMPLE_COUNT_1_BIT, view_type, tiling, usage))
    return false;

  VkCommandBuffer cmdbuf = g_vulkan_context->GetCurrentCommandBuffer();
  m_frame_texture.TransitionToLayout(cmdbuf, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

  static constexpr VkClearColorValue cc = {};
  static constexpr VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vkCmdClearColorImage(cmdbuf, m_frame_texture.GetImage(), m_frame_texture.GetLayout(), &cc, 1, &range);

  Vulkan::FramebufferBuilder fbb;
  fbb.SetRenderPass(m_frame_render_pass);
  fbb.AddAttachment(m_frame_texture.GetView());
  fbb.SetSize(width, height, 1);
  m_frame_framebuffer = fbb.Create(g_vulkan_context->GetDevice(), false);
  if (m_frame_framebuffer == VK_NULL_HANDLE)
    return false;

  m_frame_view = {};
  m_frame_view.image_view = m_frame_texture.GetView();
  m_frame_view.image_layout = m_frame_texture.GetLayout();
  m_frame_view.create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  m_frame_view.create_info.image = m_frame_texture.GetImage();
  m_frame_view.create_info.viewType = view_type;
  m_frame_view.create_info.format = FRAMEBUFFER_FORMAT;
  m_frame_view.create_info.components = {VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B,
                                         VK_COMPONENT_SWIZZLE_A};
  m_frame_view.create_info.subresourceRange = range;
  return true;
}
