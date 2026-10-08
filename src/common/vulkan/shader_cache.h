#pragma once
#include "vulkan_loader.h"
#include <memory>
#include <string>
#include <string_view>

namespace Vulkan {

// VkPipelineCache wrapper. Historically this class also managed an on-
// disk SPIR-V blob cache populated by runtime glslang invocations on
// shadergen output (vulkan_shaders.bin / vulkan_shaders.idx). That
// SPIR-V cache is gone now that every shader in the Vulkan backend is
// pre-baked into the build at .inc-include time and instantiated via
// Vulkan::EmbeddedShaders::CreateShaderModule. What remains is the
// driver's own pipeline-binary cache (vulkan_pipelines.bin) which is
// still essential - VkPipelineCache feeds the driver's SPIR-V -> GPU
// ISA compiler, and saving it across runs avoids recompiling every
// pipeline on every cold boot.
//
// Open() will also opportunistically delete any leftover vulkan_-
// shaders.bin / vulkan_shaders.idx on disk so users with stale caches
// from earlier builds get them tidied up automatically.
class ShaderCache
{
public:
  ~ShaderCache();

  static void Create(std::string_view base_path, bool debug);
  static void Destroy();

  /// Returns a handle to the pipeline cache. Set set_dirty to true if you are planning on writing to it externally.
  VkPipelineCache GetPipelineCache(bool set_dirty = true);

  /// Writes pipeline cache to file, saving all newly compiled pipelines.
  bool FlushPipelineCache();

  /// Creates a private pipeline cache seeded with the current contents, for a thread that
  /// compiles pipelines concurrently; VK_NULL_HANDLE on failure. Same thread as the
  /// main cache's users.
  VkPipelineCache CreateWorkerPipelineCache();

  /// Merges a cache from CreateWorkerPipelineCache() back in and destroys it, once no
  /// other thread uses it.
  void MergeWorkerPipelineCache(VkPipelineCache cache);

private:
  ShaderCache();

  static std::string GetPipelineCacheBaseFileName(const std::string_view& base_path, bool debug);
  static std::string GetLegacyShaderCacheBaseFileName(const std::string_view& base_path, bool debug);

  void Open(std::string_view base_path, bool debug);

  bool CreateNewPipelineCache();
  bool ReadExistingPipelineCache();
  void ClosePipelineCache();

  std::string m_pipeline_cache_filename;

  VkPipelineCache m_pipeline_cache = VK_NULL_HANDLE;
  bool m_pipeline_cache_dirty = false;
};

} // namespace Vulkan

extern std::unique_ptr<Vulkan::ShaderCache> g_vulkan_shader_cache;
