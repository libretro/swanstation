set(SRCS
	${CMAKE_CURRENT_SOURCE_DIR}/compat/compat_posix_string.c
	${CMAKE_CURRENT_SOURCE_DIR}/compat/compat_strl.c
	${CMAKE_CURRENT_SOURCE_DIR}/compat/fopen_utf8.c
	${CMAKE_CURRENT_SOURCE_DIR}/encodings/encoding_utf.c
	${CMAKE_CURRENT_SOURCE_DIR}/streams/file_stream.c
	${CMAKE_CURRENT_SOURCE_DIR}/string/stdstring.c
	${CMAKE_CURRENT_SOURCE_DIR}/time/rtime.c
	${CMAKE_CURRENT_SOURCE_DIR}/vfs/vfs_implementation.c
)

add_library(libretro-common ${SRCS})

target_include_directories(libretro-common PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include/")
target_include_directories(libretro-common INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include/")
