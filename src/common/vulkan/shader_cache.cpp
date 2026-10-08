#include "shader_cache.h"
#include "../file_system.h"
#include "../log.h"
#include "context.h"
#include "util.h"

#include <cstring>
#include <optional>
#include <vector>
#include <file/file_path.h>
Log_SetChannel(Vulkan::ShaderCache);

std::unique_ptr<Vulkan::ShaderCache> g_vulkan_shader_cache;

namespace Vulkan {

#pragma pack(push, 4)
struct VK_PIPELINE_CACHE_HEADER
{
  uint32_t header_length;
  uint32_t header_version;
  uint32_t vendor_id;
  uint32_t device_id;
  uint8_t  uuid[VK_UUID_SIZE];
};
#pragma pack(pop)

static bool ValidatePipelineCacheHeader(const VK_PIPELINE_CACHE_HEADER& header)
{
  if (header.header_length < sizeof(VK_PIPELINE_CACHE_HEADER))
  {
    Log_ErrorPrintf("Pipeline cache failed validation: Invalid header length");
    return false;
  }
  if (header.header_version != VK_PIPELINE_CACHE_HEADER_VERSION_ONE)
  {
    Log_ErrorPrintf("Pipeline cache failed validation: Invalid header version");
    return false;
  }
  if (header.vendor_id != g_vulkan_context->GetDeviceProperties().vendorID)
  {
    Log_ErrorPrintf("Pipeline cache failed validation: Incorrect vendor ID (file: 0x%X, device: 0x%X)",
                    header.vendor_id, g_vulkan_context->GetDeviceProperties().vendorID);
    return false;
  }
  if (header.device_id != g_vulkan_context->GetDeviceProperties().deviceID)
  {
    Log_ErrorPrintf("Pipeline cache failed validation: Incorrect device ID (file: 0x%X, device: 0x%X)",
                    header.device_id, g_vulkan_context->GetDeviceProperties().deviceID);
    return false;
  }
  if (std::memcmp(header.uuid, g_vulkan_context->GetDeviceProperties().pipelineCacheUUID, VK_UUID_SIZE) != 0)
  {
    Log_ErrorPrintf("Pipeline cache failed validation: Incorrect UUID");
    return false;
  }
  return true;
}

static void FillPipelineCacheHeader(VK_PIPELINE_CACHE_HEADER* header)
{
  header->header_length  = sizeof(VK_PIPELINE_CACHE_HEADER);
  header->header_version = VK_PIPELINE_CACHE_HEADER_VERSION_ONE;
  header->vendor_id      = g_vulkan_context->GetDeviceProperties().vendorID;
  header->device_id      = g_vulkan_context->GetDeviceProperties().deviceID;
  std::memcpy(header->uuid, g_vulkan_context->GetDeviceProperties().pipelineCacheUUID, VK_UUID_SIZE);
}

ShaderCache::ShaderCache() = default;

ShaderCache::~ShaderCache()
{
  FlushPipelineCache();
  ClosePipelineCache();
}

void ShaderCache::Create(std::string_view base_path, bool debug)
{
  g_vulkan_shader_cache.reset(new ShaderCache());
  g_vulkan_shader_cache->Open(base_path, debug);
}

void ShaderCache::Destroy()
{
  g_vulkan_shader_cache.reset();
}

void ShaderCache::Open(std::string_view base_path, bool debug)
{
  if (!base_path.empty())
  {
    // Sweep any leftover SPIR-V cache files from earlier builds. The
    // runtime-glslang path is gone, so these are no longer touched -
    // delete them so users do not end up with a stale 0-byte
    // vulkan_shaders.bin sitting on disk forever.
    const std::string legacy = GetLegacyShaderCacheBaseFileName(base_path, debug);
    const std::string legacy_idx = legacy + ".idx";
    const std::string legacy_bin = legacy + ".bin";
    if (path_is_valid(legacy_idx.c_str()))
      filestream_delete(legacy_idx.c_str());
    if (path_is_valid(legacy_bin.c_str()))
      filestream_delete(legacy_bin.c_str());

    m_pipeline_cache_filename = GetPipelineCacheBaseFileName(base_path, debug);
    if (!ReadExistingPipelineCache())
      CreateNewPipelineCache();
  }
  else
  {
    CreateNewPipelineCache();
  }
}

VkPipelineCache ShaderCache::GetPipelineCache(bool set_dirty /*= true*/)
{
  if (m_pipeline_cache == VK_NULL_HANDLE)
    return VK_NULL_HANDLE;

  m_pipeline_cache_dirty |= set_dirty;
  return m_pipeline_cache;
}

std::string ShaderCache::GetPipelineCacheBaseFileName(const std::string_view& base_path, bool debug)
{
  std::string base_filename(base_path);
  base_filename += "vulkan_pipelines";
  if (debug)
    base_filename += "_debug";
  base_filename += ".bin";
  return base_filename;
}

std::string ShaderCache::GetLegacyShaderCacheBaseFileName(const std::string_view& base_path, bool debug)
{
  std::string base_filename(base_path);
  base_filename += "vulkan_shaders";
  if (debug)
    base_filename += "_debug";
  return base_filename;
}

bool ShaderCache::CreateNewPipelineCache()
{
  if (!m_pipeline_cache_filename.empty() && path_is_valid(m_pipeline_cache_filename.c_str()))
  {
    Log_WarningPrintf("Removing existing pipeline cache '%s'", m_pipeline_cache_filename.c_str());
    filestream_delete(m_pipeline_cache_filename.c_str());
  }

  const VkPipelineCacheCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO, nullptr, 0, 0, nullptr};
  VkResult res = vkCreatePipelineCache(g_vulkan_context->GetDevice(), &ci, nullptr, &m_pipeline_cache);
  if (res != VK_SUCCESS)
  {
    LOG_VULKAN_ERROR(res, "vkCreatePipelineCache() failed: ");
    return false;
  }

