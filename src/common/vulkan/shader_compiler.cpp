// Copyright 2016 Dolphin Emulator Project
// Copyright 2020 DuckStation Emulator Project
// Licensed under GPLv2+
// Refer to the LICENSE file included.

#include "shader_compiler.h"
#include "../log.h"
#include "../string_util.h"
#include "util.h"
#include <cstring>
#include <fstream>
#include <memory>
Log_SetChannel(Vulkan::ShaderCompiler);

// glslang includes
#include "SPIRV/GlslangToSpv.h"
#include "StandAlone/ResourceLimits.h"
#include "glslang/Public/ShaderLang.h"

namespace Vulkan::ShaderCompiler {
// Registers itself for cleanup via atexit
bool InitializeGlslang();

static unsigned s_next_bad_shader_id = 1;

static bool glslang_initialized = false;

static std::optional<SPIRVCodeVector> CompileShaderToSPV(EShLanguage stage, const char* stage_filename,
                                                         std::string_view source)
{
  if (!InitializeGlslang())
    return std::nullopt;

  std::unique_ptr<glslang::TShader> shader = std::make_unique<glslang::TShader>(stage);
  std::unique_ptr<glslang::TProgram> program;
  glslang::TShader::ForbidIncluder includer;
  EProfile profile = ECoreProfile;
  EShMessages messages = static_cast<EShMessages>(EShMsgDefault | EShMsgSpvRules | EShMsgVulkanRules);
  int default_version = 450;

  std::string full_source_code;
  const char* pass_source_code = source.data();
  int pass_source_code_length = static_cast<int>(source.size());
  shader->setStringsWithLengths(&pass_source_code, &pass_source_code_length, 1);

  auto DumpBadShader = [&](const char* msg) {
    std::string filename = StringUtil::StdStringFromFormat("bad_shader_%u.txt", s_next_bad_shader_id++);
    Log::Writef("Vulkan", "CompileShaderToSPV", LOGLEVEL_ERROR, "%s, writing to %s", msg, filename.c_str());

    std::ofstream ofs(filename.c_str(), std::ofstream::out | std::ofstream::binary);
    if (ofs.is_open())
    {
      ofs << source;
      ofs << "\n";

      ofs << msg << std::endl;
      ofs << "Shader Info Log:" << std::endl;
      ofs << shader->getInfoLog() << std::endl;
      ofs << shader->getInfoDebugLog() << std::endl;
      if (program)
      {
        ofs << "Program Info Log:" << std::endl;
        ofs << program->getInfoLog() << std::endl;
        ofs << program->getInfoDebugLog() << std::endl;
      }

      ofs.close();
    }
  };

  if (!shader->parse(&glslang::DefaultTBuiltInResource, default_version, profile, false, true, messages, includer))
  {
    DumpBadShader("Failed to parse shader");
    return std::nullopt;
  }

  // Even though there's only a single shader, we still need to link it to generate SPV
  program = std::make_unique<glslang::TProgram>();
  program->addShader(shader.get());
  if (!program->link(messages))
  {
    DumpBadShader("Failed to link program");
    return std::nullopt;
  }

  glslang::TIntermediate* intermediate = program->getIntermediate(stage);
  if (!intermediate)
  {
    DumpBadShader("Failed to generate SPIR-V");
    return std::nullopt;
  }

  SPIRVCodeVector out_code;
  spv::SpvBuildLogger logger;
  glslang::GlslangToSpv(*intermediate, out_code, &logger);

  const char *shdr_dbg_log   = shader->getInfoDebugLog();
  const char *shdr_info_log  = shader->getInfoLog();
  const char *prg_info_log   = program->getInfoLog();
  const char *prg_dbg_log    = program->getInfoDebugLog();

  // Write out messages
  // Temporary: skip if it contains "Warning, version 450 is not yet complete; most version-specific
  // features are present, but some are missing."
  if (std::strlen(shdr_info_log) > 108)
    Log_WarningPrintf("Shader info log: %s", shader->getInfoLog());
  if (shdr_dbg_log && shdr_dbg_log[0] != '\0')
    Log_WarningPrintf("Shader debug info log: %s", shader->getInfoDebugLog());
  if (std::strlen(prg_info_log) > 25)
    Log_WarningPrintf("Program info log: %s", program->getInfoLog());
  if (prg_dbg_log && prg_dbg_log[0] != '\0')
    Log_WarningPrintf("Program debug info log: %s", program->getInfoDebugLog());
  std::string spv_messages = logger.getAllMessages();
  if (!spv_messages.empty())
    Log_WarningPrintf("SPIR-V conversion messages: %s", spv_messages.c_str());

  return out_code;
}

bool InitializeGlslang()
{
  if (glslang_initialized)
    return true;

  if (!glslang::InitializeProcess())
    return false;

  glslang_initialized = true;
  return true;
}

void DeinitializeGlslang()
{
  if (!glslang_initialized)
    return;

  glslang::FinalizeProcess();
  glslang_initialized = false;
}

std::optional<ShaderCompiler::SPIRVCodeVector> CompileShader(Type type, std::string_view source_code, bool debug)
{
  switch (type)
  {
    case Type::Vertex:
      return CompileShaderToSPV(EShLangVertex, "vs", source_code);

    case Type::Geometry:
      return CompileShaderToSPV(EShLangGeometry, "gs", source_code);

    case Type::Fragment:
      return CompileShaderToSPV(EShLangFragment, "ps", source_code);

    case Type::Compute:
      return CompileShaderToSPV(EShLangCompute, "cs", source_code);

    default:
      return std::nullopt;
  }
}

} // namespace Vulkan::ShaderCompiler
