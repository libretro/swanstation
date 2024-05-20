#include "file_system.h"
#include "byte_stream.h"
#include "log.h"
#include "string_util.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <stdlib.h>
#include <sys/param.h>
#else
#include <malloc.h>
#endif

#ifdef __FreeBSD__
#include <sys/sysctl.h>
#endif

#if defined(_WIN32)
#include <shlobj.h>
#include "windows_headers.h"
#else
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include <file/file_path.h>
#include <streams/file_stream.h>

extern "C" int rferror(RFILE* stream)
{
   return filestream_error(stream);
}

extern "C" int rfeof(RFILE* stream)
{
   return filestream_eof(stream);
}

extern "C" char *rfgets(char *buffer, int maxCount, RFILE* stream)
{
   if (!stream)
      return NULL;

   return filestream_gets(stream, buffer, maxCount);
}

extern "C" int rfgetc(RFILE* stream)
{
   if (!stream)
      return EOF;

   return filestream_getc(stream);
}

extern "C" RFILE* rfopen(const char *path, const char *mode)
{
   RFILE          *output  = NULL;
   unsigned int retro_mode = RETRO_VFS_FILE_ACCESS_READ;
   bool position_to_end    = false;

   if (strstr(mode, "r"))
   {
      retro_mode = RETRO_VFS_FILE_ACCESS_READ;
      if (strstr(mode, "+"))
      {
         retro_mode = RETRO_VFS_FILE_ACCESS_READ_WRITE |
            RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING;
      }
   }
   else if (strstr(mode, "w"))
   {
      retro_mode = RETRO_VFS_FILE_ACCESS_WRITE;
      if (strstr(mode, "+"))
         retro_mode = RETRO_VFS_FILE_ACCESS_READ_WRITE;
   }
   else if (strstr(mode, "a"))
   {
      retro_mode = RETRO_VFS_FILE_ACCESS_WRITE |
         RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING;
      position_to_end = true;
      if (strstr(mode, "+"))
      {
         retro_mode = RETRO_VFS_FILE_ACCESS_READ_WRITE |
            RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING;
      }
   }

   output = filestream_open(path, retro_mode,
         RETRO_VFS_FILE_ACCESS_HINT_NONE);
   if (output && position_to_end)
      filestream_seek(output, 0, RETRO_VFS_SEEK_POSITION_END);

   return output;
}

extern "C" int rfclose(RFILE* stream)
{
   if (!stream)
      return EOF;

   return filestream_close(stream);
}

extern "C" int64_t rftell(RFILE* stream)
{
   if (!stream)
      return -1;

   return filestream_tell(stream);
}

extern "C" int64_t rfread(void* buffer,
   size_t elem_size, size_t elem_count, RFILE* stream)
{
   if (!stream || (elem_size == 0) || (elem_count == 0))
      return 0;

   return (filestream_read(stream, buffer, elem_size * elem_count) / elem_size);
}

extern "C" int64_t rfseek(RFILE* stream, int64_t offset, int origin)
{
   int seek_position = -1;

   if (!stream)
      return -1;

   switch (origin)
   {
      case SEEK_SET:
         seek_position = RETRO_VFS_SEEK_POSITION_START;
         break;
      case SEEK_CUR:
         seek_position = RETRO_VFS_SEEK_POSITION_CURRENT;
         break;
      case SEEK_END:
         seek_position = RETRO_VFS_SEEK_POSITION_END;
         break;
   }

   return filestream_seek(stream, offset, seek_position);
}

extern "C" int64_t rfwrite(void const* buffer,
   size_t elem_size, size_t elem_count, RFILE* stream)
{
   if (!stream || (elem_size == 0) || (elem_count == 0))
      return 0;

   return (filestream_write(stream, buffer, elem_size * elem_count) / elem_size);
}

Log_SetChannel(FileSystem);