  m_pipeline_cache_dirty = true;
  return true;
}

bool ShaderCache::ReadExistingPipelineCache()
{
  std::optional<std::vector<uint8_t>> data = FileSystem::ReadBinaryFile(m_pipeline_cache_filename.c_str());
  if (!data.has_value())
    return false;

  if (data->size() < sizeof(VK_PIPELINE_CACHE_HEADER))
  {
    Log_ErrorPrintf("Pipeline cache at '%s' is too small", m_pipeline_cache_filename.c_str());
    return false;
  }

  VK_PIPELINE_CACHE_HEADER header;
  std::memcpy(&header, data->data(), sizeof(header));
  if (!ValidatePipelineCacheHeader(header))
    return false;

  const VkPipelineCacheCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO, nullptr, 0, data->size(),
                                     data->data()};
  VkResult res = vkCreatePipelineCache(g_vulkan_context->GetDevice(), &ci, nullptr, &m_pipeline_cache);
  if (res != VK_SUCCESS)
  {
    LOG_VULKAN_ERROR(res, "vkCreatePipelineCache() failed: ");
    return false;
  }

  return true;
}

bool ShaderCache::FlushPipelineCache()
{
  if (m_pipeline_cache == VK_NULL_HANDLE || !m_pipeline_cache_dirty || m_pipeline_cache_filename.empty())
    return false;

  size_t data_size;
  VkResult res = vkGetPipelineCacheData(g_vulkan_context->GetDevice(), m_pipeline_cache, &data_size, nullptr);
  if (res != VK_SUCCESS)
  {
    LOG_VULKAN_ERROR(res, "vkGetPipelineCacheData() failed: ");
    return false;
  }

  std::vector<uint8_t> data(data_size);
  res = vkGetPipelineCacheData(g_vulkan_context->GetDevice(), m_pipeline_cache, &data_size, data.data());
  if (res != VK_SUCCESS)
  {
    LOG_VULKAN_ERROR(res, "vkGetPipelineCacheData() (data) failed: ");
    return false;
  }
  data.resize(data_size);

  if (!FileSystem::WriteBinaryFile(m_pipeline_cache_filename.c_str(), data.data(), data.size()))
  {
    Log_ErrorPrintf("Failed to write pipeline cache to '%s'", m_pipeline_cache_filename.c_str());
    return false;
  }

  Log_InfoPrintf("Saved %zu bytes to '%s'", data_size, m_pipeline_cache_filename.c_str());
  m_pipeline_cache_dirty = false;
  return true;
}

VkPipelineCache ShaderCache::CreateWorkerPipelineCache()
{
  const VkDevice device = g_vulkan_context->GetDevice();
  std::vector<uint8_t> data;
  if (m_pipeline_cache != VK_NULL_HANDLE)
  {
    size_t data_size = 0;
    if (vkGetPipelineCacheData(device, m_pipeline_cache, &data_size, nullptr) == VK_SUCCESS && data_size > 0)
    {
      data.resize(data_size);
      if (vkGetPipelineCacheData(device, m_pipeline_cache, &data_size, data.data()) == VK_SUCCESS)
        data.resize(data_size);
      else
        data.clear();
    }
  }

  const VkPipelineCacheCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO, nullptr, 0, data.size(),
                                     data.empty() ? nullptr : data.data()};
  VkPipelineCache cache = VK_NULL_HANDLE;
  const VkResult res = vkCreatePipelineCache(device, &ci, nullptr, &cache);
  if (res != VK_SUCCESS)
  {
    LOG_VULKAN_ERROR(res, "vkCreatePipelineCache() (worker) failed: ");
    return VK_NULL_HANDLE;
  }

  return cache;
}

void ShaderCache::MergeWorkerPipelineCache(VkPipelineCache cache)
{
  if (cache == VK_NULL_HANDLE)
    return;

  const VkDevice device = g_vulkan_context->GetDevice();
  if (m_pipeline_cache != VK_NULL_HANDLE)
  {
    const VkResult res = vkMergePipelineCaches(device, m_pipeline_cache, 1, &cache);
    if (res == VK_SUCCESS)
      m_pipeline_cache_dirty = true;
    else
      LOG_VULKAN_ERROR(res, "vkMergePipelineCaches() failed: ");
  }

  vkDestroyPipelineCache(device, cache, nullptr);
}

void ShaderCache::ClosePipelineCache()
{
  if (m_pipeline_cache == VK_NULL_HANDLE)
    return;

  vkDestroyPipelineCache(g_vulkan_context->GetDevice(), m_pipeline_cache, nullptr);
  m_pipeline_cache = VK_NULL_HANDLE;
}

} // namespace Vulkan
