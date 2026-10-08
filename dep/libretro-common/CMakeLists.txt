set(SRCS
	${CMAKE_CURRENT_SOURCE_DIR}/compat/compat_posix_string.c
	${CMAKE_CURRENT_SOURCE_DIR}/compat/compat_strl.c
	${CMAKE_CURRENT_SOURCE_DIR}/compat/fopen_utf8.c
	${CMAKE_CURRENT_SOURCE_DIR}/file/file_path.c
	${CMAKE_CURRENT_SOURCE_DIR}/file/file_path_io.c
	${CMAKE_CURRENT_SOURCE_DIR}/file/retro_dirent.c
	${CMAKE_CURRENT_SOURCE_DIR}/encodings/encoding_utf.c
	${CMAKE_CURRENT_SOURCE_DIR}/formats/data_transfer.c
	${CMAKE_CURRENT_SOURCE_DIR}/memmap/memmap.c
	${CMAKE_CURRENT_SOURCE_DIR}/queues/retro_spsc.c
	${CMAKE_CURRENT_SOURCE_DIR}/rthreads/retro_eventcount.c
	${CMAKE_CURRENT_SOURCE_DIR}/rthreads/rthreads.c
	${CMAKE_CURRENT_SOURCE_DIR}/streams/file_stream.c
	${CMAKE_CURRENT_SOURCE_DIR}/string/stdstring.c
	${CMAKE_CURRENT_SOURCE_DIR}/time/rtime.c
	${CMAKE_CURRENT_SOURCE_DIR}/vfs/vfs_implementation.c
	${CMAKE_CURRENT_SOURCE_DIR}/vfs/vfs_hybrid.c
)

add_library(libretro-common ${SRCS})

# File mapping backs RETRO_VFS_FILE_ACCESS_HINT_FREQUENT_ACCESS (and
# filestream_get_mapped_ptr): mmap() on POSIX platforms, CreateFileMapping
# on Windows (automatic under _WIN32, no define needed).
# LIBRETRO_COMMON_NO_MMAP builds without it so the unmapped fallback
# paths (block scanners, seek+read) can be exercised and A/B tested.
option(LIBRETRO_COMMON_NO_MMAP "Disable file mapping (fallback-path testing)" OFF)
if(NOT WIN32 AND NOT LIBRETRO_COMMON_NO_MMAP)
  target_compile_definitions(libretro-common PRIVATE HAVE_MMAP)
endif()

target_link_libraries(libretro-common PUBLIC Threads::Threads)

target_include_directories(libretro-common PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include/")
target_include_directories(libretro-common INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include/")
