include(CheckSymbolExists)

add_definitions("-DLIBRETRO=1")

check_symbol_exists(mkstemp "stdlib.h" HAVE_MKSTEMP)
check_symbol_exists(alloca "alloca.h" HAVE_ALLOCA_H)
if(NOT HAVE_ALLOCA_H)
  check_symbol_exists(alloca "malloc.h" HAVE_MALLOC_H)
endif()
configure_file("config.h.cmake" "config.h")
include_directories("${CMAKE_CURRENT_BINARY_DIR}")

add_subdirectory(common)
add_subdirectory(core)
add_subdirectory(libretro)
