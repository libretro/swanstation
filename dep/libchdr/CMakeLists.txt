add_library(libchdr
  include/dr_libs/dr_flac.h
  include/libchdr/bitstream.h
  include/libchdr/cdrom.h
  include/libchdr/chd.h
  include/libchdr/chdconfig.h
  include/libchdr/coretypes.h
  include/libchdr/flac.h
  include/libchdr/huffman.h
  src/libchdr_bitstream.c
  src/libchdr_cdrom.c
  src/libchdr_chd.c
  src/libchdr_flac.c
  src/libchdr_huffman.c
)

target_include_directories(libchdr PUBLIC 
	"${CMAKE_CURRENT_SOURCE_DIR}/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/../libretro-common/include"
	)
target_compile_definitions(libchdr PRIVATE -D__LIBRETRO__)
target_link_libraries(libchdr PRIVATE zlib lzma)

