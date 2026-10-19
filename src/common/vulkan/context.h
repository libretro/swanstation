// Copyright 2016 Dolphin Emulator Project
// Copyright 2020 DuckStation Emulator Project
// Licensed under GPLv2+
// Refer to the LICENSE file included.

#pragma once

#include "../types.h"
#include "vulkan_loader.h"
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Vulkan {

class Context
{
public:
  static constexpr uint32_t NUM_COMMAND_BUFFERS = 2;

  // Creates the device in place of vkCreateDevice, e.g. a libretro
  // frontend's, which adds what it needs to the create info.
  using CreateDeviceWrapper = VkDevice (*)(VkPhysicalDevice gpu, void* opaque, const VkDeviceCreateInfo* info);

  ~Context();

  // Returns a list of Vulkan-compatible GPUs.
  using GPUList = std::vector<VkPhysicalDevice>;
  static GPUList EnumerateGPUs(VkInstance instance);

  // Creates a new context from a pre-existing instance.
  static bool CreateFromExistingInstance(VkInstance instance, VkPhysicalDevice gpu, VkSurfaceKHR surface,
                                         bool take_ownership, bool enable_validation_layer, bool enable_debug_utils,
                                         const char** required_device_extensions = nullptr,
                                         uint32_t num_required_device_extensions = 0,
                                         const char** required_device_layers = nullptr,
                                         uint32_t num_required_device_layers = 0,
                                         const VkPhysicalDeviceFeatures* required_features = nullptr,
                                         CreateDeviceWrapper create_device_wrapper = nullptr,
                                         void* create_device_wrapper_opaque = nullptr);

  // Destroys context.
  static void Destroy();

  // Enable/disable debug message runtime.
  bool EnableDebugUtils();
  void DisableDebugUtils();

  // Global state accessors
  ALWAYS_INLINE VkPhysicalDevice GetPhysicalDevice() const { return m_physical_device; }
  ALWAYS_INLINE VkDevice GetDevice() const { return m_device; }
  ALWAYS_INLINE VkQueue GetGraphicsQueue() const { return m_graphics_queue; }
  ALWAYS_INLINE uint32_t GetGraphicsQueueFamilyIndex() const { return m_graphics_queue_family_index; }
  ALWAYS_INLINE VkQueue GetPresentQueue() const { return m_present_queue; }
  ALWAYS_INLINE uint32_t GetPresentQueueFamilyIndex() const { return m_present_queue_family_index; }
  ALWAYS_INLINE const VkPhysicalDeviceMemoryProperties& GetDeviceMemoryProperties() const
  {
    return m_device_memory_properties;
  }
  ALWAYS_INLINE const VkPhysicalDeviceProperties& GetDeviceProperties() const { return m_device_properties; }
  ALWAYS_INLINE const VkPhysicalDeviceFeatures& GetDeviceFeatures() const { return m_device_features; }
  ALWAYS_INLINE const VkPhysicalDeviceLimits& GetDeviceLimits() const { return m_device_properties.limits; }

  // Helpers for getting constants
  ALWAYS_INLINE VkDeviceSize GetUniformBufferAlignment() const
  {
    return m_device_properties.limits.minUniformBufferOffsetAlignment;
  }
  ALWAYS_INLINE VkDeviceSize GetTexelBufferAlignment() const
  {
    return m_device_properties.limits.minTexelBufferOffsetAlignment;
  }
  ALWAYS_INLINE VkDeviceSize GetStorageBufferAlignment() const
  {
    return m_device_properties.limits.minStorageBufferOffsetAlignment;
  }
  ALWAYS_INLINE VkDeviceSize GetBufferImageGranularity() const
  {
    return m_device_properties.limits.bufferImageGranularity;
  }

  // Finds a memory type index for the specified memory properties and the bits returned by
  // vkGetImageMemoryRequirements
  bool GetMemoryType(uint32_t bits, VkMemoryPropertyFlags properties, uint32_t* out_type_index);
  uint32_t GetMemoryType(uint32_t bits, VkMemoryPropertyFlags properties);

  // Finds a memory type for upload or readback buffers.
  uint32_t GetUploadMemoryType(uint32_t bits, bool* is_coherent = nullptr);
  uint32_t GetReadbackMemoryType(uint32_t bits, bool* is_coherent = nullptr, bool* is_cached = nullptr);

  // Creates a simple render pass.
  VkRenderPass GetRenderPass(VkFormat color_format, VkFormat depth_format, VkSampleCountFlagBits samples,
                             VkAttachmentLoadOp load_op);

  // These command buffers are allocated per-frame. They are valid until the command buffer
  // is submitted, after that you should call these functions again.
  ALWAYS_INLINE VkCommandBuffer GetCurrentCommandBuffer() const { return m_current_command_buffer; }

  /// Allocates a descriptor set from the pool reserved for the current frame.
  VkDescriptorSet AllocateDescriptorSet(VkDescriptorSetLayout set_layout);

  /// Allocates a descriptor set from the pool reserved for the current frame.
  VkDescriptorSet AllocateGlobalDescriptorSet(VkDescriptorSetLayout set_layout);

  /// Frees a descriptor set allocated from the global pool.
  void FreeGlobalDescriptorSet(VkDescriptorSet set);

  // Fence "counters" are used to track which commands have been completed by the GPU.
  // If the last completed fence counter is greater or equal to N, it means that the work
  // associated counter N has been completed by the GPU. The value of N to associate with
  // commands can be retreived by calling GetCurrentFenceCounter().
  uint64_t GetCompletedFenceCounter() const { return m_completed_fence_counter; }

  // Gets the fence that will be signaled when the currently executing command buffer is
  // queued and executed. Do not wait for this fence before the buffer is executed.
  uint64_t GetCurrentFenceCounter() const { return m_frame_resources[m_current_frame].fence_counter; }

  void SubmitCommandBuffer(VkSemaphore wait_semaphore = VK_NULL_HANDLE, VkSemaphore signal_semaphore = VK_NULL_HANDLE,
                           VkSwapchainKHR present_swap_chain = VK_NULL_HANDLE,
                           uint32_t present_image_index = 0xFFFFFFFF, bool submit_on_thread = false);
  void MoveToNextCommandBuffer();

  void ExecuteCommandBuffer(bool wait_for_completion);

  // Schedule a vulkan resource for destruction later on. This will occur when the command buffer
  // is next re-used, and the GPU has finished working with the specified resource.
  void DeferBufferDestruction(VkBuffer object);
  void DeferDeviceMemoryDestruction(VkDeviceMemory object);
  void DeferFramebufferDestruction(VkFramebuffer object);
  void DeferImageDestruction(VkImage object);
  void DeferImageViewDestruction(VkImageView object);

  // Wait for a fence to be completed.
  // Also invokes callbacks for completion.
  void WaitForFenceCounter(uint64_t fence_counter);

  void WaitForGPUIdle();

private:
  Context(VkInstance instance, VkPhysicalDevice physical_device, bool owns_device);

  using ExtensionList = std::vector<const char*>;
  bool SelectDeviceExtensions(ExtensionList* extension_list, bool enable_surface);
  bool SelectDeviceFeatures(const VkPhysicalDeviceFeatures* required_features);
  bool CreateDevice(VkSurfaceKHR surface, bool enable_validation_layer, const char** required_device_extensions,
                    uint32_t num_required_device_extensions, const char** required_device_layers,
                    uint32_t num_required_device_layers, const VkPhysicalDeviceFeatures* required_features,
                    CreateDeviceWrapper create_device_wrapper, void* create_device_wrapper_opaque);

  bool CreateCommandBuffers();
  void DestroyCommandBuffers();
  bool CreateGlobalDescriptorPool();
  void DestroyGlobalDescriptorPool();
  void DestroyRenderPassCache();

  void ActivateCommandBuffer(uint32_t index);
  void WaitForCommandBufferCompletion(uint32_t index);

  void DoSubmitCommandBuffer(uint32_t index, VkSemaphore wait_semaphore, VkSemaphore signal_semaphore);

  struct FrameResources
  {
    // [0] - Init (upload) command buffer, [1] - draw command buffer
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    uint64_t fence_counter = 0;
    bool needs_fence_wait = false;

    std::vector<std::function<void()>> cleanup_resources;
  };

  VkInstance m_instance = VK_NULL_HANDLE;
  VkPhysicalDevice m_physical_device = VK_NULL_HANDLE;
  VkDevice m_device = VK_NULL_HANDLE;

  VkCommandBuffer m_current_command_buffer = VK_NULL_HANDLE;

  VkDescriptorPool m_global_descriptor_pool = VK_NULL_HANDLE;

  VkQueue m_graphics_queue = VK_NULL_HANDLE;
  uint32_t m_graphics_queue_family_index = 0;
  VkQueue m_present_queue = VK_NULL_HANDLE;
  uint32_t m_present_queue_family_index = 0;

  std::array<FrameResources, NUM_COMMAND_BUFFERS> m_frame_resources;
  uint64_t m_next_fence_counter = 1;
  uint64_t m_completed_fence_counter = 0;
  uint32_t m_current_frame;

  bool m_owns_device = false;

  // Render pass cache
  using RenderPassCacheKey = std::tuple<VkFormat, VkFormat, VkSampleCountFlagBits, VkAttachmentLoadOp>;
  std::map<RenderPassCacheKey, VkRenderPass> m_render_pass_cache;

  VkDebugUtilsMessengerEXT m_debug_messenger_callback = VK_NULL_HANDLE;

  VkPhysicalDeviceFeatures m_device_features = {};
  VkPhysicalDeviceProperties m_device_properties = {};
  VkPhysicalDeviceMemoryProperties m_device_memory_properties = {};
};

} // namespace Vulkan

extern std::unique_ptr<Vulkan::Context> g_vulkan_context;