namespace FileSystem {

void CanonicalizePath(char* Destination, u32 cbDestination, const char* Path, bool OSPath /*= true*/)
{
  u32 i, j;
  // get length
  u32 pathLength = static_cast<u32>(std::strlen(Path));

  // clone to a local buffer if the same pointer
  if (Destination == Path)
  {
    char* pathClone = (char*)alloca(pathLength + 1);
    StringUtil::Strlcpy(pathClone, Path, pathLength + 1);
    Path = pathClone;
  }

  // zero destination
  std::memset(Destination, 0, cbDestination);

  // iterate path
  u32 destinationLength = 0;
  for (i = 0; i < pathLength;)
  {
    char prevCh = (i > 0) ? Path[i - 1] : '\0';
    char currentCh = Path[i];
    char nextCh = (i < (pathLength - 1)) ? Path[i + 1] : '\0';

    if (currentCh == '.')
    {
      if (prevCh == '\\' || prevCh == '/' || prevCh == '\0')
      {
        // handle '.'
        if (nextCh == '\\' || nextCh == '/' || nextCh == '\0')
        {
          // skip '.\'
          i++;

          // remove the previous \, if we have one trailing the dot it'll append it anyway
          if (destinationLength > 0)
            Destination[--destinationLength] = '\0';
          // if there was no previous \, skip past the next one
          else if (nextCh != '\0')
            i++;

          continue;
        }
        // handle '..'
        else if (nextCh == '.')
        {
          char afterNext = ((i + 1) < pathLength) ? Path[i + 2] : '\0';
          if (afterNext == '\\' || afterNext == '/' || afterNext == '\0')
          {
            // remove one directory of the path, including the /.
            if (destinationLength > 1)
            {
              for (j = destinationLength - 2; j > 0; j--)
              {
                if (Destination[j] == '\\' || Destination[j] == '/')
                  break;
              }

              destinationLength = j;
#ifdef _DEBUG
              Destination[destinationLength] = '\0';
#endif
            }

            // skip the dot segment
            i += 2;
            continue;
          }
        }
      }
    }

    // fix ospath
    if (OSPath && (currentCh == '\\' || currentCh == '/'))
      currentCh = FS_OSPATH_SEPARATOR_CHARACTER;

    // copy character
    if (destinationLength < cbDestination)
    {
      Destination[destinationLength++] = currentCh;
#ifdef _DEBUG
      Destination[destinationLength] = '\0';
#endif
    }
    else
      break;

    // increment position by one
    i++;
  }

  // if we end up with the empty string, return '.'
  if (destinationLength == 0)
    Destination[destinationLength++] = '.';

  // ensure nullptr termination
  if (destinationLength < cbDestination)
    Destination[destinationLength] = '\0';
  else
    Destination[destinationLength - 1] = '\0';
}

void CanonicalizePath(String& Destination, const char* Path, bool OSPath /* = true */)
{
  // the function won't actually write any more characters than are present to the buffer,
  // so we can get away with simply passing both pointers if they are the same.
  if (Destination.GetWriteableCharArray() != Path)
  {
    // otherwise, resize the destination to at least the source's size, and then pass as-is
    Destination.Reserve(static_cast<u32>(std::strlen(Path)) + 1);
  }

  CanonicalizePath(Destination.GetWriteableCharArray(), Destination.GetBufferSize(), Path, OSPath);
  Destination.UpdateSize();
}

void CanonicalizePath(String& Destination, bool OSPath /* = true */)
{
  CanonicalizePath(Destination, Destination);
}

void CanonicalizePath(std::string& path, bool OSPath /*= true*/)
{
  CanonicalizePath(path.data(), static_cast<u32>(path.size() + 1), path.c_str(), OSPath);
}

static inline bool FileSystemCharacterIsSane(char c, bool StripSlashes)
{
  if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') && !(c >= '0' && c <= '9') && c != ' ' && c != '_' &&
      c != '-' && c != '.')
  {
    if (!StripSlashes && (c == '/' || c == '\\'))
      return true;

    return false;
  }

