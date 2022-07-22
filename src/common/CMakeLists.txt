add_library(common
  align.h
  assert.cpp
  assert.h
  audio_stream.cpp
  audio_stream.h
  bitfield.h
  bitutils.h
  byte_stream.cpp
  byte_stream.h
  cd_image.cpp
  cd_image.h
  cd_image_bin.cpp
  cd_image_cue.cpp
  cd_image_chd.cpp
  cd_image_device.cpp
  cd_image_ecm.cpp
  cd_image_hasher.cpp
  cd_image_hasher.h
  cd_image_m3u.cpp
  cd_image_memory.cpp
  cd_image_mds.cpp
  cd_image_pbp.cpp
  cd_image_ppf.cpp
  cd_subchannel_replacement.cpp
  cd_subchannel_replacement.h
  cd_xa.cpp
  cd_xa.h
  cue_parser.cpp
  cue_parser.h
  dimensional_array.h
  error.cpp
  error.h
  event.cpp
  event.h
  fifo_queue.h
  file_system.cpp
  file_system.h
  image.cpp
  image.h
  gl/program.cpp
  gl/program.h
  gl/shader_cache.cpp
  gl/shader_cache.h
  gl/stream_buffer.cpp
  gl/stream_buffer.h
  gl/texture.cpp
  gl/texture.h
  hash_combine.h
  heap_array.h
  iso_reader.cpp
  iso_reader.h
  jit_code_buffer.cpp
  jit_code_buffer.h
  log.cpp
  log.h
  make_array.h
  md5_digest.cpp
  md5_digest.h
  null_audio_stream.cpp
  null_audio_stream.h
  memory_arena.cpp
  memory_arena.h
  page_fault_handler.cpp
  page_fault_handler.h
  platform.h
  pbp_types.h
  progress_callback.cpp
  progress_callback.h
  rectangle.h
  scope_guard.h
  shiftjis.cpp
  shiftjis.h
  state_wrapper.cpp
  state_wrapper.h
  string.cpp
  string.h
  string_util.cpp
  string_util.h
  timer.cpp
  timer.h
  timestamp.cpp
  timestamp.h
  types.h
  vulkan/builders.cpp
  vulkan/builders.h
  vulkan/context.cpp
  vulkan/context.h
  vulkan/shader_cache.cpp
  vulkan/shader_cache.h
  vulkan/shader_compiler.cpp
  vulkan/shader_compiler.h
  vulkan/staging_buffer.cpp
  vulkan/staging_buffer.h
  vulkan/staging_texture.cpp
  vulkan/staging_texture.h
  vulkan/stream_buffer.cpp
  vulkan/stream_buffer.h
  vulkan/texture.cpp
  vulkan/texture.h
  vulkan/util.cpp
  vulkan/util.h
  window_info.h
)

target_include_directories(common PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_include_directories(common PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(common PRIVATE glad stb Threads::Threads libchdr glslang vulkan-loader zlib)

if(WIN32)
  target_sources(common PRIVATE
    d3d11/shader_cache.cpp
    d3d11/shader_cache.h
    d3d11/shader_compiler.cpp
    d3d11/shader_compiler.h
    d3d11/staging_texture.cpp
    d3d11/staging_texture.h
    d3d11/stream_buffer.cpp
    d3d11/stream_buffer.h
    d3d11/texture.cpp
    d3d11/texture.h
    windows_headers.h
  )
  if(USE_D3D12)
    target_sources(common PRIVATE
      d3d12/context.cpp
      d3d12/context.h
      d3d12/descriptor_heap_manager.cpp
      d3d12/descriptor_heap_manager.h
      d3d12/shader_cache.cpp
      d3d12/shader_cache.h
      d3d12/staging_texture.cpp
      d3d12/staging_texture.h
      d3d12/stream_buffer.cpp
      d3d12/stream_buffer.h
      d3d12/texture.cpp
      d3d12/texture.h
      d3d12/util.cpp
      d3d12/util.h
    )
  endif()
  target_link_libraries(common PRIVATE d3dcompiler.lib)

endif()

if(ANDROID)
  target_link_libraries(common PRIVATE log)
endif()

if(APPLE)
  # Needed for Vulkan Swap Chain.
  target_link_libraries(common PRIVATE "objc")
endif()

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
  # We need -lrt for shm_unlink
  target_link_libraries(common PRIVATE rt)
endif()
