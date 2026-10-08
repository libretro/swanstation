#include "gpu_hw_vulkan.h"
#include "common/log.h"
#include "common/state_wrapper.h"
#include "common/thread_priority.h"
#include "common/timer.h"
#include "common/vulkan/builders.h"
#include "common/vulkan/context.h"
#include "common/vulkan/embedded_shaders.h"
#include "common/vulkan/shader_cache.h"
#include "common/vulkan/staging_texture.h"
#include "common/vulkan/util.h"
#include "host_display.h"
#include "host_interface.h"
#include "core/host_interface.h"
#include "system.h"
#include "vulkan_loader.h"
#include <cstring>
Log_SetChannel(GPU_HW_Vulkan);

class LibretroVulkanHostDisplayTexture : public HostDisplayTexture
{
public:
  LibretroVulkanHostDisplayTexture(Vulkan::Texture texture, Vulkan::StagingTexture staging_texture)
    : m_texture(std::move(texture)), m_staging_texture(std::move(staging_texture))
  {
  }
  ~LibretroVulkanHostDisplayTexture() override = default;

  void* GetHandle() const override { return const_cast<Vulkan::Texture*>(&m_texture); }
  uint32_t GetWidth() const override { return m_texture.GetWidth(); }
  uint32_t GetHeight() const override { return m_texture.GetHeight(); }
  uint32_t GetSamples() const override { return m_texture.GetSamples(); }

