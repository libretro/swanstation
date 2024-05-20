#include "image.h"
#include "byte_stream.h"
#include "file_system.h"
#include "stb_image.h"
#include "stb_image_write.h"
#include "string_util.h"

namespace Common {
bool LoadImageFromFile(Common::RGBA8Image* image, const char* filename)
{
  RFILE *fp = FileSystem::OpenRFile(filename, "rb");
  if (!fp)
    return false;

  int width, height, file_channels;
  u8* pixel_data = stbi_load_from_file(fp, &width, &height, &file_channels, 4);
  if (!pixel_data)
  {
    rfclose(fp);
    return false;
  }

  image->SetPixels(static_cast<u32>(width), static_cast<u32>(height), reinterpret_cast<const u32*>(pixel_data));
  stbi_image_free(pixel_data);
  rfclose(fp);
  return true;
}

bool WriteImageToFile(const RGBA8Image& image, const char* filename)
{
  const char* extension = std::strrchr(filename, '.');
  if (!extension)
    return false;

  RFILE *fp = FileSystem::OpenRFile(filename, "wb");
  if (!fp)
    return {};

  const auto write_func = [](void* context, void* data, int size) {
    rfwrite(data, 1, size, static_cast<RFILE*>(context));
  };

  bool result = false;
  if (StringUtil::Strcasecmp(extension, ".png") == 0)
  {
    result = (stbi_write_png_to_func(write_func, fp, image.GetWidth(), image.GetHeight(), 4, image.GetPixels(),
                                     image.GetByteStride()) != 0);
  }
  else if (StringUtil::Strcasecmp(extension, ".jpg") == 0)
  {
    result = (stbi_write_jpg_to_func(write_func, fp, image.GetWidth(), image.GetHeight(), 4, image.GetPixels(),
                                     95) != 0);
  }
  else if (StringUtil::Strcasecmp(extension, ".tga") == 0)
  {
    result =
      (stbi_write_tga_to_func(write_func, fp, image.GetWidth(), image.GetHeight(), 4, image.GetPixels()) != 0);
  }
  else if (StringUtil::Strcasecmp(extension, ".bmp") == 0)
  {
    result =
      (stbi_write_bmp_to_func(write_func, fp, image.GetWidth(), image.GetHeight(), 4, image.GetPixels()) != 0);
  }
  rfclose(fp);
  if (!result)
    return false;
  return true;
}

} // namespace Common
