#pragma once
#include "timestamp.h"
#include "types.h"
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <streams/file_stream.h>

class ByteStream;

#ifdef _WIN32
#define FS_OSPATH_SEPARATOR_CHARACTER '\\'
#define FS_OSPATH_SEPARATOR_STR "\\"
#else
#define FS_OSPATH_SEPARATOR_CHARACTER '/'
#define FS_OSPATH_SEPARATOR_STR "/"
#endif

inline constexpr u32 FILESYSTEM_FILE_ATTRIBUTE_DIRECTORY = 1, FILESYSTEM_FILE_ATTRIBUTE_READ_ONLY = 2,
                     FILESYSTEM_FILE_ATTRIBUTE_COMPRESSED = 4;

inline constexpr u32 FILESYSTEM_FIND_RECURSIVE = (1 << 0), FILESYSTEM_FIND_RELATIVE_PATHS = (1 << 1),
                     FILESYSTEM_FIND_HIDDEN_FILES = (1 << 2), FILESYSTEM_FIND_FOLDERS = (1 << 3),
                     FILESYSTEM_FIND_FILES = (1 << 4), FILESYSTEM_FIND_KEEP_ARRAY = (1 << 5);

struct FILESYSTEM_STAT_DATA
{
  u32 Attributes;
  Timestamp ModificationTime;
  u64 Size;
};

struct FILESYSTEM_FIND_DATA
{
  std::string FileName;
  Timestamp ModificationTime;
  u32 Attributes;
  u64 Size;
};

namespace FileSystem {

using FindResultsArray = std::vector<FILESYSTEM_FIND_DATA>;

#ifdef __ANDROID__
/// Sets the instance for the FileHelpers Java class, used for storage access framework
/// file access on Android.
void SetAndroidFileHelper(void* jvm, void* env, void* object);
#endif

// canonicalize a path string (i.e. replace .. with actual folder name, etc), if OS path is used, on windows, the
// separators will be \, otherwise /
void CanonicalizePath(char* Destination, u32 cbDestination, const char* Path, bool OSPath = true);
void CanonicalizePath(String& Destination, const char* Path, bool OSPath = true);
void CanonicalizePath(String& Destination, bool OSPath = true);
void CanonicalizePath(std::string& path, bool OSPath = true);

// builds a path relative to the specified file
std::string BuildRelativePath(const std::string_view& filename, const std::string_view& new_filename);

// sanitizes a filename for use in a filesystem.
void SanitizeFileName(char* Destination, u32 cbDestination, const char* FileName, bool StripSlashes = true);
void SanitizeFileName(String& Destination, const char* FileName, bool StripSlashes = true);
void SanitizeFileName(String& Destination, bool StripSlashes = true);
void SanitizeFileName(std::string& Destination, bool StripSlashes = true);

/// Returns true if the specified path is an absolute path (C:\Path on Windows or /path on Unix).
bool IsAbsolutePath(const std::string_view& path);

/// Removes the extension of a filename.
std::string_view StripExtension(const std::string_view& path);

/// Replaces the extension of a filename with another.
std::string ReplaceExtension(const std::string_view& path, const std::string_view& new_extension);

/// Returns the display name of a filename. Usually this is the same as the path, except on Android
/// where it resolves a content URI to its name.
std::string GetDisplayNameFromPath(const std::string_view& path);

/// Returns the directory component of a filename.
std::string_view GetPathDirectory(const std::string_view& path);

/// Returns the filename component of a filename.
std::string_view GetFileNameFromPath(const std::string_view& path);

/// Returns the file title (less the extension and path) from a filename.
std::string_view GetFileTitleFromPath(const std::string_view& path);

// search for files
bool FindFiles(const char* Path, const char* Pattern, u32 Flags, FindResultsArray* pResults);

// stat file
bool StatFile(const char* Path, FILESYSTEM_STAT_DATA* pStatData);
bool StatFile(std::FILE* fp, FILESYSTEM_STAT_DATA* pStatData);

// file exists?
bool FileExists(const char* Path);

// directory exists?
bool DirectoryExists(const char* Path);

// delete file
bool DeleteFile(const char* Path);

// rename file
bool RenamePath(const char* OldPath, const char* NewPath);

// open files
std::unique_ptr<ByteStream> OpenFile(const char* FileName, u32 Flags);

using ManagedCFilePtr = std::unique_ptr<std::FILE, void (*)(std::FILE*)>;
ManagedCFilePtr OpenManagedCFile(const char* filename, const char* mode);
std::FILE* OpenCFile(const char* filename, const char* mode);
int FSeek64(std::FILE* fp, s64 offset, int whence);
s64 FTell64(std::FILE* fp);

std::optional<std::vector<u8>> ReadBinaryFile(const char* filename);
std::optional<std::vector<u8>> ReadBinaryFile(std::FILE* fp);
std::optional<std::string> ReadFileToString(const char* filename);
std::optional<std::string> ReadFileToString(std::FILE* fp);
bool WriteBinaryFile(const char* filename, const void* data, size_t data_length);

// creates a directory in the local filesystem
// if the directory already exists, the return value will be true.
// if Recursive is specified, all parent directories will be created
// if they do not exist.
bool CreateDirectory(const char* Path, bool Recursive);

/// Returns the path to the current executable.
std::string GetProgramPath();

RFILE *OpenRFile(const char* filename, const char* mode);
int RFSeek64(RFILE* fp, s64 offset, int whence);
s64 RFTell64(RFILE* fp);
s64 RFSize64(RFILE* fp);

}; // namespace FileSystem

char *rfgets(char *buffer, int maxCount, RFILE* stream);
int rfeof(RFILE* stream);
int rferror(RFILE* stream);
RFILE* rfopen(const char *path, const char *mode);
int rfclose(RFILE* stream);
int64_t rftell(RFILE* stream);
int64_t rfseek(RFILE* stream, int64_t offset, int origin);
int64_t rfwrite(void const* buffer,
   size_t elem_size, size_t elem_count, RFILE* stream);
int64_t rfread(void* buffer,
   size_t elem_size, size_t elem_count, RFILE* stream);
int rfgetc(RFILE* stream);