  const Vulkan::Texture& GetTexture() const { return m_texture; }
  Vulkan::Texture& GetTexture() { return m_texture; }
  Vulkan::StagingTexture& GetStagingTexture() { return m_staging_texture; }

private:
  Vulkan::Texture m_texture;
  Vulkan::StagingTexture m_staging_texture;
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

static constexpr std::array<VkFormat, static_cast<uint32_t>(HostDisplayPixelFormat::Count)> s_display_pixel_format_mapping =
  {{VK_FORMAT_UNDEFINED, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R5G6B5_UNORM_PACK16,
    VK_FORMAT_A1R5G5B5_UNORM_PACK16}};

std::unique_ptr<HostDisplayTexture> LibretroVulkanHostDisplay::CreateTexture(uint32_t width, uint32_t height, uint32_t layers,
                                                                             uint32_t levels, uint32_t samples,
                                                                             HostDisplayPixelFormat format,
                                                                             const void* data, uint32_t data_stride,
                                                                             bool dynamic /* = false */)
{
  const VkFormat vk_format = s_display_pixel_format_mapping[static_cast<uint32_t>(format)];
  if (vk_format == VK_FORMAT_UNDEFINED)
    return {};

  static constexpr VkImageUsageFlags usage =
    VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

  Vulkan::Texture texture;
  if (!texture.Create(width, height, levels, layers, vk_format, static_cast<VkSampleCountFlagBits>(samples),
                      (layers > 1) ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
                      usage))
    return {};

  Vulkan::StagingTexture staging_texture;
  if (data || dynamic)
  {
    if (!staging_texture.Create(dynamic ? Vulkan::StagingBuffer::Type::Mutable : Vulkan::StagingBuffer::Type::Upload,
                                vk_format, width, height))
      return {};
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

  return std::make_unique<LibretroVulkanHostDisplayTexture>(std::move(texture), std::move(staging_texture));
}

bool LibretroVulkanHostDisplay::SupportsDisplayPixelFormat(HostDisplayPixelFormat format) const
{
  const VkFormat vk_format = s_display_pixel_format_mapping[static_cast<uint32_t>(format)];
  if (vk_format == VK_FORMAT_UNDEFINED)
    return false;

  VkFormatProperties fp = {};
  vkGetPhysicalDeviceFormatProperties(g_vulkan_context->GetPhysicalDevice(), vk_format, &fp);

  const VkFormatFeatureFlags required = (VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT);
  return ((fp.optimalTilingFeatures & required) == required);
}

bool LibretroVulkanHostDisplay::BeginSetDisplayPixels(HostDisplayPixelFormat format, uint32_t width, uint32_t height,
                                                      void** out_buffer, uint32_t* out_pitch)
{
  const VkFormat vk_format = s_display_pixel_format_mapping[static_cast<uint32_t>(format)];

  if (m_display_pixels_texture.GetWidth() < width || m_display_pixels_texture.GetHeight() < height ||
      m_display_pixels_texture.GetFormat() != vk_format)
  {
    if (!m_display_pixels_texture.Create(width, height, 1, 1, vk_format, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_VIEW_TYPE_2D,
                                         VK_IMAGE_TILING_OPTIMAL,
                                         VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT))
      return false;
  }

  if ((m_upload_staging_texture.GetWidth() < width || m_upload_staging_texture.GetHeight() < height) &&
      !m_upload_staging_texture.Create(Vulkan::StagingBuffer::Type::Upload, vk_format, width, height))
    return false;

  SetDisplayTexture(&m_display_pixels_texture, format, m_display_pixels_texture.GetWidth(),
                    m_display_pixels_texture.GetHeight(), 0, 0, width, height);

  *out_buffer = m_upload_staging_texture.GetMappedPointer();
  *out_pitch = m_upload_staging_texture.GetMappedStride();
  return true;
}

void LibretroVulkanHostDisplay::EndSetDisplayPixels()
{
  m_upload_staging_texture.CopyToTexture(0, 0, m_display_pixels_texture, 0, 0, 0, 0,
                                         static_cast<uint32_t>(m_display_texture_view_width),
                                         static_cast<uint32_t>(m_display_texture_view_height));
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
      g_host_interface_storage.ReportError("No GPU provided and none available, cannot create device");
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

static const VkApplicationInfo *get_application_info_vulkan(void)
{
	static VkApplicationInfo app_info{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
	app_info.pApplicationName   = "SwanStation";
	app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	app_info.pEngineName        = "SwanStation";
	app_info.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
	app_info.apiVersion         = VK_API_VERSION_1_0;
	return &app_info;
}

static retro_hw_render_context_negotiation_interface_vulkan s_vulkan_context_negotiation_interface = {
  RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN,         // interface_type
  RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN_VERSION, // interface_version
  get_application_info_vulkan,                                  // get_application_info
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
  Vulkan::ShaderCache::Create(shader_cache_directory, debug_device);
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
  // The presentation-stage shaders that used to live as inline R"()" string
  // literals here have moved out to data/shaders/vulkan/present_*.glsl with
  // pre-baked SPIR-V under src/common/vulkan/embedded_spirv/. The C++-side
  // pipeline layout (16-byte u_src_rect push constant, single combined
  // image sampler at set 0 / binding 0) is unchanged.

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

  VkShaderModule vertex_shader = Vulkan::EmbeddedShaders::CreateShaderModule(
    Vulkan::EmbeddedShaders::k_present_fullscreen_vs,
    Vulkan::EmbeddedShaders::k_present_fullscreen_vs_size_bytes);
  if (vertex_shader == VK_NULL_HANDLE)
    return false;

  VkShaderModule display_fragment_shader = Vulkan::EmbeddedShaders::CreateShaderModule(
    Vulkan::EmbeddedShaders::k_present_display_fs,
    Vulkan::EmbeddedShaders::k_present_display_fs_size_bytes);
  VkShaderModule cursor_fragment_shader = Vulkan::EmbeddedShaders::CreateShaderModule(
    Vulkan::EmbeddedShaders::k_present_cursor_fs,
    Vulkan::EmbeddedShaders::k_present_cursor_fs_size_bytes);
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
}

void LibretroVulkanHostDisplay::RenderSoftwareCursor(int32_t left, int32_t top, int32_t width, int32_t height, HostDisplayTexture* texture)
{
  VkCommandBuffer cmdbuffer = g_vulkan_context->GetCurrentCommandBuffer();

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

void LibretroVulkanHostDisplay::ResizeRenderWindow(int32_t new_window_width, int32_t new_window_height)
{
  m_window_info.surface_width = static_cast<uint32_t>(new_window_width);
  m_window_info.surface_height = static_cast<uint32_t>(new_window_height);
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
  const uint32_t display_width    = static_cast<uint32_t>(m_display_width) * resolution_scale;
  const uint32_t display_height   = static_cast<uint32_t>(m_display_height) * resolution_scale;
  // Lightgun state was cached at controller-update time; do NOT call
  // g_retro_input_state_callback() from the renderer - see the matching
  // comment in gpu_hw_opengl.cpp::Render().
  const int16_t  gun_x           = GetLightgunRawX();
  const int16_t  gun_y           = GetLightgunRawY();
  const bool offscreen       = IsLightgunOffscreen();
  const int32_t pos_x            = offscreen ? 0 : (((static_cast<int32_t>(gun_x) + 0x7FFF) * display_width)  / 0xFFFF);
  const int32_t pos_y            = offscreen ? 0 : (((static_cast<int32_t>(gun_y) + 0x7FFF) * display_height) / 0xFFFF);
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

void LibretroVulkanHostDisplay::RenderDisplay(int32_t left, int32_t top, int32_t width, int32_t height, void* texture_handle,
                                              uint32_t texture_width, int32_t texture_height, int32_t texture_view_x,
                                              int32_t texture_view_y, int32_t texture_view_width, int32_t texture_view_height)
{
  VkCommandBuffer cmdbuffer = g_vulkan_context->GetCurrentCommandBuffer();

  VkDescriptorSet ds = g_vulkan_context->AllocateDescriptorSet(m_descriptor_set_layout);
  if (ds == VK_NULL_HANDLE)
    return;

  {
    const Vulkan::Texture* vktex = static_cast<Vulkan::Texture*>(texture_handle);
    Vulkan::DescriptorSetUpdateBuilder dsupdate;
    dsupdate.AddCombinedImageSamplerDescriptorWrite(
      ds, 0, vktex->GetView(), m_point_sampler, vktex->GetLayout());
    dsupdate.Update(g_vulkan_context->GetDevice());
  }

  const PushConstants pc{static_cast<float>(texture_view_x) / static_cast<float>(texture_width),
                         static_cast<float>(texture_view_y) / static_cast<float>(texture_height),
                         static_cast<float>(texture_view_width) / static_cast<float>(texture_width),
                         static_cast<float>(texture_view_height) / static_cast<float>(texture_height)};

  vkCmdBindPipeline(cmdbuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_display_pipeline);
  vkCmdPushConstants(cmdbuffer, m_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
  vkCmdBindDescriptorSets(cmdbuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline_layout, 0, 1, &ds, 0, nullptr);
  Vulkan::Util::SetViewportAndScissor(cmdbuffer, left, top, width, height);
  vkCmdDraw(cmdbuffer, 3, 1, 0, 0);
}

bool LibretroVulkanHostDisplay::CheckFramebufferSize(uint32_t width, uint32_t height)
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

GPU_HW_Vulkan::GPU_HW_Vulkan() = default;

GPU_HW_Vulkan::~GPU_HW_Vulkan()
{
  if (m_host_display)
  {
    m_host_display->ClearDisplayTexture();
    ResetGraphicsAPIState();
  }

  DestroyResources();
}

bool GPU_HW_Vulkan::Initialize(HostDisplay* host_display)
{
  if (host_display->GetRenderAPI() != HostDisplay::RenderAPI::Vulkan)
  {
    Log_ErrorPrintf("Host render API is incompatible");
    return false;
  }

  SetCapabilities();

  if (!GPU_HW::Initialize(host_display))
    return false;

  if (!CreatePipelineLayouts())
  {
    Log_ErrorPrintf("Failed to create pipeline layouts");
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

  UpdateDepthBufferFromMaskBit();
  RestoreGraphicsAPIState();
  return true;
}

void GPU_HW_Vulkan::Reset(bool clear_vram)
{
  GPU_HW::Reset(clear_vram);

  EndRenderPass();

  if (clear_vram)
    ClearFramebuffer();
}

bool GPU_HW_Vulkan::DoState(StateWrapper& sw, HostDisplayTexture** host_texture, bool update_display)
{
  if (host_texture)
  {
    EndRenderPass();

    const VkImageCopy ic{{VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u},
                         {0, 0, 0},
                         {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u},
                         {0, 0, 0},
                         {m_vram_texture.GetWidth(), m_vram_texture.GetHeight(), 1u}};

    VkCommandBuffer buf = g_vulkan_context->GetCurrentCommandBuffer();

    if (sw.IsReading())
    {
      Vulkan::Texture* tex = static_cast<Vulkan::Texture*>((*host_texture)->GetHandle());
      if (tex->GetWidth() != m_vram_texture.GetWidth() || tex->GetHeight() != m_vram_texture.GetHeight() ||
          tex->GetSamples() != m_vram_texture.GetSamples())
        return false;

      const VkImageLayout old_tex_layout = tex->GetLayout();
      const VkImageLayout old_vram_layout = m_vram_texture.GetLayout();
      tex->TransitionToLayout(buf, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
      m_vram_texture.TransitionToLayout(buf, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
      vkCmdCopyImage(g_vulkan_context->GetCurrentCommandBuffer(), tex->GetImage(), tex->GetLayout(),
                     m_vram_texture.GetImage(), m_vram_texture.GetLayout(), 1, &ic);
      m_vram_texture.TransitionToLayout(buf, old_vram_layout);
      tex->TransitionToLayout(buf, old_tex_layout);
    }
    else
    {
      HostDisplayTexture* htex = *host_texture;
      if (!htex || htex->GetWidth() != m_vram_texture.GetWidth() || htex->GetHeight() != m_vram_texture.GetHeight() ||
          htex->GetSamples() != static_cast<uint32_t>(m_vram_texture.GetSamples()))
      {
        delete htex;

        htex = m_host_display
                 ->CreateTexture(m_vram_texture.GetWidth(), m_vram_texture.GetHeight(), 1, 1,
                                 m_vram_texture.GetSamples(), HostDisplayPixelFormat::RGBA8, nullptr, 0, false)
                 .release();
        *host_texture = htex;
        if (!htex)
          return false;
      }

      Vulkan::Texture* tex = static_cast<Vulkan::Texture*>(htex->GetHandle());
      if (tex->GetWidth() != m_vram_texture.GetWidth() || tex->GetHeight() != m_vram_texture.GetHeight() ||
          tex->GetSamples() != m_vram_texture.GetSamples())
        return false;

      const VkImageLayout old_vram_layout = m_vram_texture.GetLayout();
      tex->TransitionToLayout(buf, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
      m_vram_texture.TransitionToLayout(buf, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
      vkCmdCopyImage(g_vulkan_context->GetCurrentCommandBuffer(), m_vram_texture.GetImage(), m_vram_texture.GetLayout(),
                     tex->GetImage(), tex->GetLayout(), 1, &ic);
      m_vram_texture.TransitionToLayout(buf, old_vram_layout);
      tex->TransitionToLayout(buf, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
  }

  return GPU_HW::DoState(sw, host_texture, update_display);
}

void GPU_HW_Vulkan::ResetGraphicsAPIState()
{
  GPU_HW::ResetGraphicsAPIState();

  EndRenderPass();

  if (m_host_display->GetDisplayTextureHandle() == &m_vram_texture)
  {
    m_vram_texture.TransitionToLayout(g_vulkan_context->GetCurrentCommandBuffer(),
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  }

  // this is called at the end of the frame, so the UBO is associated with the previous command buffer.
  m_batch_ubo_dirty = true;
}

void GPU_HW_Vulkan::RestoreGraphicsAPIState()
{
  VkCommandBuffer cmdbuf = g_vulkan_context->GetCurrentCommandBuffer();
  m_vram_texture.TransitionToLayout(cmdbuf, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

  VkDeviceSize vertex_buffer_offset = 0;
  vkCmdBindVertexBuffers(cmdbuf, 0, 1, m_vertex_stream_buffer.GetBufferPointer(), &vertex_buffer_offset);
  Vulkan::Util::SetViewport(cmdbuf, 0, 0, m_vram_texture.GetWidth(), m_vram_texture.GetHeight());
  vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_batch_pipeline_layout, 0, 1,
                          &m_batch_descriptor_set, 1, &m_current_uniform_buffer_offset);
  SetScissorFromDrawingArea();
}

void GPU_HW_Vulkan::UpdateSettings()
{
  GPU_HW::UpdateSettings();

  // Stop the background batch-compile worker BEFORE UpdateHWSettings
  // writes m_texture_filtering. With B's filter-dimensioned batch
  // cache the worker reads m_texture_filtering implicitly on every
  // call to GetBatchPipeline / GetBatchFragmentShader (it determines
  // both the slot index AND the SPIR-V blob selection). An
  // unsynchronised write here while the worker is still iterating
  // could split those two reads across old / new filter values,
  // installing a FS module for one filter into a slot indexed by
  // another - which then persists indefinitely because the filter-
  // only path skips DestroyPipelines. Joining first eliminates the
  // race entirely; CompilePipelines below will restart the worker
  // for the new filter as appropriate. (StopShaderCompileThread is
  // idempotent - the call inside CompilePipelines at line ~1406
  // becomes a no-op for paths that pre-stop here.)
  StopShaderCompileThread();

  bool framebuffer_changed, shaders_changed, only_dim_changed, downsample_changed, display_only_source_changed;
  UpdateHWSettings(&framebuffer_changed, &shaders_changed, &only_dim_changed, &downsample_changed,
                   /*shader_source_changed=*/nullptr, &display_only_source_changed);

  if (framebuffer_changed)
  {
    RestoreGraphicsAPIState();
    ReadVRAM(0, 0, VRAM_WIDTH, VRAM_HEIGHT);
    ResetGraphicsAPIState();
  }

  // Everything should be finished executing before recreating resources.
  m_host_display->ClearDisplayTexture();
  g_vulkan_context->ExecuteCommandBuffer(true);

  if (framebuffer_changed)
    CreateFramebuffer();

  // Downsample-only resource rebuild. Triggered when downsample mode
  // changed but framebuffer_changed did NOT (i.e. Disabled <-> Box,
  // neither end Adaptive). Without this branch the only way the
  // backend could service a downsample toggle was via the full
  // ReadVRAM -> CreateFramebuffer -> UpdateVRAM round-trip - which
  // produced a multi-second freeze on every toggle even though the
  // batch matrix and main VRAM textures don't depend on downsample
  // mode at all. The work here is bounded by the downsample
  // resource and PSO count: 1 texture + 1 framebuffer + 1 PSO for
  // Box; ~4-12 textures/views + framebuffers + 4 PSOs for
  // Adaptive (which would only land here when transitioning from
  // Box - the Disabled <-> Adaptive and Box <-> Adaptive cases go
  // through framebuffer_changed above).
  //
  // Order matters relative to shaders_changed below. If
  // shaders_changed is also true and a full DestroyPipelines /
  // CompilePipelines pair runs, that pair would (a) destroy the
  // downsample PSOs anyway and (b) pre-fill them again via
  // CompilePipelines' Lazy/Enabled non-batch pre-fill block - using
  // the resources we recreate here. Doing the resource swap first
  // means the PSO pre-fill picks up the new render pass.
  if (downsample_changed && !framebuffer_changed)
  {
    DestroyDownsamplePipelines();
    DestroyDownsampleResources();
    if (m_downsample_mode != GPUDownsampleMode::Disabled)
    {
      const uint32_t texture_width = VRAM_WIDTH * m_resolution_scale;
      const uint32_t texture_height = VRAM_HEIGHT * m_resolution_scale;
      const VkFormat texture_format = VK_FORMAT_R8G8B8A8_UNORM;
      CreateDownsampleResources(texture_width, texture_height, texture_format);
    }
  }
  else if (downsample_changed)
  {
    // framebuffer_changed = true ALSO implies downsample_changed for
    // Disabled <-> Adaptive / Box <-> Adaptive. CreateFramebuffer
    // above already rebuilt the downsample resources for the new
    // mode (via DestroyFramebuffer + the CreateDownsampleResources
    // call at the bottom of CreateFramebuffer), so all that remains
    // is to drop the stale downsample PSOs - they referenced the
    // OLD render pass and would otherwise be left referencing
    // destroyed Vulkan handles. The shaders_changed path or the
    // post-shaders rebuild step below pre-fills the new PSOs.
    DestroyDownsamplePipelines();
  }

  if (shaders_changed)
  {
    if (display_only_source_changed)
    {
      // chroma_smoothing flipped and nothing else affecting shader
      // source. SMOOTH_CHROMA is constant_id=103 inside the display
      // FS (see GetDisplayPipeline). Every other Vulkan pipeline -
      // the 1164-cell m_batch_pipelines matrix, the VRAM ops PSOs
      // (vram_fill / vram_read / vram_write / vram_copy / vram_
      // update_depth / vram_readback), the downsample PSOs, the
      // batch FS modules - is invariant under chroma_smoothing
      // (none of them reach the SMOOTH_CHROMA spec const or the
      // display FS at all), so DestroyPipelines would throw away
      // ~1.2k valid pipelines just to invalidate 6. Destroy only
      // the 6-slot display pipeline cache; GetDisplayPipeline
      // lazy-faults a fresh pipeline with the new SMOOTH_CHROMA
      // spec const on the next UpdateDisplay. Mirrors the D3D12 /
      // D3D11 / OpenGL partial-clear from 57ac62e / 93e5db5 /
      // 722e98a.
      m_display_pipelines.enumerate(Vulkan::Util::SafeDestroyPipeline);

      // Relaunch the background batch warm-up worker that
      // StopShaderCompileThread joined at the top of UpdateSettings;
      // this branch doesn't go through CompilePipelines so its
      // launch site at line ~1929 isn't reached. The worker only
      // walks m_batch_pipelines (which doesn't carry SMOOTH_CHROMA
      // anywhere), so keeping it stopped after a chroma toggle
      // would just starve a mid-warmup session of further
      // background fill. Match the precompile_mode gate from
      // CompilePipelines verbatim - Lazy AND Enabled both run the
      // worker on Vulkan (Enabled's first pass over the current
      // filter sub-cube hits the lock-free fast-return path since
      // precompile_sync already filled it, then warms the other
      // six sub-cubes; that asymmetry is intentional and unrelated
      // to chroma_smoothing).
      const GPUShaderPrecompileMode precompile_mode = g_settings.gpu_shader_precompile_mode;
      if (precompile_mode == GPUShaderPrecompileMode::Lazy ||
          precompile_mode == GPUShaderPrecompileMode::Enabled)
      {
        StartShaderCompileThread();
      }
    }
    else if (only_dim_changed)
    {
      // Only cache-dimensioned settings changed (texture filter,
      // true colour, and/or scaled dithering). m_batch_pipelines
      // is dimensioned over all three; the previous tuple's sub-
      // cube is still valid and stays populated. CompilePipelines
      // just lazy-populates the new tuple's sub-cube on top: in
      // Enabled mode the precompile_sync loop walks the matrix
      // calling GetBatchPipeline with the new (m_texture_filtering,
      // m_true_color, m_scaled_dithering) tuple, so the OTHER sub-
      // cubes are untouched. In Lazy / Disabled the new sub-cube
      // stays empty until first draw / the worker reaches each
      // cell.
      //
      // Cycling back to a previously-visited tuple is instant - no
      // vkCreateGraphicsPipelines call, no destroy/recreate, just an
      // atomic load of an already-filled slot.
      CompilePipelines();
    }
    else
    {
      // Full flush: any non-dimensioned shader-affecting change
      // (resolution scale, MSAA, per-sample shading, UV limits,
      // PGXP depth, colour perspective, precompile mode)
      // invalidates every sub-cube because per-session spec
      // constants and structural SPIR-V choice apply identically
      // to all of them.
      DestroyPipelines();
      CompilePipelines();
    }
  }
  else if (downsample_changed)
  {
    // No shader change. Pre-build new downsample PSOs in Enabled /
    // Lazy modes so the next display call doesn't pay the lazy-
    // fault stutter. Disabled stays truly lazy by contract.
    const GPUShaderPrecompileMode precompile_mode = g_settings.gpu_shader_precompile_mode;
    if (precompile_mode == GPUShaderPrecompileMode::Enabled ||
        precompile_mode == GPUShaderPrecompileMode::Lazy)
    {
      if (m_downsample_mode == GPUDownsampleMode::Adaptive)
      {
        (void)GetDownsampleFirstPassPipeline();
        (void)GetDownsampleMidPassPipeline();
        (void)GetDownsampleBlurPassPipeline();
        (void)GetDownsampleCompositePassPipeline();
      }
      else if (m_downsample_mode == GPUDownsampleMode::Box)
      {
        (void)GetDownsampleFirstPassPipeline();
      }
    }
  }

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

void GPU_HW_Vulkan::MapBatchVertexPointer(uint32_t required_vertices)
{
  const uint32_t required_space = required_vertices * sizeof(BatchVertex);
  if (!m_vertex_stream_buffer.ReserveMemory(required_space, sizeof(BatchVertex)))
  {
    ExecuteCommandBuffer(false, true);
    m_vertex_stream_buffer.ReserveMemory(required_space, sizeof(BatchVertex));
  }

  m_batch_start_vertex_ptr = static_cast<BatchVertex*>(m_vertex_stream_buffer.GetCurrentHostPointer());
  m_batch_current_vertex_ptr = m_batch_start_vertex_ptr;
  m_batch_end_vertex_ptr = m_batch_start_vertex_ptr + (m_vertex_stream_buffer.GetCurrentSpace() / sizeof(BatchVertex));
  m_batch_base_vertex = m_vertex_stream_buffer.GetCurrentOffset() / sizeof(BatchVertex);
}

void GPU_HW_Vulkan::UnmapBatchVertexPointer(uint32_t used_vertices)
{
  if (used_vertices > 0)
    m_vertex_stream_buffer.CommitMemory(used_vertices * sizeof(BatchVertex));

  m_batch_start_vertex_ptr = nullptr;
  m_batch_end_vertex_ptr = nullptr;
  m_batch_current_vertex_ptr = nullptr;
}

void GPU_HW_Vulkan::UploadUniformBuffer(const void* data, uint32_t data_size)
{
  const uint32_t alignment = static_cast<uint32_t>(g_vulkan_context->GetUniformBufferAlignment());
  if (!m_uniform_stream_buffer.ReserveMemory(data_size, alignment))
  {
    ExecuteCommandBuffer(false, true);
    m_uniform_stream_buffer.ReserveMemory(data_size, alignment);
  }

  m_current_uniform_buffer_offset = m_uniform_stream_buffer.GetCurrentOffset();
  std::memcpy(m_uniform_stream_buffer.GetCurrentHostPointer(), data, data_size);
  m_uniform_stream_buffer.CommitMemory(data_size);

  vkCmdBindDescriptorSets(g_vulkan_context->GetCurrentCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                          m_batch_pipeline_layout, 0, 1, &m_batch_descriptor_set, 1, &m_current_uniform_buffer_offset);
}

void GPU_HW_Vulkan::SetCapabilities()
{
  const uint32_t max_texture_size = g_vulkan_context->GetDeviceLimits().maxImageDimension2D;
  const uint32_t max_texture_scale = max_texture_size / VRAM_WIDTH;
  Log_InfoPrintf("Max texture size: %ux%u", max_texture_size, max_texture_size);
  m_max_resolution_scale = max_texture_scale;

  VkImageFormatProperties color_properties = {};
  vkGetPhysicalDeviceImageFormatProperties(g_vulkan_context->GetPhysicalDevice(), VK_FORMAT_R8G8B8A8_UNORM,
                                           VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
                                           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 0, &color_properties);
  VkImageFormatProperties depth_properties = {};
  vkGetPhysicalDeviceImageFormatProperties(g_vulkan_context->GetPhysicalDevice(), VK_FORMAT_D32_SFLOAT,
                                           VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
                                           VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 0, &depth_properties);
  const VkSampleCountFlags combined_properties =
    g_vulkan_context->GetDeviceProperties().limits.framebufferColorSampleCounts &
    g_vulkan_context->GetDeviceProperties().limits.framebufferDepthSampleCounts & color_properties.sampleCounts &
    depth_properties.sampleCounts;
  if (combined_properties & VK_SAMPLE_COUNT_64_BIT)
    m_max_multisamples = 64;
  else if (combined_properties & VK_SAMPLE_COUNT_32_BIT)
    m_max_multisamples = 32;
  else if (combined_properties & VK_SAMPLE_COUNT_16_BIT)
    m_max_multisamples = 16;
  else if (combined_properties & VK_SAMPLE_COUNT_8_BIT)
    m_max_multisamples = 8;
  else if (combined_properties & VK_SAMPLE_COUNT_4_BIT)
    m_max_multisamples = 4;
  else if (combined_properties & VK_SAMPLE_COUNT_2_BIT)
    m_max_multisamples = 2;
  else
    m_max_multisamples = 1;

  m_supports_dual_source_blend = g_vulkan_context->GetDeviceFeatures().dualSrcBlend;
  m_supports_per_sample_shading = g_vulkan_context->GetDeviceFeatures().sampleRateShading;
  m_supports_adaptive_downsampling = true;
  m_supports_disable_color_perspective = true;

  Log_InfoPrintf("Dual-source blend: %s", m_supports_dual_source_blend ? "supported" : "not supported");
  Log_InfoPrintf("Per-sample shading: %s", m_supports_per_sample_shading ? "supported" : "not supported");
  Log_InfoPrintf("Max multisamples: %u", m_max_multisamples);

#ifdef __APPLE__
  // Partial texture buffer uploads appear to be broken in macOS/MoltenVK.
  m_use_ssbos_for_vram_writes = true;
#else
  const uint32_t max_texel_buffer_elements = g_vulkan_context->GetDeviceLimits().maxTexelBufferElements;
  Log_InfoPrintf("Max texel buffer elements: %u", max_texel_buffer_elements);
  if (max_texel_buffer_elements < (VRAM_WIDTH * VRAM_HEIGHT))
  {
    Log_WarningPrintf("Texel buffer elements insufficient, using shader storage buffers instead.");
    m_use_ssbos_for_vram_writes = true;
  }
#endif
}

void GPU_HW_Vulkan::DestroyResources()
{
  // Everything should be finished executing before recreating resources.
  if (g_vulkan_context)
    g_vulkan_context->ExecuteCommandBuffer(true);

  DestroyFramebuffer();
  DestroyPipelines();

  Vulkan::Util::SafeDestroyPipelineLayout(m_downsample_pipeline_layout);
  Vulkan::Util::SafeDestroyDescriptorSetLayout(m_downsample_composite_descriptor_set_layout);
  Vulkan::Util::SafeDestroyPipelineLayout(m_downsample_composite_pipeline_layout);

  Vulkan::Util::SafeFreeGlobalDescriptorSet(m_vram_write_descriptor_set);
  Vulkan::Util::SafeDestroyBufferView(m_texture_stream_buffer_view);

  m_vertex_stream_buffer.Destroy(false);
  m_uniform_stream_buffer.Destroy(false);
  m_texture_stream_buffer.Destroy(false);

  Vulkan::Util::SafeDestroyPipelineLayout(m_vram_write_pipeline_layout);
  Vulkan::Util::SafeDestroyPipelineLayout(m_single_sampler_pipeline_layout);
  Vulkan::Util::SafeDestroyPipelineLayout(m_no_samplers_pipeline_layout);
  Vulkan::Util::SafeDestroyPipelineLayout(m_batch_pipeline_layout);
  Vulkan::Util::SafeDestroyDescriptorSetLayout(m_vram_write_descriptor_set_layout);
  Vulkan::Util::SafeDestroyDescriptorSetLayout(m_single_sampler_descriptor_set_layout);
  Vulkan::Util::SafeDestroyDescriptorSetLayout(m_batch_descriptor_set_layout);
  Vulkan::Util::SafeDestroySampler(m_point_sampler);
  Vulkan::Util::SafeDestroySampler(m_linear_sampler);
  Vulkan::Util::SafeDestroySampler(m_trilinear_sampler);
}

void GPU_HW_Vulkan::BeginRenderPass(VkRenderPass render_pass, VkFramebuffer framebuffer, uint32_t x, uint32_t y, uint32_t width,
                                    uint32_t height, const VkClearValue* clear_value /* = nullptr */)
{
  const VkRenderPassBeginInfo bi = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                                    nullptr,
                                    render_pass,
                                    framebuffer,
                                    {{static_cast<int32_t>(x), static_cast<int32_t>(y)}, {width, height}},
                                    (clear_value ? 1u : 0u),
                                    clear_value};
  vkCmdBeginRenderPass(g_vulkan_context->GetCurrentCommandBuffer(), &bi, VK_SUBPASS_CONTENTS_INLINE);
  m_current_render_pass = render_pass;
}

void GPU_HW_Vulkan::BeginVRAMRenderPass()
{
  if (m_current_render_pass == m_vram_render_pass)
    return;

  EndRenderPass();
  BeginRenderPass(m_vram_render_pass, m_vram_framebuffer, 0, 0, m_vram_texture.GetWidth(), m_vram_texture.GetHeight());
}

void GPU_HW_Vulkan::EndRenderPass()
{
  if (m_current_render_pass == VK_NULL_HANDLE)
    return;

  vkCmdEndRenderPass(g_vulkan_context->GetCurrentCommandBuffer());
  m_current_render_pass = VK_NULL_HANDLE;
}

void GPU_HW_Vulkan::ExecuteCommandBuffer(bool wait_for_completion, bool restore_state)
{
  EndRenderPass();
  g_vulkan_context->ExecuteCommandBuffer(wait_for_completion);
  m_batch_ubo_dirty = true;
  if (restore_state)
    RestoreGraphicsAPIState();
}

bool GPU_HW_Vulkan::CreatePipelineLayouts()
{
  VkDevice device = g_vulkan_context->GetDevice();

  Vulkan::DescriptorSetLayoutBuilder dslbuilder;
  dslbuilder.AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1,
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
  dslbuilder.AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
  m_batch_descriptor_set_layout = dslbuilder.Create(device);
  if (m_batch_descriptor_set_layout == VK_NULL_HANDLE)
    return false;

  // textures start at 1
  dslbuilder.AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
  m_single_sampler_descriptor_set_layout = dslbuilder.Create(device);
  if (m_single_sampler_descriptor_set_layout == VK_NULL_HANDLE)
    return false;

  if (m_use_ssbos_for_vram_writes)
    dslbuilder.AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
  else
    dslbuilder.AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
  m_vram_write_descriptor_set_layout = dslbuilder.Create(device);
  if (m_vram_write_descriptor_set_layout == VK_NULL_HANDLE)
    return false;

  Vulkan::PipelineLayoutBuilder plbuilder;
  plbuilder.AddDescriptorSet(m_batch_descriptor_set_layout);
  m_batch_pipeline_layout = plbuilder.Create(device);
  if (m_batch_pipeline_layout == VK_NULL_HANDLE)
    return false;

  plbuilder.AddDescriptorSet(m_single_sampler_descriptor_set_layout);
  plbuilder.AddPushConstants(VK_SHADER_STAGE_FRAGMENT_BIT, 0, MAX_PUSH_CONSTANTS_SIZE);
  m_single_sampler_pipeline_layout = plbuilder.Create(device);
  if (m_single_sampler_pipeline_layout == VK_NULL_HANDLE)
    return false;

  plbuilder.AddPushConstants(VK_SHADER_STAGE_FRAGMENT_BIT, 0, MAX_PUSH_CONSTANTS_SIZE);
  m_no_samplers_pipeline_layout = plbuilder.Create(device);
  if (m_no_samplers_pipeline_layout == VK_NULL_HANDLE)
    return false;

  plbuilder.AddDescriptorSet(m_vram_write_descriptor_set_layout);
  plbuilder.AddPushConstants(VK_SHADER_STAGE_FRAGMENT_BIT, 0, MAX_PUSH_CONSTANTS_SIZE);
  m_vram_write_pipeline_layout = plbuilder.Create(device);
  if (m_vram_write_pipeline_layout == VK_NULL_HANDLE)
    return false;

  plbuilder.AddDescriptorSet(m_single_sampler_descriptor_set_layout);
  plbuilder.AddPushConstants(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, MAX_PUSH_CONSTANTS_SIZE);
  m_downsample_pipeline_layout = plbuilder.Create(device);
  if (m_downsample_pipeline_layout == VK_NULL_HANDLE)
    return false;

  dslbuilder.AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
  dslbuilder.AddBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
  m_downsample_composite_descriptor_set_layout = dslbuilder.Create(device);
  if (m_downsample_composite_descriptor_set_layout == VK_NULL_HANDLE)
    return false;
  plbuilder.AddDescriptorSet(m_downsample_composite_descriptor_set_layout);
  plbuilder.AddPushConstants(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, MAX_PUSH_CONSTANTS_SIZE);
  m_downsample_composite_pipeline_layout = plbuilder.Create(device);
  if (m_downsample_composite_pipeline_layout == VK_NULL_HANDLE)
    return false;

  return true;
}

bool GPU_HW_Vulkan::CreateSamplers()
{
  VkDevice device = g_vulkan_context->GetDevice();

  Vulkan::SamplerBuilder sbuilder;
  sbuilder.SetPointSampler(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER);
  sbuilder.SetAddressMode(VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT,
                          VK_SAMPLER_ADDRESS_MODE_REPEAT);
  m_point_sampler = sbuilder.Create(device);
  if (m_point_sampler == VK_NULL_HANDLE)
    return false;

  sbuilder.SetLinearSampler(false, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER);
  sbuilder.SetAddressMode(VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT,
                          VK_SAMPLER_ADDRESS_MODE_REPEAT);
  m_linear_sampler = sbuilder.Create(device);
  if (m_linear_sampler == VK_NULL_HANDLE)
    return false;

  sbuilder.SetLinearSampler(true, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER);
  m_trilinear_sampler = sbuilder.Create(device);
  if (m_trilinear_sampler == VK_NULL_HANDLE)
    return false;

  return true;
}

bool GPU_HW_Vulkan::CreateFramebuffer()
{
  DestroyFramebuffer();

  // scale vram size to internal resolution
  const uint32_t texture_width = VRAM_WIDTH * m_resolution_scale;
  const uint32_t texture_height = VRAM_HEIGHT * m_resolution_scale;
  const VkFormat texture_format = VK_FORMAT_R8G8B8A8_UNORM;
  /* D32_SFLOAT rather than D16_UNORM: in PGXP depth-buffer mode the value
   * written to this attachment is the reconstructed perspective W, carried at
   * full float32 through the vertex path (a_pos is R32G32B32A32_SFLOAT) - a
   * 16-bit UNORM depth quantizes exactly the precision PGXP exists to recover.
   * D32_SFLOAT is lossless for the legacy ordered-depth path too and is a
   * format the spec guarantees for depth-stencil attachment support, so no
   * capability check is needed. Made unconditional (rather than keyed on the
   * PGXP-depth setting) because a mid-session toggle recompiles shaders but
   * does not set framebuffer_changed / recreate this texture. This also brings
   * the actual depth format in line with the D32_SFLOAT sample-count query
   * already used above for max-MSAA detection. The batch/VRAM-ops pipelines
   * follow via the render passes below, which key off depth_format. */
  const VkFormat depth_format = VK_FORMAT_D32_SFLOAT;
  const VkSampleCountFlagBits samples = static_cast<VkSampleCountFlagBits>(m_multisamples);

  if (!m_vram_texture.Create(texture_width, texture_height, 1, 1, texture_format, samples, VK_IMAGE_VIEW_TYPE_2D,
                             VK_IMAGE_TILING_OPTIMAL,
                             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                               VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT) ||
      !m_vram_depth_texture.Create(texture_width, texture_height, 1, 1, depth_format, samples, VK_IMAGE_VIEW_TYPE_2D,
                                   VK_IMAGE_TILING_OPTIMAL,
                                   VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT) ||
      !m_vram_read_texture.Create(texture_width, texture_height, 1, 1, texture_format, VK_SAMPLE_COUNT_1_BIT,
                                  VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
                                  VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT) ||
      !m_display_texture.Create(
        ((m_downsample_mode == GPUDownsampleMode::Adaptive) ? VRAM_WIDTH : GPU_MAX_DISPLAY_WIDTH) * m_resolution_scale,
        GPU_MAX_DISPLAY_HEIGHT * m_resolution_scale, 1, 1, texture_format, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_VIEW_TYPE_2D,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
          VK_IMAGE_USAGE_TRANSFER_DST_BIT) ||
      !m_vram_readback_texture.Create(VRAM_WIDTH, VRAM_HEIGHT, 1, 1, texture_format, VK_SAMPLE_COUNT_1_BIT,
                                      VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
                                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT) ||
      !m_vram_readback_staging_texture.Create(Vulkan::StagingBuffer::Type::Readback, texture_format, VRAM_WIDTH / 2,
                                              VRAM_HEIGHT))
  {
    return false;
  }

  m_vram_render_pass =
    g_vulkan_context->GetRenderPass(texture_format, depth_format, samples, VK_ATTACHMENT_LOAD_OP_LOAD);
  m_vram_update_depth_render_pass =
    g_vulkan_context->GetRenderPass(VK_FORMAT_UNDEFINED, depth_format, samples, VK_ATTACHMENT_LOAD_OP_DONT_CARE);
  m_display_load_render_pass = g_vulkan_context->GetRenderPass(
    m_display_texture.GetFormat(), VK_FORMAT_UNDEFINED, m_display_texture.GetSamples(), VK_ATTACHMENT_LOAD_OP_LOAD);
  m_display_discard_render_pass =
    g_vulkan_context->GetRenderPass(m_display_texture.GetFormat(), VK_FORMAT_UNDEFINED, m_display_texture.GetSamples(),
                                    VK_ATTACHMENT_LOAD_OP_DONT_CARE);
  m_vram_readback_render_pass =
    g_vulkan_context->GetRenderPass(m_vram_readback_texture.GetFormat(), VK_FORMAT_UNDEFINED,
                                    m_vram_readback_texture.GetSamples(), VK_ATTACHMENT_LOAD_OP_DONT_CARE);

  if (m_vram_render_pass == VK_NULL_HANDLE || m_vram_update_depth_render_pass == VK_NULL_HANDLE ||
      m_display_load_render_pass == VK_NULL_HANDLE || m_vram_readback_render_pass == VK_NULL_HANDLE)
  {
    return false;
  }

  // vram framebuffer has both colour and depth
  Vulkan::FramebufferBuilder fbb;
  fbb.AddAttachment(m_vram_texture.GetView());
  fbb.AddAttachment(m_vram_depth_texture.GetView());
  fbb.SetRenderPass(m_vram_render_pass);
  fbb.SetSize(m_vram_texture.GetWidth(), m_vram_texture.GetHeight(), m_vram_texture.GetLayers());
  m_vram_framebuffer = fbb.Create(g_vulkan_context->GetDevice());
  if (m_vram_framebuffer == VK_NULL_HANDLE)
    return false;

  m_vram_update_depth_framebuffer = m_vram_depth_texture.CreateFramebuffer(m_vram_update_depth_render_pass);
  m_vram_readback_framebuffer = m_vram_readback_texture.CreateFramebuffer(m_vram_readback_render_pass);
  m_display_framebuffer = m_display_texture.CreateFramebuffer(m_display_load_render_pass);
  if (m_vram_update_depth_framebuffer == VK_NULL_HANDLE || m_vram_readback_framebuffer == VK_NULL_HANDLE ||
      m_display_framebuffer == VK_NULL_HANDLE)
  {
    return false;
  }
  VkCommandBuffer cmdbuf = g_vulkan_context->GetCurrentCommandBuffer();

  m_vram_texture.TransitionToLayout(cmdbuf, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
  m_vram_depth_texture.TransitionToLayout(cmdbuf, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
  m_vram_read_texture.TransitionToLayout(cmdbuf, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

  Vulkan::DescriptorSetUpdateBuilder dsubuilder;

  m_batch_descriptor_set = g_vulkan_context->AllocateGlobalDescriptorSet(m_batch_descriptor_set_layout);
  m_vram_copy_descriptor_set = g_vulkan_context->AllocateGlobalDescriptorSet(m_single_sampler_descriptor_set_layout);
  m_vram_read_descriptor_set = g_vulkan_context->AllocateGlobalDescriptorSet(m_single_sampler_descriptor_set_layout);
  m_display_descriptor_set = g_vulkan_context->AllocateGlobalDescriptorSet(m_single_sampler_descriptor_set_layout);
  if (m_batch_descriptor_set == VK_NULL_HANDLE || m_vram_copy_descriptor_set == VK_NULL_HANDLE ||
      m_vram_read_descriptor_set == VK_NULL_HANDLE || m_display_descriptor_set == VK_NULL_HANDLE)
  {
    return false;
  }

  dsubuilder.AddBufferDescriptorWrite(m_batch_descriptor_set, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                                      m_uniform_stream_buffer.GetBuffer(), 0, sizeof(BatchUBOData));
  dsubuilder.AddCombinedImageSamplerDescriptorWrite(m_batch_descriptor_set, 1, m_vram_read_texture.GetView(),
                                                    m_point_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  dsubuilder.AddCombinedImageSamplerDescriptorWrite(m_vram_copy_descriptor_set, 1, m_vram_read_texture.GetView(),
                                                    m_point_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  dsubuilder.AddCombinedImageSamplerDescriptorWrite(m_vram_read_descriptor_set, 1, m_vram_texture.GetView(),
                                                    m_point_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  dsubuilder.AddCombinedImageSamplerDescriptorWrite(m_display_descriptor_set, 1, m_display_texture.GetView(),
                                                    m_point_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  dsubuilder.Update(g_vulkan_context->GetDevice());

  if (m_downsample_mode != GPUDownsampleMode::Disabled)
  {
    if (!CreateDownsampleResources(texture_width, texture_height, texture_format))
      return false;
  }

  ClearDisplay();
  SetFullVRAMDirtyRectangle();
  return true;
}

bool GPU_HW_Vulkan::CreateDownsampleResources(uint32_t texture_width, uint32_t texture_height, VkFormat texture_format)
{
  VkCommandBuffer cmdbuf = g_vulkan_context->GetCurrentCommandBuffer();
  Vulkan::DescriptorSetUpdateBuilder dsubuilder;
  Vulkan::FramebufferBuilder fbb;

  if (m_downsample_mode == GPUDownsampleMode::Adaptive)
  {
    const uint32_t levels = GetAdaptiveDownsamplingMipLevels();

    if (!m_downsample_texture.Create(texture_width, texture_height, levels, 1, texture_format, VK_SAMPLE_COUNT_1_BIT,
                                     VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
                                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                       VK_IMAGE_USAGE_TRANSFER_DST_BIT) ||
        !m_downsample_weight_texture.Create(VRAM_WIDTH, VRAM_HEIGHT, 1, 1, VK_FORMAT_R8_UNORM, VK_SAMPLE_COUNT_1_BIT,
                                            VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
                                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT))
    {
      return false;
    }

    m_downsample_texture.TransitionToLayout(cmdbuf, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    m_downsample_render_pass = g_vulkan_context->GetRenderPass(m_downsample_texture.GetFormat(), VK_FORMAT_UNDEFINED,
                                                               VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR);
    m_downsample_weight_render_pass = g_vulkan_context->GetRenderPass(
      m_downsample_weight_texture.GetFormat(), VK_FORMAT_UNDEFINED, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR);
    if (m_downsample_render_pass == VK_NULL_HANDLE || m_downsample_weight_render_pass == VK_NULL_HANDLE)
      return false;

    m_downsample_weight_framebuffer = m_downsample_weight_texture.CreateFramebuffer(m_downsample_weight_render_pass);
    if (m_downsample_weight_framebuffer == VK_NULL_HANDLE)
      return false;

    m_downsample_mip_views.resize(levels);
    for (uint32_t i = 0; i < levels; i++)
    {
      SmoothMipView& mv = m_downsample_mip_views[i];

      const VkImageViewCreateInfo vci = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                         nullptr,
                                         0,
                                         m_downsample_texture.GetImage(),
                                         VK_IMAGE_VIEW_TYPE_2D,
                                         m_downsample_texture.GetFormat(),
                                         {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                                          VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY},
                                         {VK_IMAGE_ASPECT_COLOR_BIT, i, 1u, 0u, 1u}};
      VkResult res = vkCreateImageView(g_vulkan_context->GetDevice(), &vci, nullptr, &mv.image_view);
      if (res != VK_SUCCESS)
      {
        LOG_VULKAN_ERROR(res, "vkCreateImageView() for smooth mip failed: ");
        return false;
      }

      mv.descriptor_set = g_vulkan_context->AllocateGlobalDescriptorSet(m_single_sampler_descriptor_set_layout);
      if (mv.descriptor_set == VK_NULL_HANDLE)
        return false;

      dsubuilder.AddCombinedImageSamplerDescriptorWrite(m_downsample_mip_views[i].descriptor_set, 1,
                                                        m_downsample_mip_views[i].image_view, m_point_sampler,
                                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

      fbb.AddAttachment(mv.image_view);
      fbb.SetRenderPass(m_downsample_render_pass);
      fbb.SetSize(texture_width >> i, texture_height >> i, 1);
      mv.framebuffer = fbb.Create(g_vulkan_context->GetDevice());
      if (mv.framebuffer == VK_NULL_HANDLE)
        return false;
    }

    m_downsample_composite_descriptor_set =
      g_vulkan_context->AllocateGlobalDescriptorSet(m_downsample_composite_descriptor_set_layout);
    if (m_downsample_composite_descriptor_set_layout == VK_NULL_HANDLE)
      return false;

    dsubuilder.AddCombinedImageSamplerDescriptorWrite(m_downsample_composite_descriptor_set, 1,
                                                      m_downsample_texture.GetView(), m_trilinear_sampler,
                                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    dsubuilder.AddCombinedImageSamplerDescriptorWrite(m_downsample_composite_descriptor_set, 2,
                                                      m_downsample_weight_texture.GetView(), m_linear_sampler,
                                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    dsubuilder.Update(g_vulkan_context->GetDevice());
  }
  else if (m_downsample_mode == GPUDownsampleMode::Box)
  {
    if (!m_downsample_texture.Create(VRAM_WIDTH, VRAM_HEIGHT, 1, 1, texture_format, VK_SAMPLE_COUNT_1_BIT,
                                     VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
                                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                       VK_IMAGE_USAGE_TRANSFER_SRC_BIT))
    {
      return false;
    }

    m_downsample_render_pass = g_vulkan_context->GetRenderPass(m_downsample_texture.GetFormat(), VK_FORMAT_UNDEFINED,
                                                               VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR);

    m_downsample_mip_views.resize(1);
    m_downsample_mip_views[0].framebuffer = m_downsample_texture.CreateFramebuffer(m_downsample_render_pass);
    if (m_downsample_mip_views[0].framebuffer == VK_NULL_HANDLE)
      return false;
  }
  return true;
}

void GPU_HW_Vulkan::DestroyDownsampleResources()
{
  Vulkan::Util::SafeFreeGlobalDescriptorSet(m_downsample_composite_descriptor_set);
  for (SmoothMipView& mv : m_downsample_mip_views)
  {
    Vulkan::Util::SafeFreeGlobalDescriptorSet(mv.descriptor_set);
    Vulkan::Util::SafeDestroyImageView(mv.image_view);
    Vulkan::Util::SafeDestroyFramebuffer(mv.framebuffer);
  }
  m_downsample_mip_views.clear();
  m_downsample_texture.Destroy(false);
  Vulkan::Util::SafeDestroyFramebuffer(m_downsample_weight_framebuffer);
  m_downsample_weight_texture.Destroy(false);
}

void GPU_HW_Vulkan::DestroyDownsamplePipelines()
{
  Vulkan::Util::SafeDestroyPipeline(m_downsample_first_pass_pipeline);
  Vulkan::Util::SafeDestroyPipeline(m_downsample_mid_pass_pipeline);
  Vulkan::Util::SafeDestroyPipeline(m_downsample_blur_pass_pipeline);
  Vulkan::Util::SafeDestroyPipeline(m_downsample_composite_pass_pipeline);
}

void GPU_HW_Vulkan::ClearFramebuffer()
{
  VkCommandBuffer cmdbuf = g_vulkan_context->GetCurrentCommandBuffer();

  m_vram_texture.TransitionToLayout(cmdbuf, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  m_vram_depth_texture.TransitionToLayout(cmdbuf, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

  static constexpr VkClearColorValue cc = {};
  const VkClearDepthStencilValue cds = {m_pgxp_depth_buffer ? 1.0f : 0.0f, 0};
  static constexpr VkImageSubresourceRange csrr = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u};
  static constexpr VkImageSubresourceRange dsrr = {VK_IMAGE_ASPECT_DEPTH_BIT, 0u, 1u, 0u, 1u};
  vkCmdClearColorImage(cmdbuf, m_vram_texture.GetImage(), m_vram_texture.GetLayout(), &cc, 1u, &csrr);
  vkCmdClearDepthStencilImage(cmdbuf, m_vram_depth_texture.GetImage(), m_vram_depth_texture.GetLayout(), &cds, 1u,
                              &dsrr);

  m_vram_texture.TransitionToLayout(cmdbuf, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
  m_vram_depth_texture.TransitionToLayout(cmdbuf, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
  m_last_depth_z = 1.0f;

  SetFullVRAMDirtyRectangle();
}

void GPU_HW_Vulkan::DestroyFramebuffer()
{
  DestroyDownsampleResources();

  Vulkan::Util::SafeFreeGlobalDescriptorSet(m_batch_descriptor_set);
  Vulkan::Util::SafeFreeGlobalDescriptorSet(m_vram_copy_descriptor_set);
  Vulkan::Util::SafeFreeGlobalDescriptorSet(m_vram_read_descriptor_set);
  Vulkan::Util::SafeFreeGlobalDescriptorSet(m_display_descriptor_set);

  Vulkan::Util::SafeDestroyFramebuffer(m_vram_framebuffer);
  Vulkan::Util::SafeDestroyFramebuffer(m_vram_update_depth_framebuffer);
  Vulkan::Util::SafeDestroyFramebuffer(m_vram_readback_framebuffer);
  Vulkan::Util::SafeDestroyFramebuffer(m_display_framebuffer);

  m_vram_read_texture.Destroy(false);
  m_vram_depth_texture.Destroy(false);
  m_vram_texture.Destroy(false);
  m_vram_readback_texture.Destroy(false);
  m_display_texture.Destroy(false);
  m_vram_readback_staging_texture.Destroy(false);
}

bool GPU_HW_Vulkan::CreateVertexBuffer()
{
  if (!m_vertex_stream_buffer.Create(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VERTEX_BUFFER_SIZE))
    return false;
  return true;
}

bool GPU_HW_Vulkan::CreateUniformBuffer()
{
  if (!m_uniform_stream_buffer.Create(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, UNIFORM_BUFFER_SIZE))
    return false;
  return true;
}

bool GPU_HW_Vulkan::CreateTextureBuffer()
{
  if (m_use_ssbos_for_vram_writes)
  {
    if (!m_texture_stream_buffer.Create(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VRAM_UPDATE_TEXTURE_BUFFER_SIZE))
      return false;

    m_vram_write_descriptor_set = g_vulkan_context->AllocateGlobalDescriptorSet(m_vram_write_descriptor_set_layout);
    if (m_vram_write_descriptor_set == VK_NULL_HANDLE)
      return false;

    Vulkan::DescriptorSetUpdateBuilder dsubuilder;
    dsubuilder.AddBufferDescriptorWrite(m_vram_write_descriptor_set, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                        m_texture_stream_buffer.GetBuffer(), 0,
                                        m_texture_stream_buffer.GetCurrentSize());
    dsubuilder.Update(g_vulkan_context->GetDevice());
    return true;
  }
  else
  {
    if (!m_texture_stream_buffer.Create(VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT, VRAM_UPDATE_TEXTURE_BUFFER_SIZE))
      return false;
    Vulkan::BufferViewBuilder bvbuilder;
    bvbuilder.Set(m_texture_stream_buffer.GetBuffer(), VK_FORMAT_R16_UINT, 0, m_texture_stream_buffer.GetCurrentSize());
    m_texture_stream_buffer_view = bvbuilder.Create(g_vulkan_context->GetDevice());
    if (m_texture_stream_buffer_view == VK_NULL_HANDLE)
      return false;

    m_vram_write_descriptor_set = g_vulkan_context->AllocateGlobalDescriptorSet(m_vram_write_descriptor_set_layout);
    if (m_vram_write_descriptor_set == VK_NULL_HANDLE)
      return false;

    Vulkan::DescriptorSetUpdateBuilder dsubuilder;
    dsubuilder.AddBufferViewDescriptorWrite(m_vram_write_descriptor_set, 0, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
                                            m_texture_stream_buffer_view);
    dsubuilder.Update(g_vulkan_context->GetDevice());
  }
  return true;
}

bool GPU_HW_Vulkan::CompilePipelines()
{
  // Make sure no previous background-compile worker is still alive
  // (UpdateSettings triggers DestroyPipelines -> CompilePipelines).
  StopShaderCompileThread();

  VkDevice device = g_vulkan_context->GetDevice();
  VkPipelineCache pipeline_cache = g_vulkan_shader_cache->GetPipelineCache();

  // Three-mode precompile control - see GPUShaderPrecompileMode in
  // core/types.h.
  //
  // 'precompile_sync' also gates the non-batch pipelines (VRAM
  // fill / copy / write / update depth / readback, display,
  // downsample, and the shared fullscreen-quad / UV-quad vertex
  // shaders). On 'Disabled' those are faulted in via the
  // GetXxxPipeline helpers the first time the runloop actually
  // needs them, matching the documented "Disabled = no compilation
  // at init" contract. On 'Lazy' the main-thread pre-fill at the
  // end of CompilePipelines fills them before spawning the
  // background batch-matrix worker (see the Lazy block below for
  // why this can't be deferred to the worker). On 'Enabled' we
  // still build everything upfront here.
  const GPUShaderPrecompileMode precompile_mode = g_settings.gpu_shader_precompile_mode;
  const bool precompile_sync = (precompile_mode == GPUShaderPrecompileMode::Enabled);
  // Reachable cell counts for each matrix - see IsBatchShaderReachable
  // in gpu_hw.h. The PSO matrix multiplies by 3 (depth_test) * 5
  // (transparency_mode); these axes don't affect reachability, so the
  // per-(render_mode, texture_mode) skip rule is independent of them.
  const uint32_t reachable_batch_cells = CountReachableBatchShaders(m_supports_dual_source_blend);
  const uint32_t batch_shader_progress_units = precompile_sync ? reachable_batch_cells : 0u;
  const uint32_t batch_pipeline_progress_units =
    precompile_sync ? reachable_batch_cells * 3u * 5u : 0u;
  // Non-batch units only counted when we build them upfront. The
  // fullscreen-quad VS (1), VRAM fill (4), VRAM copy (2), VRAM
  // write (2), VRAM update depth (1), VRAM readback (1), display
  // (6), and the optional downsample group sum to 17 base + at
  // most 4 downsample = 21. One tick per pipeline so the progress
  // bar tracks the actual work being done.
  const uint32_t non_batch_progress_units = precompile_sync ?
    (17u + (m_downsample_mode == GPUDownsampleMode::Adaptive ? 4u : (m_downsample_mode == GPUDownsampleMode::Box ? 1u : 0u))) :
    0u;

  ShaderCompileProgressTracker progress("Compiling Pipelines",
                                        2 + batch_shader_progress_units + batch_pipeline_progress_units +
                                          non_batch_progress_units);

  // Pre-baked batch VS: 18 blobs cover the structural axes (attribute
  // layout x interpolation x perspective). The per-session state
  // selects which two (one per 'textured' value) get instantiated.
  // PGXP_DEPTH and RESOLUTION_SCALE are spec constants applied at
  // pipeline-create time inside GetBatchPipeline, not at module-create
  // time here.
  //
  // The slot-populated guard makes this loop idempotent. On a filter-
  // only setting change UpdateSettings skips DestroyPipelines and
  // re-enters CompilePipelines; the existing VS modules are still
  // valid (filter is FS-only), so we skip the create. On a fresh
  // call (initial init or post-DestroyPipelines) every slot is
  // VK_NULL_HANDLE and the bake proceeds normally.
  const bool msaa               = (m_multisamples > 1);
  const bool per_sample_shading = m_per_sample_shading;
  const bool noperspective_col  = m_disable_color_perspective;
  for (uint8_t textured = 0; textured < 2; textured++)
  {
    if (m_batch_vertex_shaders[textured].Load() != VK_NULL_HANDLE)
    {
      progress.Increment();
      continue;
    }

    const Vulkan::EmbeddedShaders::EmbeddedShaderBlob& blob =
      Vulkan::EmbeddedShaders::GetBatchVertexShaderBlob(textured != 0, msaa, per_sample_shading,
                                                        noperspective_col);
    VkShaderModule shader = Vulkan::EmbeddedShaders::CreateShaderModule(blob.spv, blob.size_bytes);
    if (shader == VK_NULL_HANDLE)
    {
      // DestroyPipelines takes care of partial state from a failed
      // CompilePipelines via its enumerate(SafeDestroy*) sweeps,
      // because the arrays we're filling here are now members.
      return false;
    }

    m_batch_vertex_shaders[textured].Store(shader);
    progress.Increment();
  }

  // Batch fragment shader matrix and PSO matrix. Behaviour depends
  // on g_settings.gpu_shader_precompile_mode (see core/types.h):
  //
  //   - Enabled : compile every batch shader-module and every PSO
  //               right here, as in the historical implementation.
  //   - Lazy    : leave the matrices empty; spawn a worker at the
  //               end of CompilePipelines that fills them in the
  //               background.
  //   - Disabled: leave the matrices empty. GetBatchFragmentShader /
  //               GetBatchPipeline fault each combo in on the main
  //               thread the first time the game draws it.
  //
  // Both helpers handle the Reserved_*Direct16Bit dedup at the
  // helper entry (remap texture_mode 3 -> 2 and 7 -> 6 before the
  // array index), so the precompile loop doesn't need to special-
  // case them - the entry for 3 / 7 simply forwards to the
  // canonical slot.
  //
  // Structurally unreachable cells (reserved texture modes, two-pass
  // fallback render modes with no texture, single-pass dual-source
  // on hardware that lacks it) are skipped via IsBatchShaderReachable.
  if (precompile_sync)
  {
    const bool dual_source = m_supports_dual_source_blend;
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
            VkShaderModule shader =
              GetBatchFragmentShader(m_texture_filtering, render_mode, texture_mode,
                                     static_cast<bool>(dithering), static_cast<bool>(interlacing));
            if (shader == VK_NULL_HANDLE)
              return false;
            progress.Increment();
          }
        }
      }
    }

    for (uint8_t depth_test = 0; depth_test < 3; depth_test++)
    {
      for (uint8_t render_mode = 0; render_mode < 4; render_mode++)
      {
        for (uint8_t transparency_mode = 0; transparency_mode < 5; transparency_mode++)
        {
          for (uint8_t texture_mode = 0; texture_mode < 9; texture_mode++)
          {
            if (!IsBatchShaderReachable(static_cast<BatchRenderMode>(render_mode), texture_mode, dual_source))
              continue;

            for (uint8_t dithering = 0; dithering < 2; dithering++)
            {
              for (uint8_t interlacing = 0; interlacing < 2; interlacing++)
              {
                VkPipeline pipeline =
                  GetBatchPipeline(m_texture_filtering, m_true_color, m_scaled_dithering,
                                   depth_test, render_mode, texture_mode, transparency_mode,
                                   static_cast<bool>(dithering), static_cast<bool>(interlacing), false);
                if (pipeline == VK_NULL_HANDLE)
                  return false;
                progress.Increment();
              }
            }
          }
        }
      }
    }
  }
  // For Lazy and Disabled: matrices stay empty here, filled on
  // demand by the helpers. The background-thread launch for Lazy
  // happens at the end of this function, after the rest of the
  // non-batch pipelines are built.

  // Non-batch pipelines (VRAM fill / copy / write / update depth /
  // readback, display, downsample, fullscreen-quad / UV-quad VS).
  // On 'Enabled' build them all upfront here through the lazy
  // helpers so the helpers' GetXxx slot-fill machinery is
  // exercised the same way it would be at draw time. The progress
  // bar continues to tick per pipeline.
  //
  // On 'Lazy' the main thread pre-fills them below (before
  // spawning the worker) - see the Lazy branch comment for why
  // pre-filling can't be deferred to the worker. On 'Disabled' the
  // bulk of utility pipelines stay lazy, but the three hot-path
  // pipelines exercised inside UpdateSettings (vram_readback,
  // vram_write, vram_update_depth) get a targeted pre-build below
  // alongside the Lazy branch - see that block for the full
  // explanation.
  if (precompile_sync)
  {
    if (GetFullscreenQuadVertexShader() == VK_NULL_HANDLE)
      return false;
    progress.Increment();

    for (uint8_t wrapped = 0; wrapped < 2; wrapped++)
    {
      for (uint8_t interlaced = 0; interlaced < 2; interlaced++)
      {
        if (GetVRAMFillPipeline(wrapped, interlaced) == VK_NULL_HANDLE)
          return false;
        progress.Increment();
      }
    }
    for (uint8_t depth_test = 0; depth_test < 2; depth_test++)
    {
      if (GetVRAMCopyPipeline(depth_test) == VK_NULL_HANDLE)
        return false;
      progress.Increment();
    }
    for (uint8_t depth_test = 0; depth_test < 2; depth_test++)
    {
      if (GetVRAMWritePipeline(depth_test) == VK_NULL_HANDLE)
        return false;
      progress.Increment();
    }
    if (GetVRAMUpdateDepthPipeline() == VK_NULL_HANDLE)
      return false;
    progress.Increment();
    if (GetVRAMReadbackPipeline() == VK_NULL_HANDLE)
      return false;
    progress.Increment();
    for (uint8_t depth_24 = 0; depth_24 < 2; depth_24++)
    {
      for (uint8_t interlace_mode = 0; interlace_mode < 3; interlace_mode++)
      {
        if (GetDisplayPipeline(depth_24, interlace_mode) == VK_NULL_HANDLE)
          return false;
        progress.Increment();
      }
    }

    if (m_downsample_mode == GPUDownsampleMode::Adaptive)
    {
      if (GetDownsampleFirstPassPipeline() == VK_NULL_HANDLE ||
          GetDownsampleMidPassPipeline() == VK_NULL_HANDLE ||
          GetDownsampleBlurPassPipeline() == VK_NULL_HANDLE ||
          GetDownsampleCompositePassPipeline() == VK_NULL_HANDLE)
        return false;
      progress.Increment();
      progress.Increment();
      progress.Increment();
      progress.Increment();
    }
    else if (m_downsample_mode == GPUDownsampleMode::Box)
    {
      if (GetDownsampleFirstPassPipeline() == VK_NULL_HANDLE)
        return false;
      progress.Increment();
    }
  }

#undef UPDATE_PROGRESS

  if (precompile_mode == GPUShaderPrecompileMode::Lazy)
  {
    // Pre-fill the non-batch pipelines on the main thread BEFORE
    // launching the worker, as the D3D12 backend does, so the
    // runloop's first FillVRAM / UpdateDisplay / etc. doesn't fault
    // them in mid-frame.
    //
    // Pre-filling the non-batch pipelines here costs Lazy the same
    // sub-second upfront pause 'Enabled' pays for its non-batch
    // section, which is well within "instant startup" perception.
    // The worker then only walks the batch matrix.
    if (GetFullscreenQuadVertexShader() == VK_NULL_HANDLE)
      return false;
    for (uint8_t wrapped = 0; wrapped < 2; wrapped++)
    {
      for (uint8_t interlaced = 0; interlaced < 2; interlaced++)
      {
        if (GetVRAMFillPipeline(wrapped, interlaced) == VK_NULL_HANDLE)
          return false;
      }
    }
    for (uint8_t depth_test = 0; depth_test < 2; depth_test++)
    {
      if (GetVRAMCopyPipeline(depth_test) == VK_NULL_HANDLE)
        return false;
    }
    for (uint8_t depth_test = 0; depth_test < 2; depth_test++)
    {
      if (GetVRAMWritePipeline(depth_test) == VK_NULL_HANDLE)
        return false;
    }
    if (GetVRAMUpdateDepthPipeline() == VK_NULL_HANDLE)
      return false;
    if (GetVRAMReadbackPipeline() == VK_NULL_HANDLE)
      return false;
    for (uint8_t depth_24 = 0; depth_24 < 2; depth_24++)
    {
      for (uint8_t interlace_mode = 0; interlace_mode < 3; interlace_mode++)
      {
        if (GetDisplayPipeline(depth_24, interlace_mode) == VK_NULL_HANDLE)
          return false;
      }
    }
    if (m_downsample_mode == GPUDownsampleMode::Adaptive)
    {
      if (GetDownsampleFirstPassPipeline() == VK_NULL_HANDLE ||
          GetDownsampleMidPassPipeline() == VK_NULL_HANDLE ||
          GetDownsampleBlurPassPipeline() == VK_NULL_HANDLE ||
          GetDownsampleCompositePassPipeline() == VK_NULL_HANDLE)
        return false;
    }
    else if (m_downsample_mode == GPUDownsampleMode::Box)
    {
      if (GetDownsampleFirstPassPipeline() == VK_NULL_HANDLE)
        return false;
    }
  }
  else if (precompile_mode == GPUShaderPrecompileMode::Disabled)
  {
    // Targeted hot-path pre-build, even in Disabled mode. Three
    // utility pipelines are exercised inside UpdateSettings when a
    // setting change triggers framebuffer recreation (antialiasing
    // toggle, resolution scale change, true colour toggle, anything
    // that flips m_multisamples or m_resolution_scale):
    //
    //   - m_vram_readback_pipeline    - ReadVRAM call before the
    //                                   ExecuteCommandBuffer drain
    //                                   (UpdateSettings line ~794).
    //   - m_vram_write_pipelines[0/1] - UpdateVRAM call after the new
    //                                   framebuffer is in place
    //                                   (UpdateSettings line ~816).
    //   - m_vram_update_depth_pipeline - UpdateDepthBufferFromMask-
    //                                   Bit call directly after
    //                                   UpdateVRAM (UpdateSettings
    //                                   line ~817).
    //
    // Lazy-building any of these inside their respective UpdateSettings
    // cmdbufs hits an NVIDIA driver quirk that produces corrupted
    // rendering on the frame immediately after the toggle (most
    // visible as blocky / wrong-colour HUD and background tiles on
    // the second toggle cycle). Pre-building them here side-steps the
    // entire failure mode without changing Disabled's contract for
    // anything else - batch pipelines stay truly lazy (which is where
    // the actual init-time cost lives), and the other utility
    // pipelines (VRAM fill / copy / display / downsample) remain
    // lazy too because they are only ever exercised on game-driven
    // draws, well after the UpdateSettings cmdbuf has been submitted
    // and a fresh cmdbuf is recording.
    //
    // Cost: four extra vkCreateGraphicsPipelines calls on the main
    // thread at boot / setting-change time; well under 50 ms in
    // aggregate on a desktop GPU, invisible to the user.
    if (GetFullscreenQuadVertexShader() == VK_NULL_HANDLE)
      return false;
    for (uint8_t depth_test = 0; depth_test < 2; depth_test++)
    {
      if (GetVRAMWritePipeline(depth_test) == VK_NULL_HANDLE)
        return false;
    }
    if (GetVRAMUpdateDepthPipeline() == VK_NULL_HANDLE)
      return false;
    if (GetVRAMReadbackPipeline() == VK_NULL_HANDLE)
      return false;
  }

  // Background batch warm-up worker. Started for both Lazy and
  // Enabled modes; not for Disabled (whose contract is "no compile
  // at init, no background compile - fault in on first use").
  //
  // The worker walks every reachable cell across all 7 filter
  // sub-cubes of m_batch_pipelines, calling GetBatchPipeline (which
  // is the same lazy-fault-in helper the draw path uses) with each
  // filter value in turn. Ordering inside ShaderCompileThreadEntry-
  // Point puts the current m_texture_filtering first so the runloop's
  // first draws hit populated slots ASAP; the other six filter
  // values follow in numeric order.
  //
  // In Enabled mode the precompile_sync block above already
  // populated the current filter's sub-cube synchronously, so the
  // worker's first pass over it hits the lock-free fast-return on
  // every cell - effectively a free walk that confirms the cache
  // and then moves on. The real work is populating the OTHER six
  // sub-cubes so any later filter swap is instant (no progress bar,
  // no driver compile, no destroy / recreate).
  //
  // In Lazy mode the worker is also responsible for the current
  // filter's sub-cube; nothing has populated it yet. The main
  // thread can race ahead and lazy-fault any slot it actually
  // needs at draw time - the worker observes the filled slot and
  // moves on.
  //
  // DestroyPipelines (or the UpdateSettings-level
  // StopShaderCompileThread we added in Option B) signals
  // m_shader_compile_thread_quit and joins; the worker checks the
  // flag between cells and can exit within ~one PSO compile.
  if (precompile_mode == GPUShaderPrecompileMode::Lazy ||
      precompile_mode == GPUShaderPrecompileMode::Enabled)
  {
    StartShaderCompileThread();
  }

  return true;
}

void GPU_HW_Vulkan::StartShaderCompileThread()
{
  retro_atomic_store_release_int(&m_shader_compile_thread_quit, 0);
  m_worker_pipeline_cache = g_vulkan_shader_cache->CreateWorkerPipelineCache();
  m_shader_compile_thread = sthread_create(
    [](void* self) { static_cast<GPU_HW_Vulkan*>(self)->ShaderCompileThreadEntryPoint(); }, this);
  if (!m_shader_compile_thread)
  {
    g_vulkan_shader_cache->MergeWorkerPipelineCache(m_worker_pipeline_cache);
    m_worker_pipeline_cache = VK_NULL_HANDLE;
  }
}

void GPU_HW_Vulkan::StopShaderCompileThread()
{
  if (!m_shader_compile_thread)
    return;

  retro_atomic_store_release_int(&m_shader_compile_thread_quit, 1);
  sthread_join(m_shader_compile_thread);
  m_shader_compile_thread = nullptr;
  retro_atomic_store_release_int(&m_shader_compile_thread_quit, 0);

  g_vulkan_shader_cache->MergeWorkerPipelineCache(m_worker_pipeline_cache);
  m_worker_pipeline_cache = VK_NULL_HANDLE;
}

void GPU_HW_Vulkan::ShaderCompileThreadEntryPoint()
{
  // Lower this worker's scheduling priority to "below normal" so
  // it doesn't compete with the runloop / CPU emulation / audio
  // threads on CPU-contended systems. Best-effort: if the platform
  // refuses we just keep going at default priority. See
  // common/thread_priority.h for the per-platform mechanics.
  ThreadPriority::LowerCurrentThreadPriority();

  // Filter is the outermost loop. Order: current m_texture_filtering
  // first (so a Lazy-mode launch from a cold cache starts populating
  // the slots the runloop will actually hit ASAP), then the remaining
  // six filter values in numeric order. In Enabled mode the current
  // filter's sub-cube was already filled synchronously by the
  // precompile_sync block in CompilePipelines, so the worker's
  // first pass over it hits the lock-free fast-return path on every
  // cell - effectively a free walk that just verifies the cache and
  // then moves on to populate the other six.
  //
  // GPUTextureFilter::Count is 7 (Nearest, Bilinear, BilinearBinAlpha,
  // JINC2, JINC2BinAlpha, xBR, xBRBinAlpha). On hardware that lacks
  // dual-source blend the *BinAlpha and non-Nearest filters cannot
  // actually be used at runtime (UpdateHWSettings forces
  // m_texture_filtering back to Nearest), so skip warming those
  // sub-cubes - they would never be looked up at draw time and the
  // PSOs would just sit unused.
  //
  // The (m_true_color, m_scaled_dithering) snapshot is taken once at
  // worker entry. The worker is responsible only for the current
  // (true_color, scaled_dithering) combo's seven filter sub-cubes -
  // walking all four combos in the background would balloon warm-up
  // time to several minutes on a cold pipeline cache, well past
  // "useful". Toggling true_color or scaled_dithering at runtime is
  // serviced the same way a filter swap is: the previous combo's
  // sub-cubes stay populated and are instantly addressable on a
  // cycle-back, while the new combo gets a sync compile of its
  // current filter sub-cube (Enabled) or lazy fault-in (Lazy /
  // Disabled), plus a new background worker rooted at the new
  // (true_color, scaled_dithering) tuple covering the other six
  // filters.
  //
  // The quit flag is checked between cells so DestroyPipelines can
  // stop the worker within at most one PSO compile of latency
  // (Vulkan PSO compiles can be ~50-200 ms with the heavier texture
  // filters).
  //
  // Structurally unreachable cells are skipped via
  // IsBatchShaderReachable.
  const bool dual_source = m_supports_dual_source_blend;
  const uint8_t current_filter = static_cast<uint8_t>(m_texture_filtering);
  const bool true_color = m_true_color;
  const bool scaled_dithering = m_scaled_dithering;
  for (uint8_t f_offset = 0; f_offset < static_cast<uint8_t>(GPUTextureFilter::Count); f_offset++)
  {
    const uint8_t filter_idx =
      (current_filter + f_offset) % static_cast<uint8_t>(GPUTextureFilter::Count);
    const GPUTextureFilter filter = static_cast<GPUTextureFilter>(filter_idx);

    if (!dual_source && TextureFilterRequiresDualSourceBlend(filter))
      continue;

    for (uint8_t depth_test = 0; depth_test < 3; depth_test++)
    {
      for (uint8_t render_mode = 0; render_mode < 4; render_mode++)
      {
        for (uint8_t transparency_mode = 0; transparency_mode < 5; transparency_mode++)
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

                GetBatchPipeline(filter, true_color, scaled_dithering, depth_test, render_mode,
                                 texture_mode, transparency_mode,
                                 static_cast<bool>(dithering), static_cast<bool>(interlacing), true);
              }
            }
          }
        }
      }
    }
  }
}

VkShaderModule GPU_HW_Vulkan::GetBatchFragmentShader(GPUTextureFilter filter, uint8_t render_mode, uint8_t texture_mode,
                                                     bool dithering, bool interlacing)
{
  // Reserved_*Direct16Bit shader-source dedup, applied at the helper
  // entry. Slots for texture_mode 3 / 7 are never written; all
  // accesses route through the canonical slots 2 / 6. SafeDestroy*
  // on the dup slots in DestroyPipelines is a VK_NULL_HANDLE no-op.
  const uint8_t lookup_mode = (texture_mode == static_cast<uint8_t>(GPUTextureMode::Reserved_Direct16Bit))    ? 2u :
                         (texture_mode == static_cast<uint8_t>(GPUTextureMode::Reserved_RawDirect16Bit)) ? 6u :
                                                                                                      texture_mode;

  // Fast path: lock-free load. Both the precompile worker and the
  // main thread can read a slot concurrently without contending
  // with each other or with each other's slow-path compiles. The
  // filter param routes slot indexing AND blob selection - the
  // worker passes the filter it is currently warming, the draw path
  // passes m_texture_filtering. Other filter sub-cubes are populated
  // independently and stay valid across a filter-only setting
  // change (UpdateSettings skips DestroyPipelines in that case).
  AtomicSlot<VkShaderModule>& slot =
    m_batch_fragment_shaders[static_cast<uint8_t>(filter)][render_mode][lookup_mode]
                            [static_cast<uint8_t>(dithering)][static_cast<uint8_t>(interlacing)];
  VkShaderModule existing = slot.Load();
  if (existing != VK_NULL_HANDLE)
    return existing;

  // Slow path. All texture filters and the untextured path are pre-
  // baked; pick the structural blob from the (filter, render_mode,
  // texture_mode) tuple plus per-session structural state.  Per-call
  // spec constants (TRANSPARENCY, DITHERING, INTERLACING, PALETTE_*,
  // RAW_TEXTURE, BINALPHA, ...) are applied at pipeline-create time
  // in GetBatchPipeline. vkCreateShaderModule is documented thread-
  // safe by the Vulkan spec, so two threads racing to instantiate
  // the SAME blob is harmless - the publish step picks one
  // VkShaderModule and the loser destroys its.
  //
  // use_dual_source matches the original shadergen-side derivation:
  //   supports && ((render_mode is transparent) || filter != Nearest)
  const bool is_textured = (lookup_mode != static_cast<uint8_t>(GPUTextureMode::Disabled));
  const bool dual_source = m_supports_dual_source_blend &&
    ((render_mode == static_cast<uint8_t>(BatchRenderMode::TransparentAndOpaque) ||
      render_mode == static_cast<uint8_t>(BatchRenderMode::OnlyTransparent)) ||
     (filter != GPUTextureFilter::Nearest));
  const Vulkan::EmbeddedShaders::EmbeddedShaderBlob* blob_ptr;
  if (!is_textured)
  {
    blob_ptr = &Vulkan::EmbeddedShaders::GetBatchUntexturedFragmentShaderBlob(
      m_multisamples > 1, m_per_sample_shading, m_disable_color_perspective,
      dual_source);
  }
  else
  {
    switch (filter)
    {
      case GPUTextureFilter::Nearest:
        blob_ptr = &Vulkan::EmbeddedShaders::GetBatchTexturedNearestFragmentShaderBlob(
          m_multisamples > 1, m_per_sample_shading, m_disable_color_perspective,
          dual_source);
        break;
      case GPUTextureFilter::Bilinear:
      case GPUTextureFilter::BilinearBinAlpha:
        blob_ptr = &Vulkan::EmbeddedShaders::GetBatchTexturedBilinearFragmentShaderBlob(
          m_multisamples > 1, m_per_sample_shading, m_disable_color_perspective,
          dual_source);
        break;
      case GPUTextureFilter::JINC2:
      case GPUTextureFilter::JINC2BinAlpha:
        blob_ptr = &Vulkan::EmbeddedShaders::GetBatchTexturedJINC2FragmentShaderBlob(
          m_multisamples > 1, m_per_sample_shading, m_disable_color_perspective,
          dual_source);
        break;
      case GPUTextureFilter::xBR:
      case GPUTextureFilter::xBRBinAlpha:
        blob_ptr = &Vulkan::EmbeddedShaders::GetBatchTexturedXBRFragmentShaderBlob(
          m_multisamples > 1, m_per_sample_shading, m_disable_color_perspective,
          dual_source);
        break;
      default:
        Log_ErrorPrintf("GetBatchFragmentShader: unknown texture filter %u",
                        static_cast<unsigned>(filter));
        return VK_NULL_HANDLE;
    }
  }
  VkShaderModule fresh = Vulkan::EmbeddedShaders::CreateShaderModule(blob_ptr->spv, blob_ptr->size_bytes);
  if (fresh == VK_NULL_HANDLE)
  {
    Log_ErrorPrintf("Lazy batch fragment shader compile failed for (rm=%u, tm=%u, d=%u, i=%u)", render_mode,
                    texture_mode, static_cast<uint8_t>(dithering), static_cast<uint8_t>(interlacing));
    return VK_NULL_HANDLE;
  }

  // Publish step: if another thread won the race, destroy our fresh
  // module and return the winner's.
  if (!slot.CompareExchange(VK_NULL_HANDLE, fresh))
  {
    vkDestroyShaderModule(g_vulkan_context->GetDevice(), fresh, nullptr);
    return slot.Load();
  }

  return fresh;
}

VkPipeline GPU_HW_Vulkan::GetBatchPipeline(GPUTextureFilter filter, bool true_color, bool scaled_dithering,
                                           uint8_t depth_test, uint8_t render_mode,
                                           uint8_t texture_mode, uint8_t transparency_mode,
                                           bool dithering, bool interlacing, bool on_worker)
{
  // Reserved_*Direct16Bit PSO dedup, applied at the helper entry.
  // Slots for texture_mode 3 / 7 are never written; all accesses
  // route through the canonical slots 2 / 6.
  const uint8_t lookup_mode = (texture_mode == static_cast<uint8_t>(GPUTextureMode::Reserved_Direct16Bit))    ? 2u :
                         (texture_mode == static_cast<uint8_t>(GPUTextureMode::Reserved_RawDirect16Bit)) ? 6u :
                                                                                                      texture_mode;

  AtomicSlot<VkPipeline>& slot =
    m_batch_pipelines[static_cast<uint8_t>(filter)][static_cast<uint8_t>(true_color)]
                     [static_cast<uint8_t>(scaled_dithering)][depth_test][render_mode][lookup_mode]
                     [transparency_mode][static_cast<uint8_t>(dithering)][static_cast<uint8_t>(interlacing)];

  // Fast path: lock-free atomic load. This is what DrawBatchVertices
  // hits once a slot is filled (either by the precompile worker or
  // by an earlier main-thread fault-in). No kernel call, no
  // serialisation against the worker.
  VkPipeline existing = slot.Load();
  if (existing != VK_NULL_HANDLE)
    return existing;

  // Slow path. GetBatchFragmentShader is thread-safe (it
  // instantiates a pre-baked VkShaderModule and publishes it with a
  // compare-exchange). vkCreateGraphicsPipelines is the remaining
  // slow operation; per Vulkan 1.0 spec the pipelineCache parameter
  // is host-synchronised, so the worker passes its own cache.
  //
  // Two threads racing to compile the SAME slot both compile and
  // both reach the publish step; the loser destroys its pipeline
  // via vkDestroyPipeline and returns the winner's. Wasteful but
  // harmless - PSOs are deterministic on identical descriptors.
  VkShaderModule fs = GetBatchFragmentShader(filter, render_mode, lookup_mode, dithering, interlacing);
  if (fs == VK_NULL_HANDLE)
    return VK_NULL_HANDLE;

  static constexpr std::array<VkCompareOp, 3> depth_test_values = {
    VK_COMPARE_OP_ALWAYS, VK_COMPARE_OP_GREATER_OR_EQUAL, VK_COMPARE_OP_LESS_OR_EQUAL};
  const bool textured = (static_cast<GPUTextureMode>(lookup_mode) != GPUTextureMode::Disabled);

  Vulkan::GraphicsPipelineBuilder gpbuilder;
  gpbuilder.SetPipelineLayout(m_batch_pipeline_layout);
  gpbuilder.SetRenderPass(m_vram_render_pass, 0);

  gpbuilder.AddVertexBuffer(0, sizeof(BatchVertex), VK_VERTEX_INPUT_RATE_VERTEX);
  gpbuilder.AddVertexAttribute(0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(BatchVertex, x));
  gpbuilder.AddVertexAttribute(1, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(BatchVertex, color));
  if (textured)
  {
    gpbuilder.AddVertexAttribute(2, 0, VK_FORMAT_R32_UINT, offsetof(BatchVertex, u));
    gpbuilder.AddVertexAttribute(3, 0, VK_FORMAT_R32_UINT, offsetof(BatchVertex, texpage));
    // ATTR4 / a_uv_limits is bound unconditionally when textured.
    // Pre-UV_LIMITS-routing this was gated on m_using_uv_limits to
    // match the conditional a_uv_limits declaration in the
    // batch_vs_textured / batch_vs_textured_uvlim SPIR-V variants;
    // post-collapse (the batch_vs_textured_uvlim variants are gone -
    // the single textured variant always declares a_uv_limits) the
    // input layout matches the always-emitted shader input. The
    // BatchVertex uv_limits field is always populated by every
    // BatchVertex::Set call site (gpu_hw.h:47), so the binding always
    // points at a valid 4-byte slot regardless of whether
    // ComputePolygonUVLimits ran on the vertex.
    gpbuilder.AddVertexAttribute(4, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(BatchVertex, uv_limits));
  }

  gpbuilder.SetPrimitiveTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
  // Vertex shaders are filled at CompilePipelines time before the
  // worker is spawned, so a relaxed load is sufficient - we're
  // strictly after that store in happens-before order.
  //
  // Spec constants on the pre-baked batch VS: id=0 RESOLUTION_SCALE
  // only. PGXP_DEPTH used to be id=3 but is now routed through the
  // batch UBO (u_pgxp_depth at offset 52) - the VS reads the field
  // at runtime via a uniform-flow branch and the spec const slot is
  // no longer declared in batch.vert.glsl. Setting an undeclared
  // spec const is permitted by the Vulkan spec (it gets ignored)
  // but pointless, so we drop the AddBool too.
  Vulkan::SpecConstants vs_spec;
  vs_spec.AddUInt(0, static_cast<uint32_t>(m_resolution_scale));    // RESOLUTION_SCALE
  gpbuilder.SetVertexShader(
    m_batch_vertex_shaders[static_cast<uint8_t>(textured)].Load(), vs_spec.GetInfo());

  // Spec constants on the pre-baked batch FS (untextured slice and
  // textured-Nearest slice; textured non-Nearest slices still go
  // through shadergen and ignore these entries per Vulkan spec).
  //
  //   id =   0  RESOLUTION_SCALE              (uint, per-session)
  //   id = 103  DITHERING                     (bool, per-call)
  //   id = 104  INTERLACING                   (bool, per-call)
  //   id = 105  DITHERING_SCALED              (bool, cache-dim - the
  //                                            scaled_dithering
  //                                            argument is a dimension
  //                                            of m_batch_pipelines so
  //                                            toggling does not flush
  //                                            other sub-cubes)
  //   id = 106  TRUE_COLOR                    (bool, cache-dim - same
  //                                            mechanism as id 105)
  //   id = 107  PALETTE_4_BIT                 (bool, per-call -
  //                                            actual_texture_mode == 0)
  //   id = 108  PALETTE_8_BIT                 (bool, per-call -
  //                                            actual_texture_mode == 1)
  //   id = 109  RAW_TEXTURE                   (bool, per-call -
  //                                            texture_mode has
  //                                            RawTextureBit set)
  //   id = 110  BINALPHA                      (bool, per-call - the
  //                                            filter argument is one
  //                                            of the *BinAlpha
  //                                            filter values)
  //
  // 107-110 are meaningful only on the textured blobs; the untextured
  // blobs do not declare them. Passing them is harmless - unrecognised
  // spec const IDs are silently dropped.
  //
  // TRANSPARENCY (former id=100) / TRANSPARENCY_ONLY_OPAQUE (101) /
  // TRANSPARENCY_ONLY_TRANSPARENT (102) used to live as 3 spec
  // consts here encoding the 4-state BatchRenderMode enum across 3
  // booleans. They are now routed through the batch UBO
  // (u_render_mode at offset 60) - the C++ side re-uploads the UBO
  // between two-pass DrawIndexed calls so each draw sees its
  // matching enum value. The FS bytecode is invariant across the
  // flip, collapsing 4x of pipeline cache entries on Vulkan and 4x
  // of DXBC variants on the D3D12 pre-bake side. PSO blend state
  // still varies by BatchRenderMode at the GraphicsPipelineBuilder
  // level (per-render_mode matrix dim stays). Mirrors the prior
  // cbuffer-routing arc (DITHERING / INTERLACING / UV_LIMITS /
  // PGXP_DEPTH).
  const uint8_t actual_tex_mode =
    static_cast<uint8_t>(lookup_mode) &
    ~static_cast<uint8_t>(GPUTextureMode::RawTextureBit);
  const bool palette_4_bit = (actual_tex_mode == static_cast<uint8_t>(GPUTextureMode::Palette4Bit));
  const bool palette_8_bit = (actual_tex_mode == static_cast<uint8_t>(GPUTextureMode::Palette8Bit));
  const bool raw_texture   = (static_cast<uint8_t>(lookup_mode) &
                              static_cast<uint8_t>(GPUTextureMode::RawTextureBit)) != 0;
  const bool binalpha = (filter == GPUTextureFilter::BilinearBinAlpha ||
                         filter == GPUTextureFilter::JINC2BinAlpha ||
                         filter == GPUTextureFilter::xBRBinAlpha);
  Vulkan::SpecConstants fs_spec;
  fs_spec.AddUInt(  0, static_cast<uint32_t>(m_resolution_scale));
  fs_spec.AddBool(103, dithering);
  fs_spec.AddBool(104, interlacing);
  fs_spec.AddBool(105, scaled_dithering);
  fs_spec.AddBool(106, true_color);
  fs_spec.AddBool(107, palette_4_bit);
  fs_spec.AddBool(108, palette_8_bit);
  fs_spec.AddBool(109, raw_texture);
  fs_spec.AddBool(110, binalpha);
  gpbuilder.SetFragmentShader(fs, fs_spec.GetInfo());

  gpbuilder.SetRasterizationState(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
  gpbuilder.SetDepthState(true, true, depth_test_values[depth_test]);
  gpbuilder.SetNoBlendingState();
  gpbuilder.SetMultisamples(m_multisamples, m_per_sample_shading);

  if ((static_cast<GPUTransparencyMode>(transparency_mode) != GPUTransparencyMode::Disabled &&
       (static_cast<BatchRenderMode>(render_mode) != BatchRenderMode::TransparencyDisabled &&
        static_cast<BatchRenderMode>(render_mode) != BatchRenderMode::OnlyOpaque)) ||
      filter != GPUTextureFilter::Nearest)
  {
    if (m_supports_dual_source_blend)
    {
      gpbuilder.SetBlendAttachment(
        0, true, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_SRC1_ALPHA,
        (static_cast<GPUTransparencyMode>(transparency_mode) == GPUTransparencyMode::BackgroundMinusForeground &&
         static_cast<BatchRenderMode>(render_mode) != BatchRenderMode::TransparencyDisabled &&
         static_cast<BatchRenderMode>(render_mode) != BatchRenderMode::OnlyOpaque) ?
          VK_BLEND_OP_REVERSE_SUBTRACT :
          VK_BLEND_OP_ADD,
        VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD);
    }
    else
    {
      const float factor =
        (static_cast<GPUTransparencyMode>(transparency_mode) == GPUTransparencyMode::HalfBackgroundPlusHalfForeground) ?
          0.5f :
          1.0f;
      gpbuilder.SetBlendAttachment(
        0, true, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_CONSTANT_ALPHA,
        (static_cast<GPUTransparencyMode>(transparency_mode) == GPUTransparencyMode::BackgroundMinusForeground &&
         static_cast<BatchRenderMode>(render_mode) != BatchRenderMode::TransparencyDisabled &&
         static_cast<BatchRenderMode>(render_mode) != BatchRenderMode::OnlyOpaque) ?
          VK_BLEND_OP_REVERSE_SUBTRACT :
          VK_BLEND_OP_ADD,
        VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD);
      gpbuilder.SetBlendConstants(0.0f, 0.0f, 0.0f, factor);
    }
  }

  gpbuilder.SetDynamicViewportAndScissorState();

  // Per Vulkan 1.0 spec section "Threading Behavior", the
  // pipelineCache parameter to vkCreateGraphicsPipelines is in the
  // externally-synchronised parameter list, so the worker uses a
  // cache of its own.
  const VkPipelineCache pipeline_cache =
    on_worker ? m_worker_pipeline_cache : g_vulkan_shader_cache->GetPipelineCache();
  const VkPipeline fresh = gpbuilder.Create(g_vulkan_context->GetDevice(), pipeline_cache);
  if (fresh == VK_NULL_HANDLE)
  {
    Log_ErrorPrintf("Lazy batch PSO compile failed for (dt=%u, rm=%u, tm=%u, tr=%u, d=%u, i=%u)", depth_test,
                    render_mode, texture_mode, transparency_mode, static_cast<uint8_t>(dithering), static_cast<uint8_t>(interlacing));
    return VK_NULL_HANDLE;
  }

  // Publish step: if another thread won the race, destroy our fresh
  // pipeline and return the winner's.
  if (!slot.CompareExchange(VK_NULL_HANDLE, fresh))
  {
    vkDestroyPipeline(g_vulkan_context->GetDevice(), fresh, nullptr);
    return slot.Load();
  }

  return fresh;
}

// ----------------------------------------------------------------------
// Non-batch lazy helpers. These run main-thread-only - either at the
// Lazy main-thread pre-fill pass in CompilePipelines (before the
// batch worker is spawned), at the 'Enabled' synchronous compile
// pass in CompilePipelines, or as fault-ins on the first draw that
// needs them in 'Disabled' mode. The batch worker never touches
// these helpers or the main pipeline cache, so they need no atomic
// slots - each just returns a cached handle or builds and caches one.
//
// The shared fullscreen-quad / UV-quad vertex shader modules are
// kept as members so each helper can reach them without re-running
// vkCreateShaderModule. The per-pipeline fragment shaders are
// destroyed after use - each is used by exactly one pipeline so
// there's no benefit to caching them.
// ----------------------------------------------------------------------

VkShaderModule GPU_HW_Vulkan::GetFullscreenQuadVertexShader()
{
  if (m_fullscreen_quad_vertex_shader != VK_NULL_HANDLE)
    return m_fullscreen_quad_vertex_shader;

  // Pre-baked SPIR-V: no glslang invocation, no shader_cache lookup,
  // available the moment the Vulkan device is up. See
  // data/shaders/vulkan/screen_quad.vert.glsl for the source of the blob,
  // and src/common/vulkan/embedded_shaders.h for the loader.
  m_fullscreen_quad_vertex_shader = Vulkan::EmbeddedShaders::CreateShaderModule(
    Vulkan::EmbeddedShaders::k_screen_quad_vs,
    Vulkan::EmbeddedShaders::k_screen_quad_vs_size_bytes);
  if (m_fullscreen_quad_vertex_shader == VK_NULL_HANDLE)
    Log_ErrorPrint("Embedded fullscreen-quad vertex shader load failed");
  return m_fullscreen_quad_vertex_shader;
}

VkShaderModule GPU_HW_Vulkan::GetUVQuadVertexShader()
{
  if (m_uv_quad_vertex_shader != VK_NULL_HANDLE)
    return m_uv_quad_vertex_shader;

  // Pre-baked SPIR-V; see GetFullscreenQuadVertexShader for rationale.
  m_uv_quad_vertex_shader = Vulkan::EmbeddedShaders::CreateShaderModule(
    Vulkan::EmbeddedShaders::k_uv_quad_vs,
    Vulkan::EmbeddedShaders::k_uv_quad_vs_size_bytes);
  if (m_uv_quad_vertex_shader == VK_NULL_HANDLE)
    Log_ErrorPrint("Embedded UV-quad vertex shader load failed");
  return m_uv_quad_vertex_shader;
}

VkPipeline GPU_HW_Vulkan::GetVRAMFillPipeline(uint8_t wrapped, uint8_t interlaced)
{
  VkPipeline& slot = m_vram_fill_pipelines[wrapped][interlaced];
  if (slot != VK_NULL_HANDLE)
    return slot;

  VkShaderModule vs = GetFullscreenQuadVertexShader();
  if (vs == VK_NULL_HANDLE)
    return VK_NULL_HANDLE;

  // Pre-baked path: single SPIR-V blob, three bool spec constants drive
  // the four-to-eight specialised pipelines.
  VkShaderModule fs = Vulkan::EmbeddedShaders::CreateShaderModule(
    Vulkan::EmbeddedShaders::k_vram_fill_fs,
    Vulkan::EmbeddedShaders::k_vram_fill_fs_size_bytes);
  if (fs == VK_NULL_HANDLE)
    return VK_NULL_HANDLE;

  Vulkan::SpecConstants fs_spec;
  fs_spec.AddBool(  3, m_pgxp_depth_buffer);            // PGXP_DEPTH
  fs_spec.AddBool(100, interlaced != 0);                // INTERLACED
  fs_spec.AddBool(101, wrapped    != 0);                // WRAPPED

  VkDevice device = g_vulkan_context->GetDevice();
  VkPipelineCache pipeline_cache = g_vulkan_shader_cache->GetPipelineCache();

  Vulkan::GraphicsPipelineBuilder gpbuilder;
  gpbuilder.SetRenderPass(m_vram_render_pass, 0);
  gpbuilder.SetPipelineLayout(m_no_samplers_pipeline_layout);
  gpbuilder.SetPrimitiveTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
  gpbuilder.SetNoCullRasterizationState();
  gpbuilder.SetNoBlendingState();
  gpbuilder.SetDynamicViewportAndScissorState();
  gpbuilder.SetVertexShader(vs);
  gpbuilder.SetFragmentShader(fs, fs_spec.GetInfo());
  gpbuilder.SetMultisamples(m_multisamples, false);
  gpbuilder.SetDepthState(true, true, VK_COMPARE_OP_ALWAYS);

  slot = gpbuilder.Create(device, pipeline_cache, false);
  vkDestroyShaderModule(device, fs, nullptr);
  return slot;
}

VkPipeline GPU_HW_Vulkan::GetVRAMCopyPipeline(uint8_t depth_test)
{
  VkPipeline& slot = m_vram_copy_pipelines[depth_test];
  if (slot != VK_NULL_HANDLE)
    return slot;

  VkShaderModule vs = GetFullscreenQuadVertexShader();
  if (vs == VK_NULL_HANDLE)
    return VK_NULL_HANDLE;

  // Pre-baked path: single SPIR-V blob, two spec constants.
  VkShaderModule fs = Vulkan::EmbeddedShaders::CreateShaderModule(
    Vulkan::EmbeddedShaders::k_vram_copy_fs,
    Vulkan::EmbeddedShaders::k_vram_copy_fs_size_bytes);
  if (fs == VK_NULL_HANDLE)
    return VK_NULL_HANDLE;

  Vulkan::SpecConstants fs_spec;
  fs_spec.AddUInt(0, static_cast<uint32_t>(m_resolution_scale));  // RESOLUTION_SCALE
  fs_spec.AddBool(3, m_pgxp_depth_buffer);                        // PGXP_DEPTH

  VkDevice device = g_vulkan_context->GetDevice();
  VkPipelineCache pipeline_cache = g_vulkan_shader_cache->GetPipelineCache();

  Vulkan::GraphicsPipelineBuilder gpbuilder;
  gpbuilder.SetRenderPass(m_vram_render_pass, 0);
  gpbuilder.SetPipelineLayout(m_single_sampler_pipeline_layout);
  gpbuilder.SetPrimitiveTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
  gpbuilder.SetNoCullRasterizationState();
  gpbuilder.SetNoBlendingState();
  gpbuilder.SetDynamicViewportAndScissorState();
  gpbuilder.SetVertexShader(vs);
  gpbuilder.SetFragmentShader(fs, fs_spec.GetInfo());
  gpbuilder.SetMultisamples(m_multisamples, false);
  gpbuilder.SetDepthState((depth_test != 0), true,
                          (depth_test != 0) ? VK_COMPARE_OP_GREATER_OR_EQUAL : VK_COMPARE_OP_ALWAYS);

  slot = gpbuilder.Create(device, pipeline_cache, false);
  vkDestroyShaderModule(device, fs, nullptr);
  return slot;
}

VkPipeline GPU_HW_Vulkan::GetVRAMWritePipeline(uint8_t depth_test)
{
  VkPipeline& slot = m_vram_write_pipelines[depth_test];
  if (slot != VK_NULL_HANDLE)
    return slot;

  VkShaderModule vs = GetFullscreenQuadVertexShader();
  if (vs == VK_NULL_HANDLE)
    return VK_NULL_HANDLE;

  // Pre-baked path: two blobs, structurally split by descriptor type at
  // binding 0 (storage buffer vs uniform texel buffer). The per-session
  // m_use_ssbos_for_vram_writes decision picks which blob to use; both
  // share the same RESOLUTION_SCALE + PGXP_DEPTH spec constants.
  const uint32_t* spv;
  size_t spv_size;
  if (m_use_ssbos_for_vram_writes)
  {
    spv      = Vulkan::EmbeddedShaders::k_vram_write_ssbo_fs;
    spv_size = Vulkan::EmbeddedShaders::k_vram_write_ssbo_fs_size_bytes;
  }
  else
  {
    spv      = Vulkan::EmbeddedShaders::k_vram_write_texbuf_fs;
    spv_size = Vulkan::EmbeddedShaders::k_vram_write_texbuf_fs_size_bytes;
  }
  VkShaderModule fs = Vulkan::EmbeddedShaders::CreateShaderModule(spv, spv_size);
  if (fs == VK_NULL_HANDLE)
    return VK_NULL_HANDLE;

  Vulkan::SpecConstants fs_spec;
  fs_spec.AddUInt(0, static_cast<uint32_t>(m_resolution_scale));  // RESOLUTION_SCALE
  fs_spec.AddBool(3, m_pgxp_depth_buffer);                        // PGXP_DEPTH

  VkDevice device = g_vulkan_context->GetDevice();
  VkPipelineCache pipeline_cache = g_vulkan_shader_cache->GetPipelineCache();

  Vulkan::GraphicsPipelineBuilder gpbuilder;
  gpbuilder.SetRenderPass(m_vram_render_pass, 0);
  gpbuilder.SetPipelineLayout(m_vram_write_pipeline_layout);
  gpbuilder.SetPrimitiveTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
  gpbuilder.SetNoCullRasterizationState();
  gpbuilder.SetNoBlendingState();
  gpbuilder.SetDynamicViewportAndScissorState();
  gpbuilder.SetVertexShader(vs);
  gpbuilder.SetFragmentShader(fs, fs_spec.GetInfo());
  gpbuilder.SetMultisamples(m_multisamples, false);
  gpbuilder.SetDepthState(true, true, (depth_test != 0) ? VK_COMPARE_OP_GREATER_OR_EQUAL : VK_COMPARE_OP_ALWAYS);

  slot = gpbuilder.Create(device, pipeline_cache, false);
  vkDestroyShaderModule(device, fs, nullptr);
  return slot;
}

VkPipeline GPU_HW_Vulkan::GetVRAMUpdateDepthPipeline()
{
  if (m_vram_update_depth_pipeline != VK_NULL_HANDLE)
    return m_vram_update_depth_pipeline;

  VkShaderModule vs = GetFullscreenQuadVertexShader();
  if (vs == VK_NULL_HANDLE)
    return VK_NULL_HANDLE;

  // Pre-baked path: two blobs handle the structural MSAA split
  // (sampler2D vs sampler2DMS). No spec constants - the body is just
  // "copy alpha to depth" with the only variance in the sampler type.
  // The MSAA blob writes gl_FragDepth from texelFetch with gl_SampleID,
  // which forces per-sample shading per the Vulkan spec regardless of
  // the SetMultisamples per_sample_shading flag below.
  const bool      msaa     = (m_multisamples > 1);
  const uint32_t* spv      = msaa ? Vulkan::EmbeddedShaders::k_vram_update_depth_msaa_fs
                                  : Vulkan::EmbeddedShaders::k_vram_update_depth_fs;
  const size_t    spv_size = msaa ? Vulkan::EmbeddedShaders::k_vram_update_depth_msaa_fs_size_bytes
                                  : Vulkan::EmbeddedShaders::k_vram_update_depth_fs_size_bytes;

  VkShaderModule fs = Vulkan::EmbeddedShaders::CreateShaderModule(spv, spv_size);
  if (fs == VK_NULL_HANDLE)
    return VK_NULL_HANDLE;

  VkDevice device = g_vulkan_context->GetDevice();
  VkPipelineCache pipeline_cache = g_vulkan_shader_cache->GetPipelineCache();

  Vulkan::GraphicsPipelineBuilder gpbuilder;
  gpbuilder.SetRenderPass(m_vram_update_depth_render_pass, 0);
  gpbuilder.SetPipelineLayout(m_single_sampler_pipeline_layout);
  gpbuilder.SetPrimitiveTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
  gpbuilder.SetNoCullRasterizationState();
  gpbuilder.SetDynamicViewportAndScissorState();
  gpbuilder.SetVertexShader(vs);
  gpbuilder.SetFragmentShader(fs);
  gpbuilder.SetMultisamples(m_multisamples, false);
  gpbuilder.SetDepthState(true, true, VK_COMPARE_OP_ALWAYS);
  gpbuilder.SetBlendAttachment(0, false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD,
                               VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, 0);

  m_vram_update_depth_pipeline = gpbuilder.Create(device, pipeline_cache, false);
  vkDestroyShaderModule(device, fs, nullptr);
  return m_vram_update_depth_pipeline;
}

VkPipeline GPU_HW_Vulkan::GetVRAMReadbackPipeline()
{
  if (m_vram_readback_pipeline != VK_NULL_HANDLE)
    return m_vram_readback_pipeline;

  VkShaderModule vs = GetFullscreenQuadVertexShader();
  if (vs == VK_NULL_HANDLE)
    return VK_NULL_HANDLE;

  // Pre-baked path: two blobs handle the structural MSAA split
  // (sampler2D vs sampler2DMS); RESOLUTION_SCALE / MULTISAMPLES are
  // session-constant common-knob spec constants.
  const bool      msaa     = (m_multisamples > 1);
  const uint32_t* spv      = msaa ? Vulkan::EmbeddedShaders::k_vram_readback_msaa_fs
                                  : Vulkan::EmbeddedShaders::k_vram_readback_fs;
  const size_t    spv_size = msaa ? Vulkan::EmbeddedShaders::k_vram_readback_msaa_fs_size_bytes
                                  : Vulkan::EmbeddedShaders::k_vram_readback_fs_size_bytes;

  VkShaderModule fs = Vulkan::EmbeddedShaders::CreateShaderModule(spv, spv_size);
  if (fs == VK_NULL_HANDLE)
    return VK_NULL_HANDLE;

  Vulkan::SpecConstants fs_spec;
  fs_spec.AddUInt(0, static_cast<uint32_t>(m_resolution_scale));   // RESOLUTION_SCALE
  if (msaa)
    fs_spec.AddUInt(1, static_cast<uint32_t>(m_multisamples));     // MULTISAMPLES

  VkDevice device = g_vulkan_context->GetDevice();
  VkPipelineCache pipeline_cache = g_vulkan_shader_cache->GetPipelineCache();

  Vulkan::GraphicsPipelineBuilder gpbuilder;
  gpbuilder.SetRenderPass(m_vram_readback_render_pass, 0);
  gpbuilder.SetPipelineLayout(m_single_sampler_pipeline_layout);
  gpbuilder.SetPrimitiveTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
  gpbuilder.SetVertexShader(vs);
  gpbuilder.SetFragmentShader(fs, fs_spec.GetInfo());
  gpbuilder.SetNoCullRasterizationState();
  gpbuilder.SetNoDepthTestState();
  gpbuilder.SetNoBlendingState();
  gpbuilder.SetDynamicViewportAndScissorState();

  m_vram_readback_pipeline = gpbuilder.Create(device, pipeline_cache, false);
  vkDestroyShaderModule(device, fs, nullptr);
  return m_vram_readback_pipeline;
}

VkPipeline GPU_HW_Vulkan::GetDisplayPipeline(uint8_t depth_24, uint8_t interlace_mode)
{
  VkPipeline& slot = m_display_pipelines[depth_24][interlace_mode];
  if (slot != VK_NULL_HANDLE)
    return slot;

  VkShaderModule vs = GetFullscreenQuadVertexShader();
  if (vs == VK_NULL_HANDLE)
    return VK_NULL_HANDLE;

  // Pre-baked path: two blobs handle the structural MSAA split
  // (sampler2D vs sampler2DMS); the remaining DEPTH_24BIT,
  // INTERLACED/INTERLEAVED, SMOOTH_CHROMA combinations collapse into
  // specialisation constants. RESOLUTION_SCALE and MULTISAMPLES are
  // session-constant common-knob spec constants. The slot lookup
  // m_display_pipelines[depth_24][interlace_mode] is unchanged, so
  // each of the 6 entries gets its own spec-const-specialised
  // pipeline at first touch.
  const bool         msaa      = (m_multisamples > 1);
  const uint32_t*    spv       = msaa ? Vulkan::EmbeddedShaders::k_display_msaa_fs
                                      : Vulkan::EmbeddedShaders::k_display_fs;
  const size_t       spv_size  = msaa ? Vulkan::EmbeddedShaders::k_display_msaa_fs_size_bytes
                                      : Vulkan::EmbeddedShaders::k_display_fs_size_bytes;

  VkShaderModule fs = Vulkan::EmbeddedShaders::CreateShaderModule(spv, spv_size);
  if (fs == VK_NULL_HANDLE)
    return VK_NULL_HANDLE;

  const InterlacedRenderMode irm = static_cast<InterlacedRenderMode>(interlace_mode);
  Vulkan::SpecConstants fs_spec;
  fs_spec.AddUInt(  0, static_cast<uint32_t>(m_resolution_scale));    // RESOLUTION_SCALE
  if (msaa)
    fs_spec.AddUInt(1, static_cast<uint32_t>(m_multisamples));        // MULTISAMPLES
  fs_spec.AddBool(100, depth_24 != 0);                                // DEPTH_24BIT
  fs_spec.AddBool(101, irm != InterlacedRenderMode::None);            // INTERLACED
  fs_spec.AddBool(102, irm == InterlacedRenderMode::InterleavedFields);// INTERLEAVED
  fs_spec.AddBool(103, static_cast<bool>(m_chroma_smoothing));        // SMOOTH_CHROMA

  VkDevice device = g_vulkan_context->GetDevice();
  VkPipelineCache pipeline_cache = g_vulkan_shader_cache->GetPipelineCache();

  Vulkan::GraphicsPipelineBuilder gpbuilder;
  gpbuilder.SetRenderPass(m_display_load_render_pass, 0);
  gpbuilder.SetPipelineLayout(m_single_sampler_pipeline_layout);
  gpbuilder.SetPrimitiveTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
  gpbuilder.SetVertexShader(vs);
  gpbuilder.SetFragmentShader(fs, fs_spec.GetInfo());
  gpbuilder.SetNoCullRasterizationState();
  gpbuilder.SetNoDepthTestState();
  gpbuilder.SetNoBlendingState();
  gpbuilder.SetDynamicViewportAndScissorState();

  slot = gpbuilder.Create(device, pipeline_cache, false);
  vkDestroyShaderModule(device, fs, nullptr);
  return slot;
}

// First-pass downsample. Adaptive uses the mip-generation shader,
// Box uses the box-sample shader. Always uses uv_quad VS for
// Adaptive (which samples a UV-quad input) and fullscreen-quad VS
// for Box (full-screen blit-style downsample). The mode is read
// once from m_downsample_mode; toggling the setting triggers
// UpdateSettings -> DestroyPipelines so the slot is cleared.
VkPipeline GPU_HW_Vulkan::GetDownsampleFirstPassPipeline()
{
  if (m_downsample_first_pass_pipeline != VK_NULL_HANDLE)
    return m_downsample_first_pass_pipeline;

  // Both downsample modes are now pre-baked (Adaptive: mip FS with
  // FIRST_PASS=true at constant_id=100; Box: box-sample FS with
  // RESOLUTION_SCALE at constant_id=0). m_shadergen is not needed here.

  VkDevice device = g_vulkan_context->GetDevice();
  VkPipelineCache pipeline_cache = g_vulkan_shader_cache->GetPipelineCache();

  if (m_downsample_mode == GPUDownsampleMode::Adaptive)
  {
    VkShaderModule vs = GetUVQuadVertexShader();
    if (vs == VK_NULL_HANDLE)
      return VK_NULL_HANDLE;
    VkShaderModule fs = Vulkan::EmbeddedShaders::CreateShaderModule(
      Vulkan::EmbeddedShaders::k_adaptive_downsample_mip_fs,
      Vulkan::EmbeddedShaders::k_adaptive_downsample_mip_fs_size_bytes);
    if (fs == VK_NULL_HANDLE)
      return VK_NULL_HANDLE;

    Vulkan::SpecConstants fs_spec;
    fs_spec.AddBool(100, /*FIRST_PASS=*/true);

    Vulkan::GraphicsPipelineBuilder gpbuilder;
    gpbuilder.SetRenderPass(m_downsample_render_pass, 0);
    gpbuilder.SetPipelineLayout(m_downsample_pipeline_layout);
    gpbuilder.SetPrimitiveTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    gpbuilder.SetVertexShader(vs);
    gpbuilder.SetFragmentShader(fs, fs_spec.GetInfo());
    gpbuilder.SetNoCullRasterizationState();
    gpbuilder.SetNoDepthTestState();
    gpbuilder.SetNoBlendingState();
    gpbuilder.SetDynamicViewportAndScissorState();

    m_downsample_first_pass_pipeline = gpbuilder.Create(device, pipeline_cache, false);
    vkDestroyShaderModule(device, fs, nullptr);
  }
  else if (m_downsample_mode == GPUDownsampleMode::Box)
  {
    // Pre-baked path. RESOLUTION_SCALE is supplied as a spec constant
    // at constant_id=0.
    VkShaderModule vs = GetFullscreenQuadVertexShader();
    if (vs == VK_NULL_HANDLE)
      return VK_NULL_HANDLE;
    VkShaderModule fs = Vulkan::EmbeddedShaders::CreateShaderModule(
      Vulkan::EmbeddedShaders::k_box_sample_downsample_fs,
      Vulkan::EmbeddedShaders::k_box_sample_downsample_fs_size_bytes);
    if (fs == VK_NULL_HANDLE)
      return VK_NULL_HANDLE;

    Vulkan::SpecConstants fs_spec;
    fs_spec.AddUInt(0, m_resolution_scale);

    Vulkan::GraphicsPipelineBuilder gpbuilder;
    gpbuilder.SetRenderPass(m_downsample_render_pass, 0);
    gpbuilder.SetPipelineLayout(m_single_sampler_pipeline_layout);
    gpbuilder.SetPrimitiveTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    gpbuilder.SetVertexShader(vs);
    gpbuilder.SetFragmentShader(fs, fs_spec.GetInfo());
    gpbuilder.SetNoCullRasterizationState();
    gpbuilder.SetNoDepthTestState();
    gpbuilder.SetNoBlendingState();
    gpbuilder.SetDynamicViewportAndScissorState();

    m_downsample_first_pass_pipeline = gpbuilder.Create(device, pipeline_cache, false);
    vkDestroyShaderModule(device, fs, nullptr);
  }

  return m_downsample_first_pass_pipeline;
}

VkPipeline GPU_HW_Vulkan::GetDownsampleMidPassPipeline()
{
  if (m_downsample_mid_pass_pipeline != VK_NULL_HANDLE)
    return m_downsample_mid_pass_pipeline;
  if (m_downsample_mode != GPUDownsampleMode::Adaptive)
    return VK_NULL_HANDLE;

  // Pre-baked path: same SPIR-V blob as the Adaptive first-pass, but with
  // FIRST_PASS=false plumbed through VkSpecializationInfo. No m_shadergen
  // dependency.
  VkShaderModule vs = GetUVQuadVertexShader();
  if (vs == VK_NULL_HANDLE)
    return VK_NULL_HANDLE;
  VkShaderModule fs = Vulkan::EmbeddedShaders::CreateShaderModule(
    Vulkan::EmbeddedShaders::k_adaptive_downsample_mip_fs,
    Vulkan::EmbeddedShaders::k_adaptive_downsample_mip_fs_size_bytes);
  if (fs == VK_NULL_HANDLE)
    return VK_NULL_HANDLE;

  Vulkan::SpecConstants fs_spec;
  fs_spec.AddBool(100, /*FIRST_PASS=*/false);

  VkDevice device = g_vulkan_context->GetDevice();
  VkPipelineCache pipeline_cache = g_vulkan_shader_cache->GetPipelineCache();

  Vulkan::GraphicsPipelineBuilder gpbuilder;
  gpbuilder.SetRenderPass(m_downsample_render_pass, 0);
  gpbuilder.SetPipelineLayout(m_downsample_pipeline_layout);
  gpbuilder.SetPrimitiveTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
  gpbuilder.SetVertexShader(vs);
  gpbuilder.SetFragmentShader(fs, fs_spec.GetInfo());
  gpbuilder.SetNoCullRasterizationState();
  gpbuilder.SetNoDepthTestState();
  gpbuilder.SetNoBlendingState();
  gpbuilder.SetDynamicViewportAndScissorState();

  m_downsample_mid_pass_pipeline = gpbuilder.Create(device, pipeline_cache, false);
  vkDestroyShaderModule(device, fs, nullptr);
  return m_downsample_mid_pass_pipeline;
}

VkPipeline GPU_HW_Vulkan::GetDownsampleBlurPassPipeline()
{
  if (m_downsample_blur_pass_pipeline != VK_NULL_HANDLE)
    return m_downsample_blur_pass_pipeline;
  if (m_downsample_mode != GPUDownsampleMode::Adaptive)
    return VK_NULL_HANDLE;

  // Pre-baked path: no m_shadergen needed.
  VkShaderModule vs = GetUVQuadVertexShader();
  if (vs == VK_NULL_HANDLE)
    return VK_NULL_HANDLE;
  VkShaderModule fs = Vulkan::EmbeddedShaders::CreateShaderModule(
    Vulkan::EmbeddedShaders::k_adaptive_downsample_blur_fs,
    Vulkan::EmbeddedShaders::k_adaptive_downsample_blur_fs_size_bytes);
  if (fs == VK_NULL_HANDLE)
    return VK_NULL_HANDLE;

  VkDevice device = g_vulkan_context->GetDevice();
  VkPipelineCache pipeline_cache = g_vulkan_shader_cache->GetPipelineCache();

  Vulkan::GraphicsPipelineBuilder gpbuilder;
  gpbuilder.SetRenderPass(m_downsample_weight_render_pass, 0);
  gpbuilder.SetPipelineLayout(m_downsample_pipeline_layout);
  gpbuilder.SetPrimitiveTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
  gpbuilder.SetVertexShader(vs);
  gpbuilder.SetFragmentShader(fs);
  gpbuilder.SetNoCullRasterizationState();
  gpbuilder.SetNoDepthTestState();
  gpbuilder.SetNoBlendingState();
  gpbuilder.SetDynamicViewportAndScissorState();

  m_downsample_blur_pass_pipeline = gpbuilder.Create(device, pipeline_cache, false);
  vkDestroyShaderModule(device, fs, nullptr);
  return m_downsample_blur_pass_pipeline;
}

VkPipeline GPU_HW_Vulkan::GetDownsampleCompositePassPipeline()
{
  if (m_downsample_composite_pass_pipeline != VK_NULL_HANDLE)
    return m_downsample_composite_pass_pipeline;
  if (m_downsample_mode != GPUDownsampleMode::Adaptive)
    return VK_NULL_HANDLE;

  // Pre-baked path. No m_shadergen needed; RESOLUTION_SCALE is supplied
  // as a spec constant at constant_id=0.
  VkShaderModule vs = GetUVQuadVertexShader();
  if (vs == VK_NULL_HANDLE)
    return VK_NULL_HANDLE;
  VkShaderModule fs = Vulkan::EmbeddedShaders::CreateShaderModule(
    Vulkan::EmbeddedShaders::k_adaptive_downsample_composite_fs,
    Vulkan::EmbeddedShaders::k_adaptive_downsample_composite_fs_size_bytes);
  if (fs == VK_NULL_HANDLE)
    return VK_NULL_HANDLE;

  Vulkan::SpecConstants fs_spec;
  fs_spec.AddUInt(0, m_resolution_scale);

  VkDevice device = g_vulkan_context->GetDevice();
  VkPipelineCache pipeline_cache = g_vulkan_shader_cache->GetPipelineCache();

  Vulkan::GraphicsPipelineBuilder gpbuilder;
  gpbuilder.SetRenderPass(m_display_load_render_pass, 0);
  gpbuilder.SetPipelineLayout(m_downsample_composite_pipeline_layout);
  gpbuilder.SetPrimitiveTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
  gpbuilder.SetVertexShader(vs);
  gpbuilder.SetFragmentShader(fs, fs_spec.GetInfo());
  gpbuilder.SetNoCullRasterizationState();
  gpbuilder.SetNoDepthTestState();
  gpbuilder.SetNoBlendingState();
  gpbuilder.SetDynamicViewportAndScissorState();

  m_downsample_composite_pass_pipeline = gpbuilder.Create(device, pipeline_cache, false);
  vkDestroyShaderModule(device, fs, nullptr);
  return m_downsample_composite_pass_pipeline;
}

void GPU_HW_Vulkan::DestroyPipelines()
{
  // Tear down the background-compile worker before destroying the
  // matrix - the worker writes into m_batch_fragment_shaders and
  // m_batch_pipelines, both of which we're about to enumerate-destroy.
  StopShaderCompileThread();

  // Atomic slot teardown. By this point the worker is stopped and
  // no other thread is touching the arrays.
  VkDevice device = g_vulkan_context->GetDevice();
  m_batch_pipelines.enumerate([device](AtomicSlot<VkPipeline>& slot) {
    const VkPipeline p = slot.Exchange(VK_NULL_HANDLE);
    if (p != VK_NULL_HANDLE)
      vkDestroyPipeline(device, p, nullptr);
  });
  const auto destroy_module = [device](AtomicSlot<VkShaderModule>& slot) {
    const VkShaderModule m = slot.Exchange(VK_NULL_HANDLE);
    if (m != VK_NULL_HANDLE)
      vkDestroyShaderModule(device, m, nullptr);
  };
  m_batch_fragment_shaders.enumerate(destroy_module);
  m_batch_vertex_shaders.enumerate(destroy_module);

  m_vram_fill_pipelines.enumerate(Vulkan::Util::SafeDestroyPipeline);

  for (VkPipeline& p : m_vram_write_pipelines)
    Vulkan::Util::SafeDestroyPipeline(p);

  for (VkPipeline& p : m_vram_copy_pipelines)
    Vulkan::Util::SafeDestroyPipeline(p);

  Vulkan::Util::SafeDestroyPipeline(m_vram_readback_pipeline);
  Vulkan::Util::SafeDestroyPipeline(m_vram_update_depth_pipeline);

  DestroyDownsamplePipelines();

  // Cached shared vertex shaders for the non-batch helpers. Reset
  // here so the next CompilePipelines pass picks up any shadergen
  // changes (e.g. resolution_scale, true_color, etc. affect generated
  // GLSL). Vulkan::Util doesn't expose a SafeDestroyShaderModule
  // helper, so do it manually.
  if (m_fullscreen_quad_vertex_shader != VK_NULL_HANDLE)
  {
    vkDestroyShaderModule(device, m_fullscreen_quad_vertex_shader, nullptr);
    m_fullscreen_quad_vertex_shader = VK_NULL_HANDLE;
  }
  if (m_uv_quad_vertex_shader != VK_NULL_HANDLE)
  {
    vkDestroyShaderModule(device, m_uv_quad_vertex_shader, nullptr);
    m_uv_quad_vertex_shader = VK_NULL_HANDLE;
  }

  m_display_pipelines.enumerate(Vulkan::Util::SafeDestroyPipeline);
}

void GPU_HW_Vulkan::DrawBatchVertices(BatchRenderMode render_mode, uint32_t base_vertex, uint32_t num_vertices)
{
  BeginVRAMRenderPass();

  VkCommandBuffer cmdbuf = g_vulkan_context->GetCurrentCommandBuffer();

  // Fetch the batch PSO via the lazy helper. Behaviour matches the
  // D3D11 / D3D12 backends: Enabled mode pre-fills every slot at
  // CompilePipelines time so this is a fast mutex-protected pointer
  // load, Lazy mode either grabs the worker's result or compiles on
  // the main thread on race, Disabled compiles on every first use.
  //
  // [filter][true_color][scaled_dithering][depth_test][render_mode][texture_mode][transparency_mode][dithering][interlacing]
  const uint8_t depth_test = m_batch.use_depth_buffer ? static_cast<uint8_t>(2) : static_cast<uint8_t>(m_batch.check_mask_before_draw);
  VkPipeline pipeline =
    GetBatchPipeline(m_texture_filtering, m_true_color, m_scaled_dithering,
                     depth_test, static_cast<uint8_t>(render_mode),
                     static_cast<uint8_t>(m_batch.texture_mode),
                     static_cast<uint8_t>(m_batch.transparency_mode), m_batch.dithering, m_batch.interlacing, false);

  vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
  vkCmdDraw(cmdbuf, num_vertices, 1, base_vertex, 0);
}

void GPU_HW_Vulkan::SetScissorFromDrawingArea()
{
  int left, top, right, bottom;
  CalcScissorRect(&left, &top, &right, &bottom);
  Vulkan::Util::SetScissor(g_vulkan_context->GetCurrentCommandBuffer(), left, top, right - left, bottom - top);
}

void GPU_HW_Vulkan::ClearDisplay()
{
  GPU_HW::ClearDisplay();
  EndRenderPass();

  m_host_display->ClearDisplayTexture();

  VkCommandBuffer cmdbuf = g_vulkan_context->GetCurrentCommandBuffer();
  m_display_texture.TransitionToLayout(cmdbuf, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

  static const VkClearColorValue cc = {{0.0f, 0.0f, 0.0f, 1.0f}};
  static const VkImageSubresourceRange srr = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vkCmdClearColorImage(cmdbuf, m_display_texture.GetImage(), m_display_texture.GetLayout(), &cc, 1, &srr);
}

void GPU_HW_Vulkan::UpdateDisplay()
{
  GPU_HW::UpdateDisplay();
  EndRenderPass();

  VkCommandBuffer cmdbuf = g_vulkan_context->GetCurrentCommandBuffer();

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
        m_host_display->SetDisplayTexture(&m_vram_texture, HostDisplayPixelFormat::RGBA8, m_vram_texture.GetWidth(),
                                          m_vram_texture.GetHeight(), scaled_vram_offset_x, scaled_vram_offset_y,
                                          scaled_display_width, scaled_display_height);
      }
    }
    else
    {
      EndRenderPass();

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

      m_display_texture.TransitionToLayout(cmdbuf, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
      m_vram_texture.TransitionToLayout(cmdbuf, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

      BeginRenderPass((interlaced != InterlacedRenderMode::None) ? m_display_load_render_pass :
                                                                   m_display_discard_render_pass,
                      m_display_framebuffer, 0, 0, scaled_display_width, scaled_display_height);

      vkCmdBindPipeline(
        cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
        GetDisplayPipeline(static_cast<uint8_t>(m_GPUSTAT.display_area_color_depth_24),
                           static_cast<uint8_t>(interlaced)));
      vkCmdPushConstants(cmdbuf, m_single_sampler_pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(uniforms),
                         uniforms);
      vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_single_sampler_pipeline_layout, 0, 1,
                              &m_vram_read_descriptor_set, 0, nullptr);
      Vulkan::Util::SetViewportAndScissor(cmdbuf, 0, 0, scaled_display_width, scaled_display_height);

      vkCmdDraw(cmdbuf, 3, 1, 0, 0);

      EndRenderPass();

      m_vram_texture.TransitionToLayout(cmdbuf, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
      m_display_texture.TransitionToLayout(cmdbuf, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

      if (IsUsingDownsampling())
      {
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

void GPU_HW_Vulkan::ReadVRAM(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
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

  EndRenderPass();

  VkCommandBuffer cmdbuf = g_vulkan_context->GetCurrentCommandBuffer();

  m_vram_texture.TransitionToLayout(cmdbuf, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  m_vram_readback_texture.TransitionToLayout(cmdbuf, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

  // Work around Mali driver bug: set full framebuffer size for render area. The GPU crashes with a page fault if we use
  // the actual size we're rendering to...
  const uint32_t rp_width = std::max<uint32_t>(16, encoded_width);
  const uint32_t rp_height = std::max<uint32_t>(16, encoded_height);
  BeginRenderPass(m_vram_readback_render_pass, m_vram_readback_framebuffer, 0, 0, rp_width, rp_height);

  // Encode the 24-bit texture as 16-bit.
  // 6 DWORDs to match the post-RESOLUTION_SCALE-refactor vram_read_ps
  // cbuffer (u_base_coords.xy, u_size.xy, u_resolution_scale, u_pad0).
  const uint32_t uniforms[6] = {copy_rect.left, copy_rect.top, copy_rect.GetWidth(), copy_rect.GetHeight(),
                                m_resolution_scale, 0u /* u_pad0 */};
  vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, GetVRAMReadbackPipeline());
  vkCmdPushConstants(cmdbuf, m_single_sampler_pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(uniforms),
                     uniforms);
  vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_single_sampler_pipeline_layout, 0, 1,
                          &m_vram_read_descriptor_set, 0, nullptr);
  Vulkan::Util::SetViewportAndScissor(cmdbuf, 0, 0, encoded_width, encoded_height);
  vkCmdDraw(cmdbuf, 3, 1, 0, 0);
  EndRenderPass();

  m_vram_readback_texture.TransitionToLayout(cmdbuf, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  m_vram_texture.TransitionToLayout(cmdbuf, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

  // Stage the readback.
  m_vram_readback_staging_texture.CopyFromTexture(m_vram_readback_texture, 0, 0, 0, 0, 0, 0, encoded_width,
                                                  encoded_height);

  // And copy it into our shadow buffer (will execute command buffer and stall).
  ExecuteCommandBuffer(true, true);
  m_vram_readback_staging_texture.ReadTexels(0, 0, encoded_width, encoded_height,
                                             &m_vram_shadow[copy_rect.top * VRAM_WIDTH + copy_rect.left],
                                             VRAM_WIDTH * sizeof(uint16_t));
}

void GPU_HW_Vulkan::FillVRAM(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color)
{
  if (IsUsingSoftwareRendererForReadbacks())
    FillSoftwareRendererVRAM(x, y, width, height, color);

  GPU_HW::FillVRAM(x, y, width, height, color);

  BeginVRAMRenderPass();

  VkCommandBuffer cmdbuf = g_vulkan_context->GetCurrentCommandBuffer();

  const VRAMFillUBOData uniforms = GetVRAMFillUBOData(x, y, width, height, color);
  vkCmdPushConstants(cmdbuf, m_no_samplers_pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(uniforms),
                     &uniforms);
  vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    GetVRAMFillPipeline(static_cast<uint8_t>(IsVRAMFillOversized(x, y, width, height)),
                                        static_cast<uint8_t>(IsInterlacedRenderingEnabled())));

  const Common::Rectangle<uint32_t> bounds(GetVRAMTransferBounds(x, y, width, height));
  Vulkan::Util::SetViewportAndScissor(cmdbuf, bounds.left * m_resolution_scale, bounds.top * m_resolution_scale,
                                      bounds.GetWidth() * m_resolution_scale, bounds.GetHeight() * m_resolution_scale);
  vkCmdDraw(cmdbuf, 3, 1, 0, 0);

  RestoreGraphicsAPIState();
}

void GPU_HW_Vulkan::UpdateVRAM(uint32_t x, uint32_t y, uint32_t width, uint32_t height, const void* data, bool set_mask, bool check_mask)
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
  const uint32_t alignment = std::max<uint32_t>(sizeof(uint32_t), static_cast<uint32_t>(m_use_ssbos_for_vram_writes ?
                                                                      g_vulkan_context->GetStorageBufferAlignment() :
                                                                      g_vulkan_context->GetTexelBufferAlignment()));
  if (!m_texture_stream_buffer.ReserveMemory(data_size, alignment))
  {
    ExecuteCommandBuffer(false, true);
    if (!m_texture_stream_buffer.ReserveMemory(data_size, alignment))
      return;
  }

  const uint32_t start_index = m_texture_stream_buffer.GetCurrentOffset() / sizeof(uint16_t);
  std::memcpy(m_texture_stream_buffer.GetCurrentHostPointer(), data, data_size);
  m_texture_stream_buffer.CommitMemory(data_size);

  VkCommandBuffer cmdbuf = g_vulkan_context->GetCurrentCommandBuffer();

  BeginVRAMRenderPass();

  const VRAMWriteUBOData uniforms = GetVRAMWriteUBOData(x, y, width, height, start_index, set_mask, check_mask);
  vkCmdPushConstants(cmdbuf, m_vram_write_pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(uniforms),
                     &uniforms);
  vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    GetVRAMWritePipeline(static_cast<uint8_t>(check_mask && !m_pgxp_depth_buffer)));
  vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_vram_write_pipeline_layout, 0, 1,
                          &m_vram_write_descriptor_set, 0, nullptr);

  // the viewport should already be set to the full vram, so just adjust the scissor
  const Common::Rectangle<uint32_t> scaled_bounds = bounds * m_resolution_scale;
  Vulkan::Util::SetScissor(cmdbuf, scaled_bounds.left, scaled_bounds.top, scaled_bounds.GetWidth(),
                           scaled_bounds.GetHeight());
  vkCmdDraw(cmdbuf, 3, 1, 0, 0);

  RestoreGraphicsAPIState();
}

void GPU_HW_Vulkan::CopyVRAM(uint32_t src_x, uint32_t src_y, uint32_t dst_x, uint32_t dst_y, uint32_t width, uint32_t height)
{
  VkCommandBuffer cmdbuf = g_vulkan_context->GetCurrentCommandBuffer();
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

    BeginVRAMRenderPass();

    vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      GetVRAMCopyPipeline(static_cast<uint8_t>(m_GPUSTAT.check_mask_before_draw && !m_pgxp_depth_buffer)));
    vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_single_sampler_pipeline_layout, 0, 1,
                            &m_vram_copy_descriptor_set, 0, nullptr);
    vkCmdPushConstants(cmdbuf, m_single_sampler_pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(uniforms),
                       &uniforms);
    Vulkan::Util::SetViewportAndScissor(cmdbuf, dst_bounds_scaled.left, dst_bounds_scaled.top,
                                        dst_bounds_scaled.GetWidth(), dst_bounds_scaled.GetHeight());
    vkCmdDraw(cmdbuf, 3, 1, 0, 0);
    RestoreGraphicsAPIState();

    if (m_GPUSTAT.check_mask_before_draw)
      m_current_depth++;

    return;
  }

  GPU_HW::CopyVRAM(src_x, src_y, dst_x, dst_y, width, height);

  src_x *= m_resolution_scale;
  src_y *= m_resolution_scale;
  dst_x *= m_resolution_scale;
  dst_y *= m_resolution_scale;
  width *= m_resolution_scale;
  height *= m_resolution_scale;

  EndRenderPass();

  m_vram_texture.TransitionToLayout(cmdbuf, VK_IMAGE_LAYOUT_GENERAL);

  const VkImageCopy ic{{VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u},
                       {static_cast<int32_t>(src_x), static_cast<int32_t>(src_y), 0},
                       {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u},
                       {static_cast<int32_t>(dst_x), static_cast<int32_t>(dst_y), 0},
                       {width, height, 1u}};
  vkCmdCopyImage(cmdbuf, m_vram_texture.GetImage(), m_vram_texture.GetLayout(), m_vram_texture.GetImage(),
                 m_vram_texture.GetLayout(), 1, &ic);

  m_vram_texture.TransitionToLayout(cmdbuf, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
}

void GPU_HW_Vulkan::UpdateVRAMReadTexture()
{
  EndRenderPass();

  VkCommandBuffer cmdbuf = g_vulkan_context->GetCurrentCommandBuffer();
  m_vram_texture.TransitionToLayout(cmdbuf, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  m_vram_read_texture.TransitionToLayout(cmdbuf, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

  const auto scaled_rect = m_vram_dirty_rect * m_resolution_scale;

  if (m_vram_texture.GetSamples() > VK_SAMPLE_COUNT_1_BIT)
  {
    const VkImageResolve resolve{{VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u},
                                 {static_cast<int32_t>(scaled_rect.left), static_cast<int32_t>(scaled_rect.top), 0},
                                 {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u},
                                 {static_cast<int32_t>(scaled_rect.left), static_cast<int32_t>(scaled_rect.top), 0},
                                 {scaled_rect.GetWidth(), scaled_rect.GetHeight(), 1u}};
    vkCmdResolveImage(cmdbuf, m_vram_texture.GetImage(), m_vram_texture.GetLayout(), m_vram_read_texture.GetImage(),
                      m_vram_read_texture.GetLayout(), 1, &resolve);
  }
  else
  {
    const VkImageCopy copy{{VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u},
                           {static_cast<int32_t>(scaled_rect.left), static_cast<int32_t>(scaled_rect.top), 0},
                           {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u},
                           {static_cast<int32_t>(scaled_rect.left), static_cast<int32_t>(scaled_rect.top), 0},
                           {scaled_rect.GetWidth(), scaled_rect.GetHeight(), 1u}};

    vkCmdCopyImage(cmdbuf, m_vram_texture.GetImage(), m_vram_texture.GetLayout(), m_vram_read_texture.GetImage(),
                   m_vram_read_texture.GetLayout(), 1u, &copy);
  }

  m_vram_read_texture.TransitionToLayout(cmdbuf, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  m_vram_texture.TransitionToLayout(cmdbuf, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
  GPU_HW::UpdateVRAMReadTexture();
}

void GPU_HW_Vulkan::UpdateDepthBufferFromMaskBit()
{
  if (m_pgxp_depth_buffer)
    return;

  EndRenderPass();
  VkCommandBuffer cmdbuf = g_vulkan_context->GetCurrentCommandBuffer();
  m_vram_texture.TransitionToLayout(cmdbuf, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

  BeginRenderPass(m_vram_update_depth_render_pass, m_vram_update_depth_framebuffer, 0, 0, m_vram_texture.GetWidth(),
                  m_vram_texture.GetHeight());

  vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, GetVRAMUpdateDepthPipeline());
  vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_single_sampler_pipeline_layout, 0, 1,
                          &m_vram_read_descriptor_set, 0, nullptr);
  Vulkan::Util::SetViewportAndScissor(cmdbuf, 0, 0, m_vram_texture.GetWidth(), m_vram_texture.GetHeight());
  vkCmdDraw(cmdbuf, 3, 1, 0, 0);

  EndRenderPass();

  m_vram_texture.TransitionToLayout(cmdbuf, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

  RestoreGraphicsAPIState();
}

void GPU_HW_Vulkan::ClearDepthBuffer()
{
  EndRenderPass();

  VkCommandBuffer cmdbuf = g_vulkan_context->GetCurrentCommandBuffer();
  m_vram_depth_texture.TransitionToLayout(cmdbuf, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

  static const VkClearDepthStencilValue cds = {1.0f, 0};
  static constexpr VkImageSubresourceRange dsrr = {VK_IMAGE_ASPECT_DEPTH_BIT, 0u, 1u, 0u, 1u};
  vkCmdClearDepthStencilImage(cmdbuf, m_vram_depth_texture.GetImage(), m_vram_depth_texture.GetLayout(), &cds, 1u,
                              &dsrr);

  m_vram_depth_texture.TransitionToLayout(cmdbuf, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
  m_last_depth_z = 1.0f;
}

bool GPU_HW_Vulkan::CreateTextureReplacementStreamBuffer()
{
  if (m_texture_replacment_stream_buffer.IsValid())
    return true;

  if (!m_texture_replacment_stream_buffer.Create(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, TEXTURE_REPLACEMENT_BUFFER_SIZE))
    return false;

  return true;
}

bool GPU_HW_Vulkan::BlitVRAMReplacementTexture(const TextureReplacementTexture* tex, uint32_t dst_x, uint32_t dst_y, uint32_t width,
                                               uint32_t height)
{
  VkCommandBuffer cmdbuf = g_vulkan_context->GetCurrentCommandBuffer();
  if (!CreateTextureReplacementStreamBuffer())
    return false;

  if (m_vram_write_replacement_texture.GetWidth() < tex->GetWidth() ||
      m_vram_write_replacement_texture.GetHeight() < tex->GetHeight())
  {
    if (!m_vram_write_replacement_texture.Create(tex->GetWidth(), tex->GetHeight(), 1, 1, VK_FORMAT_R8G8B8A8_UNORM,
                                                 VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
                                                 VK_IMAGE_USAGE_TRANSFER_DST_BIT))
      return false;
  }

  const uint32_t required_size = tex->GetWidth() * tex->GetHeight() * sizeof(uint32_t);
  const uint32_t alignment = static_cast<uint32_t>(g_vulkan_context->GetBufferImageGranularity());
  if (!m_texture_replacment_stream_buffer.ReserveMemory(required_size, alignment))
  {
    ExecuteCommandBuffer(false, true);
    if (!m_texture_replacment_stream_buffer.ReserveMemory(required_size, alignment))
      return false;
  }

  // upload to buffer
  const uint32_t buffer_offset = m_texture_replacment_stream_buffer.GetCurrentOffset();
  std::memcpy(m_texture_replacment_stream_buffer.GetCurrentHostPointer(), tex->GetPixels(), required_size);
  m_texture_replacment_stream_buffer.CommitMemory(required_size);

  // buffer -> texture
  m_vram_write_replacement_texture.UpdateFromBuffer(cmdbuf, 0, 0, 0, 0, tex->GetWidth(), tex->GetHeight(),
                                                    m_texture_replacment_stream_buffer.GetBuffer(), buffer_offset,
						    tex->GetWidth());

  // texture -> vram
  const VkImageBlit blit = {
    {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u},
    {
      {0, 0, 0},
      {static_cast<int32_t>(tex->GetWidth()), static_cast<int32_t>(tex->GetHeight()), 1},
    },
    {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u},
    {{static_cast<int32_t>(dst_x), static_cast<int32_t>(dst_y), 0},
     {static_cast<int32_t>(dst_x + width), static_cast<int32_t>(dst_y + height), 1}},
  };
  m_vram_write_replacement_texture.TransitionToLayout(cmdbuf, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  m_vram_texture.TransitionToLayout(cmdbuf, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  vkCmdBlitImage(cmdbuf, m_vram_write_replacement_texture.GetImage(), m_vram_write_replacement_texture.GetLayout(),
                 m_vram_texture.GetImage(), m_vram_texture.GetLayout(), 1, &blit, VK_FILTER_LINEAR);
  m_vram_texture.TransitionToLayout(cmdbuf, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
  return true;
}

void GPU_HW_Vulkan::DownsampleFramebuffer(Vulkan::Texture& source, uint32_t left, uint32_t top, uint32_t width, uint32_t height)
{
  if (m_downsample_mode == GPUDownsampleMode::Adaptive)
    DownsampleFramebufferAdaptive(source, left, top, width, height);
  else
    DownsampleFramebufferBoxFilter(source, left, top, width, height);
}

void GPU_HW_Vulkan::DownsampleFramebufferBoxFilter(Vulkan::Texture& source, uint32_t left, uint32_t top, uint32_t width, uint32_t height)
{
  VkCommandBuffer cmdbuf = g_vulkan_context->GetCurrentCommandBuffer();
  source.TransitionToLayout(cmdbuf, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  m_downsample_texture.TransitionToLayout(cmdbuf, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

  VkDescriptorSet ds = (&source == &m_vram_texture) ? m_vram_read_descriptor_set : m_display_descriptor_set;

  const uint32_t ds_left = left / m_resolution_scale;
  const uint32_t ds_top = top / m_resolution_scale;
  const uint32_t ds_width = width / m_resolution_scale;
  const uint32_t ds_height = height / m_resolution_scale;

  static constexpr VkClearValue clear_color = {};
  BeginRenderPass(m_downsample_render_pass, m_downsample_mip_views[0].framebuffer, ds_left, ds_top, ds_width, ds_height,
                  &clear_color);
  Vulkan::Util::SetViewportAndScissor(cmdbuf, ds_left, ds_top, ds_width, ds_height);
  vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, GetDownsampleFirstPassPipeline());
  vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_single_sampler_pipeline_layout, 0, 1, &ds, 0,
                          nullptr);
  vkCmdDraw(cmdbuf, 3, 1, 0, 0);
  EndRenderPass();

  m_downsample_texture.TransitionToLayout(cmdbuf, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

  RestoreGraphicsAPIState();

  m_host_display->SetDisplayTexture(&m_downsample_texture, HostDisplayPixelFormat::RGBA8,
                                    m_downsample_texture.GetWidth(), m_downsample_texture.GetHeight(), ds_left, ds_top,
                                    ds_width, ds_height);
}

void GPU_HW_Vulkan::DownsampleFramebufferAdaptive(Vulkan::Texture& source, uint32_t left, uint32_t top, uint32_t width, uint32_t height)
{
  const VkImageCopy copy{{VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u},
                         {static_cast<int32_t>(left), static_cast<int32_t>(top), 0},
                         {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u},
                         {static_cast<int32_t>(left), static_cast<int32_t>(top), 0},
                         {width, height, 1u}};

  VkCommandBuffer cmdbuf = g_vulkan_context->GetCurrentCommandBuffer();
  source.TransitionToLayout(cmdbuf, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  m_downsample_texture.TransitionSubresourcesToLayout(cmdbuf, 0, 1, 0, 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  vkCmdCopyImage(cmdbuf, source.GetImage(), source.GetLayout(), m_downsample_texture.GetImage(),
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

  m_downsample_texture.TransitionSubresourcesToLayout(cmdbuf, 0, 1, 0, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

  // creating mip chain
  const uint32_t levels = m_downsample_texture.GetLevels();
  for (uint32_t level = 1; level < levels; level++)
  {
    m_downsample_texture.TransitionSubresourcesToLayout(
      cmdbuf, level, 1, 0, 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    static constexpr VkClearValue clear_color = {};
    BeginRenderPass(m_downsample_render_pass, m_downsample_mip_views[level].framebuffer, 0, 0,
                    m_downsample_texture.GetMipWidth(level), m_downsample_texture.GetMipHeight(level), &clear_color);
    Vulkan::Util::SetViewportAndScissor(cmdbuf, left >> level, top >> level, width >> level, height >> level);
    vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      (level == 1) ? GetDownsampleFirstPassPipeline() : GetDownsampleMidPassPipeline());
    vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_downsample_pipeline_layout, 0, 1,
                            &m_downsample_mip_views[level - 1].descriptor_set, 0, nullptr);

    const SmoothingUBOData ubo = GetSmoothingUBO(level, left, top, width, height, m_downsample_texture.GetWidth(),
                                                 m_downsample_texture.GetHeight());
    vkCmdPushConstants(cmdbuf, m_downsample_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(ubo), &ubo);

    vkCmdDraw(cmdbuf, 3, 1, 0, 0);

    EndRenderPass();

    m_downsample_texture.TransitionSubresourcesToLayout(
      cmdbuf, level, 1, 0, 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  }

  // blur pass at lowest resolution
  {
    const uint32_t last_level = levels - 1;

    m_downsample_weight_texture.TransitionToLayout(cmdbuf, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    static constexpr VkClearValue clear_color = {};
    BeginRenderPass(m_downsample_weight_render_pass, m_downsample_weight_framebuffer, 0, 0,
                    m_downsample_texture.GetMipWidth(last_level), m_downsample_texture.GetMipHeight(last_level),
                    &clear_color);
    Vulkan::Util::SetViewportAndScissor(cmdbuf, left >> last_level, top >> last_level, width >> last_level,
                                        height >> last_level);
    vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, GetDownsampleBlurPassPipeline());
    vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_downsample_pipeline_layout, 0, 1,
                            &m_downsample_mip_views[last_level].descriptor_set, 0, nullptr);

    const SmoothingUBOData ubo = GetSmoothingUBO(last_level, left, top, width, height, m_downsample_texture.GetWidth(),
                                                 m_downsample_texture.GetHeight());
    vkCmdPushConstants(cmdbuf, m_downsample_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(ubo), &ubo);

    vkCmdDraw(cmdbuf, 3, 1, 0, 0);
    EndRenderPass();

    m_downsample_weight_texture.TransitionToLayout(cmdbuf, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  }

  // resolve pass
  {
    m_display_texture.TransitionToLayout(cmdbuf, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    BeginRenderPass(m_display_load_render_pass, m_display_framebuffer, left, top, width, height);
    Vulkan::Util::SetViewportAndScissor(cmdbuf, left, top, width, height);
    vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, GetDownsampleCompositePassPipeline());
    vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_downsample_composite_pipeline_layout, 0, 1,
                            &m_downsample_composite_descriptor_set, 0, nullptr);
    vkCmdDraw(cmdbuf, 3, 1, 0, 0);
    EndRenderPass();
    m_display_texture.TransitionToLayout(cmdbuf, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  }
  RestoreGraphicsAPIState();

  m_host_display->SetDisplayTexture(&m_display_texture, HostDisplayPixelFormat::RGBA8, m_display_texture.GetWidth(),
                                    m_display_texture.GetHeight(), left, top, width, height);
}

std::unique_ptr<GPU> GPU::CreateHardwareVulkanRenderer()
{
  return std::make_unique<GPU_HW_Vulkan>();
}
