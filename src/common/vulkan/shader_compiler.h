// Copyright 2016 Dolphin Emulator Project
// Copyright 2020 DuckStation Emulator Project
// Licensed under GPLv2+
// Refer to the LICENSE file included.

#pragma once

#include "../types.h"
#include <optional>
#include <string_view>
#include <vector>

namespace Vulkan::ShaderCompiler {

// Shader types
enum class Type
{
  Vertex,
  Geometry,
  Fragment,
  Compute
};

void DeinitializeGlslang();

// SPIR-V compiled code type
using SPIRVCodeType = u32;
using SPIRVCodeVector = std::vector<SPIRVCodeType>;

std::optional<SPIRVCodeVector> CompileShader(Type type, std::string_view source_code, bool debug);

} // namespace Vulkan::ShaderCompiler
