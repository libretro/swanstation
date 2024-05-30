cmake_minimum_required(VERSION 3.10)
project(swanstation C CXX)

message("CMake Version: ${CMAKE_VERSION}")
message("CMake System Name: ${CMAKE_SYSTEM_NAME}")

# Pull in modules.
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/CMakeModules/")

# Platform detection.
if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
  set(LINUX TRUE)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "FreeBSD")
  set(FREEBSD TRUE)
endif()

# Set minimum OS version for macOS. 10.14 should work.
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.14.0" CACHE STRING "")

set(USE_EGL OFF)
set(USE_WAYLAND OFF)
set(USE_D3D12 OFF)
set(USE_FBDEV OFF)
set(USE_EVDEV OFF)

# Force PIC when compiling a libretro core.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Set _DEBUG macro for Debug builds.
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -D_DEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -D_DEBUG")

find_package(Git)
if(GIT_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
	execute_process(
		COMMAND ${GIT_EXECUTABLE} describe --tags --always
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
		OUTPUT_VARIABLE GIT_VERSION
		OUTPUT_STRIP_TRAILING_WHITESPACE
	)
	execute_process(
		COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
		OUTPUT_VARIABLE GIT_HASH
		OUTPUT_STRIP_TRAILING_WHITESPACE
	)
endif()

string(TIMESTAMP BUILD_TIMESTAMP UTC)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/libretro/version.h.in" "${CMAKE_CURRENT_SOURCE_DIR}/src/libretro/version.h" @ONLY)

# MSVC specific tweaks.
if(MSVC)
  add_definitions("/D_CRT_SECURE_NO_WARNINGS")
  foreach(config CMAKE_C_FLAGS CMAKE_CXX_FLAGS)
    # Set warning level 3 instead of 4.
    string(REPLACE "/W3" "/W4" ${config} "${${config}}")

    # Enable intrinsic functions, disable minimal rebuild, UTF-8 source, set __cplusplus version.
    set(${config} "${${config}} /Oi /Gm- /utf-8 /Zc:__cplusplus")
  endforeach()
endif()

# COMDAT folding/remove unused functions.
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /OPT:REF /OPT:ICF")

# Enable LTO/LTCG on Release builds.
if(${CMAKE_BUILD_TYPE} STREQUAL "Release")
  cmake_policy(SET CMP0069 NEW)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT IPO_IS_SUPPORTED)
  if(IPO_IS_SUPPORTED)
    message(STATUS "Enabling LTCG/IPO.")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
  else()
    message(STATUS "LTCG/IPO is not supported, this will make the build slightly slower.")
  endif()
endif()

# Default symbol visibility to hidden, that way we don't go through the PLT for intra-library calls.
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

# -fno-semantic-interposition appears to be broken on Macs... of course.
if((CMAKE_COMPILER_IS_GNUCC OR CMAKE_CXX_COMPILER_ID MATCHES "Clang") AND NOT APPLE AND NOT ANDROID)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-semantic-interposition")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-semantic-interposition")
endif()

# Detect C++ version support.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    include(CheckCXXFlag)
    check_cxx_flag(-Wall COMPILER_SUPPORTS_WALL)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-switch")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-switch")
    if(NOT ANDROID)
      check_cxx_flag(-Wno-class-memaccess COMPILER_SUPPORTS_MEMACCESS)
      check_cxx_flag(-Wno-invalid-offsetof COMPILER_SUPPORTS_OFFSETOF)
    endif()
endif()

# Detect processor type.
if(APPLE AND "${CMAKE_OSX_ARCHITECTURES}" MATCHES "arm64")
  set(CPU_ARCH "aarch64")
elseif(${CMAKE_SYSTEM_PROCESSOR} STREQUAL "x86_64" OR ${CMAKE_SYSTEM_PROCESSOR} STREQUAL "amd64" OR
   ${CMAKE_SYSTEM_PROCESSOR} STREQUAL "AMD64")
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(CPU_ARCH "x64")
  else()
    # Cross-compiling 32-bit build. 32-bit hosts are not supported.
    set(CPU_ARCH "x86")
  endif()
elseif(${CMAKE_SYSTEM_PROCESSOR} STREQUAL "x86" OR ${CMAKE_SYSTEM_PROCESSOR} STREQUAL "i386" OR
       ${CMAKE_SYSTEM_PROCESSOR} STREQUAL "i686")
  set(CPU_ARCH "x86")
elseif(${CMAKE_SYSTEM_PROCESSOR} STREQUAL "aarch64" OR ${CMAKE_SYSTEM_PROCESSOR} STREQUAL "arm64")
  set(CPU_ARCH "aarch64")
elseif(${CMAKE_SYSTEM_PROCESSOR} STREQUAL "arm" OR ${CMAKE_SYSTEM_PROCESSOR} STREQUAL "armv7-a" OR
       ${CMAKE_SYSTEM_PROCESSOR} STREQUAL "armv7l")
  set(CPU_ARCH "aarch32")
  if(ANDROID)
    # Force ARM mode, since apparently ANDROID_ARM_MODE isn't working..
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -marm")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -marm")
  else()
    # Enable NEON.
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -marm -march=armv7-a")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -marm -march=armv7-a")
  endif()
else()
  message(FATAL_ERROR "Unknown system processor: " ${CMAKE_SYSTEM_PROCESSOR})
endif()

# Write binaries to a seperate directory.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")

# Needed for Linux - put shared libraries in the binary directory.
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

# Enable threads everywhere.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# Enable large file support on Linux 32-bit platforms.
# Android is deliberately ommitted here as it didn't support 64-bit ops on files until Android 7/N.
if((LINUX OR FREEBSD) AND (${CPU_ARCH} STREQUAL "x86" OR ${CPU_ARCH} STREQUAL "aarch32"))
  add_definitions("-D_FILE_OFFSET_BITS=64")
endif()

# Recursively include the source tree.
enable_testing()
add_subdirectory(dep)
add_subdirectory(src)