  return true;
}

void SanitizeFileName(char* Destination, u32 cbDestination, const char* FileName, bool StripSlashes /* = true */)
{
  u32 i;
  u32 fileNameLength = static_cast<u32>(std::strlen(FileName));

  if (FileName == Destination)
  {
    for (i = 0; i < fileNameLength; i++)
    {
      if (!FileSystemCharacterIsSane(FileName[i], StripSlashes))
        Destination[i] = '_';
    }
  }
  else
  {
    for (i = 0; i < fileNameLength && i < cbDestination; i++)
    {
      if (FileSystemCharacterIsSane(FileName[i], StripSlashes))
        Destination[i] = FileName[i];
      else
        Destination[i] = '_';
    }
  }
}

void SanitizeFileName(String& Destination, const char* FileName, bool StripSlashes /* = true */)
{
  u32 i;
  u32 fileNameLength;

  // if same buffer, use fastpath
  if (Destination.GetWriteableCharArray() == FileName)
  {
    fileNameLength = Destination.GetLength();
    for (i = 0; i < fileNameLength; i++)
    {
      if (!FileSystemCharacterIsSane(FileName[i], StripSlashes))
        Destination[i] = '_';
    }
  }
  else
  {
    fileNameLength = static_cast<u32>(std::strlen(FileName));
    Destination.Resize(fileNameLength);
    for (i = 0; i < fileNameLength; i++)
    {
      if (FileSystemCharacterIsSane(FileName[i], StripSlashes))
        Destination[i] = FileName[i];
      else
        Destination[i] = '_';
    }
  }
}

void SanitizeFileName(String& Destination, bool StripSlashes /* = true */)
{
  return SanitizeFileName(Destination, Destination, StripSlashes);
}

void SanitizeFileName(std::string& Destination, bool StripSlashes /* = true*/)
{
  const std::size_t len = Destination.length();
  for (std::size_t i = 0; i < len; i++)
  {
    if (!FileSystemCharacterIsSane(Destination[i], StripSlashes))
      Destination[i] = '_';
  }
}

bool IsAbsolutePath(const std::string_view& path)
{
#ifdef _WIN32
  return (path.length() >= 3 && ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
          path[1] == ':' && (path[2] == '/' || path[2] == '\\'));
#else
  return (path.length() >= 1 && path[0] == '/');
#endif
}

std::string_view StripExtension(const std::string_view& path)
{
  std::string_view::size_type pos = path.rfind('.');
  if (pos == std::string::npos)
    return path;

  return path.substr(0, pos);
}

std::string ReplaceExtension(const std::string_view& path, const std::string_view& new_extension)
{
  std::string_view::size_type pos = path.rfind('.');
  if (pos == std::string::npos)
    return std::string(path);

  std::string ret(path, 0, pos + 1);
  ret.append(new_extension);
  return ret;
}

static std::string_view::size_type GetLastSeperatorPosition(const std::string_view& filename, bool include_separator)
{
  std::string_view::size_type last_separator = filename.rfind('/');
  if (include_separator && last_separator != std::string_view::npos)
    last_separator++;

#if defined(_WIN32)
  std::string_view::size_type other_last_separator = filename.rfind('\\');
  if (other_last_separator != std::string_view::npos)
  {
    if (include_separator)
      other_last_separator++;
    if (last_separator == std::string_view::npos || other_last_separator > last_separator)
      last_separator = other_last_separator;
  }
#endif

  return last_separator;
}

std::string GetDisplayNameFromPath(const std::string_view& path)
{
  return std::string(GetFileNameFromPath(path));
}

std::string_view GetPathDirectory(const std::string_view& path)
{
  std::string::size_type pos = GetLastSeperatorPosition(path, false);
  if (pos == std::string_view::npos)
    return {};

  return path.substr(0, pos);
}

std::string_view GetFileNameFromPath(const std::string_view& path)
{
  std::string_view::size_type pos = GetLastSeperatorPosition(path, true);
  if (pos == std::string_view::npos)
    return path;

  return path.substr(pos);
}

