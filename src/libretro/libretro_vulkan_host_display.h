#pragma once
#include "common/vulkan/staging_texture.h"
#include "common/vulkan/stream_buffer.h"
#include "common/vulkan/texture.h"
#include "core/host_display.h"
#include "libretro.h"

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

private:
  static constexpr VkFormat FRAMEBUFFER_FORMAT = VK_FORMAT_R8G8B8A8_UNORM;

  struct PushConstants
  {
    float src_rect_left;
    float src_rect_top;
    float src_rect_width;
    float src_rect_height;
  };

  void RenderDisplay(s32 left, s32 top, s32 width, s32 height, void* texture_handle, u32 texture_width,
                     s32 texture_height, s32 texture_view_x, s32 texture_view_y, s32 texture_view_width,
                     s32 texture_view_height, bool linear_filter);

  bool CheckFramebufferSize(u32 width, u32 height);

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
