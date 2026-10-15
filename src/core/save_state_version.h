#pragma once
#include "types.h"

static constexpr uint32_t SAVE_STATE_MAGIC = 0x43435544;
static constexpr uint32_t SAVE_STATE_VERSION = 56;
static constexpr uint32_t SAVE_STATE_MINIMUM_VERSION = 42;

#pragma pack(push, 4)
struct SAVE_STATE_HEADER
{
  static constexpr uint32_t MAX_TITLE_LENGTH = 128, MAX_GAME_CODE_LENGTH = 32;

  uint32_t magic;
  uint32_t version;
  char title[MAX_TITLE_LENGTH];
  char game_code[MAX_GAME_CODE_LENGTH];

  uint32_t media_filename_length;
  uint32_t offset_to_media_filename;
  uint32_t media_subimage_index;
  uint32_t unused_offset_to_playlist_filename; // Unused as of version 51.

  uint32_t screenshot_width;
  uint32_t screenshot_height;
  uint32_t screenshot_size;
  uint32_t offset_to_screenshot;

  uint32_t data_compression_type;
  uint32_t data_compressed_size;
  uint32_t data_uncompressed_size;
  uint32_t offset_to_data;
};
#pragma pack(pop)
