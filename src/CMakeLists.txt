add_definitions("-DLIBRETRO=1")

configure_file("config.h.cmake" "config.h")
include_directories("${CMAKE_CURRENT_BINARY_DIR}")

add_subdirectory(common)
add_subdirectory(core)
add_subdirectory(libretro)