std::string_view GetFileTitleFromPath(const std::string_view& path)
{
  std::string_view filename(GetFileNameFromPath(path));
  std::string::size_type pos = filename.rfind('.');
  if (pos == std::string_view::npos)
    return filename;

  return filename.substr(0, pos);
}

std::string BuildRelativePath(const std::string_view& filename, const std::string_view& new_filename)
{
  std::string new_string;
  std::string_view::size_type pos = GetLastSeperatorPosition(filename, true);
  if (pos != std::string_view::npos)
    new_string.assign(filename, 0, pos);
  new_string.append(new_filename);
  return new_string;
}

std::unique_ptr<ByteStream> OpenFile(const char* FileName, u32 Flags)
{
  // has a path
  if (FileName[0] == '\0')
    return nullptr;

  // TODO: Handle Android content URIs here.

  // forward to local file wrapper
  return ByteStream_OpenFileStream(FileName, Flags);
}

std::FILE* OpenCFile(const char* filename, const char* mode)
{
#ifdef _WIN32
  int filename_len = static_cast<int>(std::strlen(filename));
  int mode_len = static_cast<int>(std::strlen(mode));
  int wlen = MultiByteToWideChar(CP_UTF8, 0, filename, filename_len, nullptr, 0);
  int wmodelen = MultiByteToWideChar(CP_UTF8, 0, mode, mode_len, nullptr, 0);
  if (wlen > 0 && wmodelen > 0)
  {
    wchar_t* wfilename = static_cast<wchar_t*>(alloca(sizeof(wchar_t) * (wlen + 1)));
    wchar_t* wmode = static_cast<wchar_t*>(alloca(sizeof(wchar_t) * (wmodelen + 1)));
    wlen = MultiByteToWideChar(CP_UTF8, 0, filename, filename_len, wfilename, wlen);
    wmodelen = MultiByteToWideChar(CP_UTF8, 0, mode, mode_len, wmode, wmodelen);
    if (wlen > 0 && wmodelen > 0)
    {
      wfilename[wlen] = 0;
      wmode[wmodelen] = 0;

      std::FILE* fp;
      if (_wfopen_s(&fp, wfilename, wmode) != 0)
        return nullptr;

      return fp;
    }
  }

  std::FILE* fp;
  if (fopen_s(&fp, filename, mode) != 0)
    return nullptr;

  return fp;
#else
  return std::fopen(filename, mode);
#endif
}

std::optional<std::vector<u8>> ReadBinaryFile(const char* filename)
{
  RFILE *fp = OpenRFile(filename, "rb");
  if (!fp)
    return std::nullopt;

  rfseek(fp, 0, SEEK_END);
  int64_t size = rftell(fp);
  rfseek(fp, 0, SEEK_SET);
  if (size < 0)
  {
    rfclose(fp);
    return std::nullopt;
  }

  std::vector<u8> res(static_cast<size_t>(size));
  if (size > 0 && rfread(res.data(), 1u, static_cast<size_t>(size), fp) != static_cast<int64_t>(size))
  {
    rfclose(fp);
    return std::nullopt;
  }
  rfclose(fp);
  return res;
}

std::optional<std::vector<u8>> ReadBinaryFile(RFILE* fp)
{
  rfseek(fp, 0, SEEK_END);
  int64_t size = rftell(fp);
  rfseek(fp, 0, SEEK_SET);
  if (size < 0)
    return std::nullopt;

  std::vector<u8> res(static_cast<size_t>(size));
  if (size > 0 && rfread(res.data(), 1u, static_cast<size_t>(size), fp) != static_cast<int64_t>(size))
    return std::nullopt;

  return res;
}

