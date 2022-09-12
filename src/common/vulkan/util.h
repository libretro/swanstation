// Copyright 2016 Dolphin Emulator Project
// Copyright 2020 DuckStation Emulator Project
// Licensed under GPLv2+
// Refer to the LICENSE file included.

#pragma once

#include "../string.h"
#include "../types.h"
#include "vulkan_loader.h"
#include <algorithm>
#include <array>
#include <cstdarg>
namespace Vulkan {
namespace Util {

bool IsDepthFormat(VkFormat format);
bool IsCompressedFormat(VkFormat format);
VkFormat GetLinearFormat(VkFormat format);
u32 GetTexelSize(VkFormat format);
u32 GetBlockSize(VkFormat format);

// Clamps a VkRect2D to the specified dimensions.
VkRect2D ClampRect2D(const VkRect2D& rect, u32 width, u32 height);

// Map {SRC,DST}_COLOR to {SRC,DST}_ALPHA
VkBlendFactor GetAlphaBlendFactor(VkBlendFactor factor);

// Safe destroy helpers
void SafeDestroyFramebuffer(VkFramebuffer& fb);
void SafeDestroyShaderModule(VkShaderModule& sm);
void SafeDestroyPipeline(VkPipeline& p);
void SafeDestroyPipelineLayout(VkPipelineLayout& pl);
void SafeDestroyDescriptorSetLayout(VkDescriptorSetLayout& dsl);
void SafeDestroyBufferView(VkBufferView& bv);
void SafeDestroyImageView(VkImageView& iv);
void SafeDestroySampler(VkSampler& samp);
void SafeDestroySemaphore(VkSemaphore& sem);
void SafeFreeGlobalDescriptorSet(VkDescriptorSet& ds);

void SetViewport(VkCommandBuffer command_buffer, int x, int y, int width, int height, float min_depth = 0.0f,
                 float max_depth = 1.0f);
void SetScissor(VkCommandBuffer command_buffer, int x, int y, int width, int height);

// Combines viewport and scissor updates
void SetViewportAndScissor(VkCommandBuffer command_buffer, int x, int y, int width, int height, float min_depth = 0.0f,
                           float max_depth = 1.0f);

// Wrapper for creating an barrier on a buffer
void BufferMemoryBarrier(VkCommandBuffer command_buffer, VkBuffer buffer, VkAccessFlags src_access_mask,
                         VkAccessFlags dst_access_mask, VkDeviceSize offset, VkDeviceSize size,
                         VkPipelineStageFlags src_stage_mask, VkPipelineStageFlags dst_stage_mask);

void LogVulkanResult(int level, const char* func_name, VkResult res, const char* msg, ...) printflike(4, 5);

#define LOG_VULKAN_ERROR(res, ...) ::Vulkan::Util::LogVulkanResult(1, __func__, res, __VA_ARGS__)

} // namespace Util

} // namespace Vulkan
