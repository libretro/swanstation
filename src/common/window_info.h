#pragma once
#include "types.h"

// Contains the information required to create a graphics context in a window.
struct WindowInfo
{
  void* display_connection = nullptr;
  u32 surface_width = 0;
  u32 surface_height = 0;
};