std::optional<std::string> ReadFileToString(RFILE* fp)
{
  rfseek(fp, 0, SEEK_END);
  int64_t size = rftell(fp);
  rfseek(fp, 0, SEEK_SET);
  if (size < 0)
    return std::nullopt;
  std::string res;
  res.resize(static_cast<size_t>(size));
  if (size > 0 && rfread(res.data(), 1u, static_cast<size_t>(size), fp) != static_cast<int64_t>(size))
    return std::nullopt;
  return res;
}

bool WriteBinaryFile(const char* filename, const void* data, size_t data_length)
{
  RFILE *fp = OpenRFile(filename, "wb");
  if (!fp)
    return false;
  if (data_length > 0 && rfwrite(data, 1u, data_length, fp) != static_cast<int64_t>(data_length))
  {
    rfclose(fp);
    return false;
  }
  rfclose(fp);
  return true;
}

#ifdef _WIN32
static u32 RecursiveFindFiles(const char* OriginPath, const char* ParentPath, const char* Path, const char* Pattern,
                              u32 Flags, FileSystem::FindResultsArray* pResults)
{
  std::string tempStr;
  if (Path)
  {
    if (ParentPath)
      tempStr = StringUtil::StdStringFromFormat("%s\\%s\\%s\\*", OriginPath, ParentPath, Path);
    else
      tempStr = StringUtil::StdStringFromFormat("%s\\%s\\*", OriginPath, Path);
  }
  else
  {
    tempStr = StringUtil::StdStringFromFormat("%s\\*", OriginPath);
  }

  // holder for utf-8 conversion
  WIN32_FIND_DATAW wfd;
  std::string utf8_filename;
  utf8_filename.reserve(countof(wfd.cFileName) * 2);

  HANDLE hFind = FindFirstFileW(StringUtil::UTF8StringToWideString(tempStr).c_str(), &wfd);

  if (hFind == INVALID_HANDLE_VALUE)
    return 0;

  // small speed optimization for '*' case
  bool hasWildCards = false;
  bool wildCardMatchAll = false;
  u32 nFiles = 0;
  if (std::strpbrk(Pattern, "*?") != nullptr)
  {
    hasWildCards = true;
    wildCardMatchAll = !(std::strcmp(Pattern, "*"));
  }

  // iterate results
  do
  {
    if (wfd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN && !(Flags & FILESYSTEM_FIND_HIDDEN_FILES))
      continue;

    if (wfd.cFileName[0] == L'.')
    {
      if (wfd.cFileName[1] == L'\0' || (wfd.cFileName[1] == L'.' && wfd.cFileName[2] == L'\0'))
        continue;
    }

    if (!StringUtil::WideStringToUTF8String(utf8_filename, wfd.cFileName))
      continue;

    FILESYSTEM_FIND_DATA outData;
    outData.Attributes = 0;

    if (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    {
      if (Flags & FILESYSTEM_FIND_RECURSIVE)
      {
        // recurse into this directory
        if (ParentPath != nullptr)
        {
          const std::string recurseDir = StringUtil::StdStringFromFormat("%s\\%s", ParentPath, Path);
          nFiles += RecursiveFindFiles(OriginPath, recurseDir.c_str(), utf8_filename.c_str(), Pattern, Flags, pResults);
        }
        else
        {
          nFiles += RecursiveFindFiles(OriginPath, Path, utf8_filename.c_str(), Pattern, Flags, pResults);
        }
      }

      if (!(Flags & FILESYSTEM_FIND_FOLDERS))
        continue;

      outData.Attributes |= FILESYSTEM_FILE_ATTRIBUTE_DIRECTORY;
    }
    else
    {
      if (!(Flags & FILESYSTEM_FIND_FILES))
        continue;
    }

    if (wfd.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
      outData.Attributes |= FILESYSTEM_FILE_ATTRIBUTE_READ_ONLY;

    // match the filename
    if (hasWildCards)
    {
      if (!wildCardMatchAll && !StringUtil::WildcardMatch(utf8_filename.c_str(), Pattern))
        continue;
    }
    else
    {
      if (std::strcmp(utf8_filename.c_str(), Pattern) != 0)
        continue;
    }

    // add file to list
    // TODO string formatter, clean this mess..
    if (!(Flags & FILESYSTEM_FIND_RELATIVE_PATHS))
    {
      if (ParentPath != nullptr)
        outData.FileName =
          StringUtil::StdStringFromFormat("%s\\%s\\%s\\%s", OriginPath, ParentPath, Path, utf8_filename.c_str());
      else if (Path != nullptr)
        outData.FileName = StringUtil::StdStringFromFormat("%s\\%s\\%s", OriginPath, Path, utf8_filename.c_str());
      else
        outData.FileName = StringUtil::StdStringFromFormat("%s\\%s", OriginPath, utf8_filename.c_str());
    }
    else
    {
      if (ParentPath != nullptr)
        outData.FileName = StringUtil::StdStringFromFormat("%s\\%s\\%s", ParentPath, Path, utf8_filename.c_str());
      else if (Path != nullptr)
        outData.FileName = StringUtil::StdStringFromFormat("%s\\%s", Path, utf8_filename.c_str());
      else
        outData.FileName = utf8_filename;
    }

    outData.Size = (u64)wfd.nFileSizeHigh << 32 | (u64)wfd.nFileSizeLow;

    nFiles++;
    pResults->push_back(std::move(outData));
  } while (FindNextFileW(hFind, &wfd) == TRUE);
  FindClose(hFind);

  return nFiles;
}

bool FileSystem::FindFiles(const char* Path, const char* Pattern, u32 Flags, FindResultsArray* pResults)
{
  // has a path
  if (Path[0] == '\0')
    return false;

  // clear result array
  if (!(Flags & FILESYSTEM_FIND_KEEP_ARRAY))
    pResults->clear();

  // enter the recursive function
  return (RecursiveFindFiles(Path, nullptr, nullptr, Pattern, Flags, pResults) > 0);
}

std::string GetProgramPath()
{
  std::wstring buffer;
  buffer.resize(MAX_PATH);

  // Fall back to the main module if this fails.
  HMODULE module = nullptr;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     reinterpret_cast<LPCWSTR>(&GetProgramPath), &module);


  for (;;)
  {
    DWORD nChars = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (nChars == static_cast<DWORD>(buffer.size()) && GetLastError() == ERROR_INSUFFICIENT_BUFFER)
    {
      buffer.resize(buffer.size() * 2);
      continue;
    }

    buffer.resize(nChars);
    break;
  }

  std::string utf8_path(StringUtil::WideStringToUTF8String(buffer));
  CanonicalizePath(utf8_path);
  return utf8_path;
}

#else
static u32 RecursiveFindFiles(const char* OriginPath, const char* ParentPath, const char* Path, const char* Pattern,
                              u32 Flags, FindResultsArray* pResults)
{
  std::string tempStr;
  if (Path)
  {
    if (ParentPath)
      tempStr = StringUtil::StdStringFromFormat("%s/%s/%s", OriginPath, ParentPath, Path);
    else
      tempStr = StringUtil::StdStringFromFormat("%s/%s", OriginPath, Path);
  }
  else
  {
    tempStr = StringUtil::StdStringFromFormat("%s", OriginPath);
  }

  DIR* pDir = opendir(tempStr.c_str());
  if (pDir == nullptr)
    return 0;

  // small speed optimization for '*' case
  bool hasWildCards = false;
  bool wildCardMatchAll = false;
  u32 nFiles = 0;
  if (std::strpbrk(Pattern, "*?"))
  {
    hasWildCards = true;
    wildCardMatchAll = (std::strcmp(Pattern, "*") == 0);
  }

  // iterate results
  PathString full_path;
  struct dirent* pDirEnt;
  while ((pDirEnt = readdir(pDir)) != nullptr)
  {
    //        if (wfd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN && !(Flags & FILESYSTEM_FIND_HIDDEN_FILES))
    //            continue;
    //
    if (pDirEnt->d_name[0] == '.')
    {
      if (pDirEnt->d_name[1] == '\0' || (pDirEnt->d_name[1] == '.' && pDirEnt->d_name[2] == '\0'))
        continue;

      if (!(Flags & FILESYSTEM_FIND_HIDDEN_FILES))
        continue;
    }

    if (ParentPath != nullptr)
      full_path.Format("%s/%s/%s/%s", OriginPath, ParentPath, Path, pDirEnt->d_name);
    else if (Path != nullptr)
      full_path.Format("%s/%s/%s", OriginPath, Path, pDirEnt->d_name);
    else
      full_path.Format("%s/%s", OriginPath, pDirEnt->d_name);

    FILESYSTEM_FIND_DATA outData;
    outData.Attributes = 0;

    int32_t sdir_size = path_get_size(full_path);

    if (sdir_size == -1)
      continue;

    if (path_is_directory(full_path))
    {
      if (Flags & FILESYSTEM_FIND_RECURSIVE)
      {
        // recurse into this directory
        if (ParentPath != nullptr)
        {
          std::string recursiveDir = StringUtil::StdStringFromFormat("%s/%s", ParentPath, Path);
          nFiles += RecursiveFindFiles(OriginPath, recursiveDir.c_str(), pDirEnt->d_name, Pattern, Flags, pResults);
        }
        else
        {
          nFiles += RecursiveFindFiles(OriginPath, Path, pDirEnt->d_name, Pattern, Flags, pResults);
        }
      }

      if (!(Flags & FILESYSTEM_FIND_FOLDERS))
        continue;

      outData.Attributes |= FILESYSTEM_FILE_ATTRIBUTE_DIRECTORY;
    }
    else
    {
      if (!(Flags & FILESYSTEM_FIND_FILES))
        continue;
    }

    outData.Size = static_cast<u64>(sdir_size);

    // match the filename
    if (hasWildCards)
    {
      if (!wildCardMatchAll && !StringUtil::WildcardMatch(pDirEnt->d_name, Pattern))
        continue;
    }
    else
    {
      if (std::strcmp(pDirEnt->d_name, Pattern) != 0)
        continue;
    }

    // add file to list
    // TODO string formatter, clean this mess..
    if (!(Flags & FILESYSTEM_FIND_RELATIVE_PATHS))
    {
      outData.FileName = std::string(full_path.GetCharArray());
    }
    else
    {
      if (ParentPath != nullptr)
        outData.FileName = StringUtil::StdStringFromFormat("%s\\%s\\%s", ParentPath, Path, pDirEnt->d_name);
      else if (Path != nullptr)
        outData.FileName = StringUtil::StdStringFromFormat("%s\\%s", Path, pDirEnt->d_name);
      else
        outData.FileName = pDirEnt->d_name;
    }

    nFiles++;
    pResults->push_back(std::move(outData));
  }

  closedir(pDir);
  return nFiles;
}

bool FindFiles(const char* Path, const char* Pattern, u32 Flags, FindResultsArray* pResults)
{
  // has a path
  if (Path[0] == '\0')
    return false;

  // clear result array
  if (!(Flags & FILESYSTEM_FIND_KEEP_ARRAY))
    pResults->clear();

  // enter the recursive function
  return (RecursiveFindFiles(Path, nullptr, nullptr, Pattern, Flags, pResults) > 0);
}

std::string GetProgramPath()
{
#if defined(__linux__)
  static const char* exeFileName = "/proc/self/exe";

  int curSize = PATH_MAX;
  char* buffer = static_cast<char*>(std::realloc(nullptr, curSize));
  for (;;)
  {
    int len = readlink(exeFileName, buffer, curSize);
    if (len < 0)
    {
      std::free(buffer);
      return {};
    }
    else if (len < curSize)
    {
      buffer[len] = '\0';
      std::string ret(buffer, len);
      std::free(buffer);
      return ret;
    }

    curSize *= 2;
    buffer = static_cast<char*>(std::realloc(buffer, curSize));
  }

#elif defined(__APPLE__)

  int curSize = PATH_MAX;
  char* buffer = static_cast<char*>(std::realloc(nullptr, curSize));
  for (;;)
  {
    u32 nChars = curSize - 1;
    int res = _NSGetExecutablePath(buffer, &nChars);
    if (res == 0)
    {
      buffer[nChars] = 0;

      char* resolvedBuffer = realpath(buffer, nullptr);
      if (resolvedBuffer == nullptr)
      {
        std::free(buffer);
        return {};
      }

      std::string ret(buffer);
      std::free(buffer);
      return ret;
    }

    curSize *= 2;
    buffer = static_cast<char*>(std::realloc(buffer, curSize + 1));
  }

#elif defined(__FreeBSD__)
  int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1};
  char buffer[PATH_MAX];
  size_t cb = sizeof(buffer) - 1;
  int res = sysctl(mib, countof(mib), buffer, &cb, nullptr, 0);
  if (res != 0)
    return {};

  buffer[cb] = '\0';
  return buffer;
#else
  return {};
#endif
}
#endif

