#pragma once
#include "../file_system.h"
#include "../hash_combine.h"
#include "../types.h"
#include "../windows_headers.h"
#include "pipeline_library_compat.h"
#include <cstdio>
#include <d3d12.h>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <wrl/client.h>

namespace D3D12 {

class ShaderCache
{
public:
  template<typename T>
  using ComPtr = Microsoft::WRL::ComPtr<T>;

  enum class EntryType
  {
    VertexShader,
    GeometryShader,
    PixelShader,
    ComputeShader,
    GraphicsPipeline,
  };

  ShaderCache();
  ~ShaderCache();

  // 'device' may be null - in that case the pipeline-library path is
  // disabled and we fall back to direct CreateGraphicsPipelineState
  // every time (legacy behaviour). If non-null we QueryInterface it
  // for ID3D12Device1 (D3D12.1, Windows 10 v1703+) and on success
  // enable the on-disk pipeline-library cache.
  //
  // 'version' is the content version (SHADER_CACHE_VERSION) stamped
  // into the shader bytecode index header after FILE_VERSION, so the
  // header layout matches D3D11::ShaderCache for the shared
  // d3d_shaders_<sm> file. Bumping it invalidates the cache when
  // shadergen output changes.
  void Open(std::string_view base_path, ID3D12Device* device, D3D_FEATURE_LEVEL feature_level, uint32_t version,
            bool debug);

  // Returns whether Open() has already been called on this instance.
  // Used by the persistent (lazy-compile) GPU backend to avoid
  // re-running Open on UpdateSettings round-trips through
  // DestroyPipelines/CompilePipelines, which would re-read the
  // pipeline-library blob and leak its file handles. Previously keyed
  // off m_shader_index_file, but the shader bytecode cache is no longer
  // opened (it's scrubbed instead - every shader is pre-baked), so
  // track open state explicitly.
  bool IsOpen() const { return m_open; }

  ComPtr<ID3D12PipelineState> GetPipelineState(ID3D12Device* device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc);

private:
  struct CacheIndexKey
  {
    uint64_t source_hash_low;
    uint64_t source_hash_high;
    uint32_t source_length;
    EntryType type;
  };

  static std::string GetCacheBaseFileName(const std::string_view& base_path, const std::string_view& type,
                                          D3D_FEATURE_LEVEL feature_level, bool debug);
  static CacheIndexKey GetPipelineCacheKey(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& gpdesc);

  // ID3D12PipelineLibrary-based on-disk PSO cache (D3D12.1+).
  // TryOpenPipelineLibrary loads the existing blob from disk if any,
  // hands it to ID3D12Device1::CreatePipelineLibrary, falls back to
  // an empty library on any failure (driver version mismatch, corrupt
  // file, etc.). Returns true if a usable library was created (full
  // or empty) and the library path should be used.
  //
  // SerializePipelineLibrary is called from the destructor and writes
  // the current library state to disk atomically (.tmp + rename).
  // Best-effort - a write failure logs and proceeds.
  bool TryOpenPipelineLibrary(ID3D12Device1* device1, std::string_view base_path, D3D_FEATURE_LEVEL feature_level,
                              bool debug);
  void SerializePipelineLibrary();
  static std::string GetPipelineLibraryFilename(const std::string_view& base_path, D3D_FEATURE_LEVEL feature_level,
                                                bool debug);
  // Render the 128-bit hash portion of a CacheIndexKey as a 32-wchar
  // hex string for use as a stable ID3D12PipelineLibrary entry name.
  // Writes 33 wchar_t (32 hex digits + L'\0') into 'out'.
  static void FormatPipelineLibraryName(const CacheIndexKey& key, WCHAR out[33]);

  std::string m_base_path;

  // ID3D12PipelineLibrary cache. m_pipeline_library_blob holds the
  // input bytes for CreatePipelineLibrary - the library keeps a
  // reference into this buffer for its entire lifetime per the D3D12
  // spec, so the vector must NOT be resized or destroyed before the
  // library. The pipeline library's own methods (StorePipeline,
  // LoadGraphicsPipeline, Serialize) are documented thread-safe so
  // we don't need a separate mutex around library access; lock-free
  // compile (the f46be3e / 65d02fd property) is preserved.
  ComPtr<ID3D12PipelineLibrary> m_pipeline_library;
  std::vector<uint8_t> m_pipeline_library_blob;

  D3D_FEATURE_LEVEL m_feature_level = D3D_FEATURE_LEVEL_11_0;
  bool m_use_pipeline_library = false;
  bool m_debug = false;
  bool m_open = false;
};

} // namespace D3D12