bool DirectoryExists(const char* Path)
{
  // has a path
  if (Path[0] == '\0')
    return false;
  return path_is_directory(Path);
}

bool FileExists(const char* Path)
{
  // has a path
  if (Path[0] == '\0')
    return false;
  return path_is_valid(Path);
}

RFILE* OpenRFile(const char *filename, const char *mode)
{
   RFILE          *output  = NULL;
   unsigned int retro_mode = RETRO_VFS_FILE_ACCESS_READ;
   bool position_to_end    = false;

   if (strstr(mode, "r"))
   {
      retro_mode = RETRO_VFS_FILE_ACCESS_READ;
      if (strstr(mode, "+"))
      {
         retro_mode = RETRO_VFS_FILE_ACCESS_READ_WRITE |
            RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING;
      }
   }
   else if (strstr(mode, "w"))
   {
      retro_mode = RETRO_VFS_FILE_ACCESS_WRITE;
      if (strstr(mode, "+"))
         retro_mode = RETRO_VFS_FILE_ACCESS_READ_WRITE;
   }
   else if (strstr(mode, "a"))
   {
      retro_mode = RETRO_VFS_FILE_ACCESS_WRITE |
         RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING;
      position_to_end = true;
      if (strstr(mode, "+"))
      {
         retro_mode = RETRO_VFS_FILE_ACCESS_READ_WRITE |
            RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING;
      }
   }

   output = filestream_open(filename, retro_mode,
         RETRO_VFS_FILE_ACCESS_HINT_NONE);
   if (output && position_to_end)
      filestream_seek(output, 0, RETRO_VFS_SEEK_POSITION_END);

   return output;
}

s64 FSeek64(RFILE* fp, s64 offset, int whence)
{
   int seek_position = -1;

   if (!fp)
      return -1;

   switch (whence)
   {
      case SEEK_SET:
         seek_position = RETRO_VFS_SEEK_POSITION_START;
         break;
      case SEEK_CUR:
         seek_position = RETRO_VFS_SEEK_POSITION_CURRENT;
         break;
      case SEEK_END:
         seek_position = RETRO_VFS_SEEK_POSITION_END;
         break;
   }

   return filestream_seek(fp, offset, seek_position);
}

s64 FTell64(RFILE* fp)
{
	return filestream_tell(fp);
}

s64 FSize64(RFILE* fp)
{
	const s64 pos = filestream_tell(fp);
	if (pos >= 0)
	{
		if (filestream_seek(fp, 0, RETRO_VFS_SEEK_POSITION_END) == 0)
		{
			const s64 size = filestream_tell(fp);
			if (filestream_seek(fp, pos, RETRO_VFS_SEEK_POSITION_START) == 0)
				return size;
		}
	}

	return -1;
}

} // namespace FileSystem
