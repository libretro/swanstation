#include "byte_stream.h"
#include "file_system.h"
#include "string_util.h"
#include <config.h>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

#if defined(_WIN32)
#include "windows_headers.h"
#include <direct.h>
#include <io.h>
#include <share.h>
#include <malloc.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <alloca.h>
#endif

#include <file/file_path.h>
#include <encodings/utf.h>

class FileByteStream : public ByteStream
{
public:
  FileByteStream(FILE* pFile) : m_pFile(pFile) { }

  virtual ~FileByteStream() override { fclose(m_pFile); }

  bool ReadByte(u8* pDestByte) override
  {
    if (m_errorState)
      return false;

    if (fread(pDestByte, 1, 1, m_pFile) != 1)
    {
      m_errorState = true;
      return false;
    }

    return true;
  }

  u32 Read(void* pDestination, u32 ByteCount) override
  {
    if (m_errorState)
      return 0;

    u32 readCount = (u32)fread(pDestination, 1, ByteCount, m_pFile);
    if (readCount != ByteCount && ferror(m_pFile) != 0)
      m_errorState = true;

    return readCount;
  }

  bool Read2(void* pDestination, u32 ByteCount, u32* pNumberOfBytesRead) override
  {
    if (m_errorState)
      return false;

    u32 bytesRead = Read(pDestination, ByteCount);

    if (pNumberOfBytesRead != nullptr)
      *pNumberOfBytesRead = bytesRead;

    if (bytesRead != ByteCount)
    {
      m_errorState = true;
      return false;
    }

    return true;
  }

  bool WriteByte(u8 SourceByte) override
  {
    if (m_errorState)
      return false;

    if (fwrite(&SourceByte, 1, 1, m_pFile) != 1)
    {
      m_errorState = true;
      return false;
    }

    return true;
  }

  u32 Write(const void* pSource, u32 ByteCount) override
  {
    if (m_errorState)
      return 0;

    u32 writeCount = (u32)fwrite(pSource, 1, ByteCount, m_pFile);
    if (writeCount != ByteCount)
      m_errorState = true;

    return writeCount;
  }

  bool Write2(const void* pSource, u32 ByteCount, u32* pNumberOfBytesWritten) override
  {
    if (m_errorState)
      return false;

    u32 bytesWritten = Write(pSource, ByteCount);

    if (pNumberOfBytesWritten != nullptr)
      *pNumberOfBytesWritten = bytesWritten;

    if (bytesWritten != ByteCount)
    {
      m_errorState = true;
      return false;
    }

    return true;
  }

#if defined(_WIN32)
  bool SeekAbsolute(u64 Offset) override
  {
    if (m_errorState)
      return false;

    if (_fseeki64(m_pFile, Offset, SEEK_SET) != 0)
    {
      m_errorState = true;
      return false;
    }

    return true;
  }

  bool SeekRelative(s64 Offset) override
  {
    if (m_errorState)
      return false;

    if (_fseeki64(m_pFile, Offset, SEEK_CUR) != 0)
    {
      m_errorState = true;
      return true;
    }

    return true;
  }

  u64 GetPosition() const override { return _ftelli64(m_pFile); }

  u64 GetSize() const override
  {
    s64 OldPos = _ftelli64(m_pFile);
    _fseeki64(m_pFile, 0, SEEK_END);
    s64 Size = _ftelli64(m_pFile);
    _fseeki64(m_pFile, OldPos, SEEK_SET);
    return (u64)Size;
  }

#else
  bool SeekAbsolute(u64 Offset) override
  {
    if (m_errorState)
      return false;

    if (fseeko(m_pFile, static_cast<off_t>(Offset), SEEK_SET) != 0)
    {
      m_errorState = true;
      return false;
    }

    return true;
  }

  bool SeekRelative(s64 Offset) override
  {
    if (m_errorState)
      return false;

    if (fseeko(m_pFile, static_cast<off_t>(Offset), SEEK_CUR) != 0)
    {
      m_errorState = true;
      return false;
    }

    return true;
  }

  u64 GetPosition() const override { return static_cast<u64>(ftello(m_pFile)); }

  u64 GetSize() const override
  {
    off_t OldPos = ftello(m_pFile);
    fseeko(m_pFile, 0, SEEK_END);
    off_t Size = ftello(m_pFile);
    fseeko(m_pFile, OldPos, SEEK_SET);
    return (u64)Size;
  }
#endif
  bool Flush() override
  {
    if (m_errorState)
      return false;

    if (fflush(m_pFile) != 0)
    {
      m_errorState = true;
      return false;
    }

    return true;
  }

  virtual bool Commit() override { return true; }

  virtual bool Discard() override { return false; }

protected:
  FILE* m_pFile;
};

class AtomicUpdatedFileByteStream final : public FileByteStream
{
public:
  AtomicUpdatedFileByteStream(FILE* pFile, std::string originalFileName, std::string temporaryFileName)
    : FileByteStream(pFile), m_committed(false), m_discarded(false), m_originalFileName(std::move(originalFileName)),
      m_temporaryFileName(std::move(temporaryFileName))
  {
  }

  ~AtomicUpdatedFileByteStream() override
  {
    if (m_discarded)
    {
#if defined(_WIN32)
      // delete the temporary file
      wchar_t *a = utf8_to_utf16_string_alloc(m_temporaryFileName.c_str());
      if (!DeleteFileW(a)) { }
      free(a);
#else
      // delete the temporary file
      if (remove(m_temporaryFileName.c_str()) < 0) { }
#endif
    }
    else if (!m_committed)
    {
      Commit();
    }

    // fclose called by FileByteStream destructor
  }

  bool Commit() override
  {
    if (m_committed)
      return Flush();

    fflush(m_pFile);

#if defined(_WIN32)
    // move the atomic file name to the original file name
    wchar_t *a = utf8_to_utf16_string_alloc(m_temporaryFileName.c_str());
    wchar_t *b = utf8_to_utf16_string_alloc(m_originalFileName.c_str());
    if (!MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING))
      m_discarded = true;
    else
      m_committed = true;
    free(a);
    free(b);
#else
    // move the atomic file name to the original file name
    if (rename(m_temporaryFileName.c_str(), m_originalFileName.c_str()) < 0)
      m_discarded = true;
    else
      m_committed = true;
#endif

    return (!m_discarded);
  }

  bool Discard() override
  {
    m_discarded = true;
    return true;
  }

private:
  bool m_committed;
  bool m_discarded;
  std::string m_originalFileName;
  std::string m_temporaryFileName;
};

MemoryByteStream::MemoryByteStream(void* pMemory, u32 MemSize)
{
  m_iPosition = 0;
  m_iSize = MemSize;
  m_pMemory = (u8*)pMemory;
}

MemoryByteStream::~MemoryByteStream() {}

bool MemoryByteStream::ReadByte(u8* pDestByte)
{
  if (m_iPosition < m_iSize)
  {
    *pDestByte = m_pMemory[m_iPosition++];
    return true;
  }

  return false;
}

u32 MemoryByteStream::Read(void* pDestination, u32 ByteCount)
{
  u32 sz = ByteCount;
  if ((m_iPosition + ByteCount) > m_iSize)
    sz = m_iSize - m_iPosition;

  if (sz > 0)
  {
    std::memcpy(pDestination, m_pMemory + m_iPosition, sz);
    m_iPosition += sz;
  }

  return sz;
}

bool MemoryByteStream::Read2(void* pDestination, u32 ByteCount, u32* pNumberOfBytesRead /* = nullptr */)
{
  u32 r = Read(pDestination, ByteCount);
  if (pNumberOfBytesRead != NULL)
    *pNumberOfBytesRead = r;

  return (r == ByteCount);
}

bool MemoryByteStream::WriteByte(u8 SourceByte)
{
  if (m_iPosition < m_iSize)
  {
    m_pMemory[m_iPosition++] = SourceByte;
    return true;
  }

  return false;
}

u32 MemoryByteStream::Write(const void* pSource, u32 ByteCount)
{
  u32 sz = ByteCount;
  if ((m_iPosition + ByteCount) > m_iSize)
    sz = m_iSize - m_iPosition;

  if (sz > 0)
  {
    std::memcpy(m_pMemory + m_iPosition, pSource, sz);
    m_iPosition += sz;
  }

  return sz;
}

bool MemoryByteStream::Write2(const void* pSource, u32 ByteCount, u32* pNumberOfBytesWritten /* = nullptr */)
{
  u32 r = Write(pSource, ByteCount);
  if (pNumberOfBytesWritten != nullptr)
    *pNumberOfBytesWritten = r;

  return (r == ByteCount);
}

bool MemoryByteStream::SeekAbsolute(u64 Offset)
{
  u32 Offset32 = (u32)Offset;
  if (Offset32 > m_iSize)
    return false;

  m_iPosition = Offset32;
  return true;
}

bool MemoryByteStream::SeekRelative(s64 Offset)
{
  s32 Offset32 = (s32)Offset;
  if ((Offset32 < 0 && -Offset32 > (s32)m_iPosition) || (u32)((s32)m_iPosition + Offset32) > m_iSize)
    return false;

  m_iPosition += Offset32;
  return true;
}

u64 MemoryByteStream::GetSize() const
{
  return (u64)m_iSize;
}

u64 MemoryByteStream::GetPosition() const
{
  return (u64)m_iPosition;
}

bool MemoryByteStream::Flush()
{
  return true;
}

bool MemoryByteStream::Commit()
{
  return true;
}

bool MemoryByteStream::Discard()
{
  return false;
}

ReadOnlyMemoryByteStream::ReadOnlyMemoryByteStream(const void* pMemory, u32 MemSize)
{
  m_iPosition = 0;
  m_iSize = MemSize;
  m_pMemory = reinterpret_cast<const u8*>(pMemory);
}

ReadOnlyMemoryByteStream::~ReadOnlyMemoryByteStream() {}

bool ReadOnlyMemoryByteStream::ReadByte(u8* pDestByte)
{
  if (m_iPosition < m_iSize)
  {
    *pDestByte = m_pMemory[m_iPosition++];
    return true;
  }

  return false;
}

u32 ReadOnlyMemoryByteStream::Read(void* pDestination, u32 ByteCount)
{
  u32 sz = ByteCount;
  if ((m_iPosition + ByteCount) > m_iSize)
    sz = m_iSize - m_iPosition;

  if (sz > 0)
  {
    std::memcpy(pDestination, m_pMemory + m_iPosition, sz);
    m_iPosition += sz;
  }

  return sz;
}

bool ReadOnlyMemoryByteStream::Read2(void* pDestination, u32 ByteCount, u32* pNumberOfBytesRead /* = nullptr */)
{
  u32 r = Read(pDestination, ByteCount);
  if (pNumberOfBytesRead != nullptr)
    *pNumberOfBytesRead = r;

  return (r == ByteCount);
}

bool ReadOnlyMemoryByteStream::WriteByte(u8 SourceByte)
{
  return false;
}

u32 ReadOnlyMemoryByteStream::Write(const void* pSource, u32 ByteCount)
{
  return 0;
}

bool ReadOnlyMemoryByteStream::Write2(const void* pSource, u32 ByteCount, u32* pNumberOfBytesWritten /* = nullptr */)
{
  return false;
}

bool ReadOnlyMemoryByteStream::SeekAbsolute(u64 Offset)
{
  u32 Offset32 = (u32)Offset;
  if (Offset32 > m_iSize)
    return false;

  m_iPosition = Offset32;
  return true;
}

bool ReadOnlyMemoryByteStream::SeekRelative(s64 Offset)
{
  s32 Offset32 = (s32)Offset;
  if ((Offset32 < 0 && -Offset32 > (s32)m_iPosition) || (u32)((s32)m_iPosition + Offset32) > m_iSize)
    return false;

  m_iPosition += Offset32;
  return true;
}

u64 ReadOnlyMemoryByteStream::GetSize() const
{
  return (u64)m_iSize;
}

u64 ReadOnlyMemoryByteStream::GetPosition() const
{
  return (u64)m_iPosition;
}

bool ReadOnlyMemoryByteStream::Flush()
{
  return false;
}

bool ReadOnlyMemoryByteStream::Commit()
{
  return false;
}

bool ReadOnlyMemoryByteStream::Discard()
{
  return false;
}

GrowableMemoryByteStream::GrowableMemoryByteStream(void* pInitialMem, u32 InitialMemSize)
{
  m_iPosition = 0;
  m_iSize = 0;

  if (pInitialMem != nullptr)
  {
    m_iMemorySize = InitialMemSize;
    m_pPrivateMemory = nullptr;
    m_pMemory = (u8*)pInitialMem;
  }
  else
  {
    m_iMemorySize = std::max(InitialMemSize, (u32)64);
    m_pPrivateMemory = m_pMemory = (u8*)std::malloc(m_iMemorySize);
  }
}

GrowableMemoryByteStream::~GrowableMemoryByteStream()
{
  if (m_pPrivateMemory != nullptr)
    std::free(m_pPrivateMemory);
}

void GrowableMemoryByteStream::Resize(u32 new_size)
{
  if (new_size > m_iMemorySize)
    ResizeMemory(new_size);

  m_iSize = new_size;
}

void GrowableMemoryByteStream::ResizeMemory(u32 new_size)
{
  if (new_size == m_iMemorySize)
    return;

  if (m_pPrivateMemory == nullptr)
  {
    m_pPrivateMemory = (u8*)std::malloc(new_size);
    std::memcpy(m_pPrivateMemory, m_pMemory, m_iSize);
    m_pMemory = m_pPrivateMemory;
    m_iMemorySize = new_size;
  }
  else
  {
    m_pPrivateMemory = m_pMemory = (u8*)std::realloc(m_pPrivateMemory, new_size);
    m_iMemorySize = new_size;
  }
}

bool GrowableMemoryByteStream::ReadByte(u8* pDestByte)
{
  if (m_iPosition < m_iSize)
  {
    *pDestByte = m_pMemory[m_iPosition++];
    return true;
  }

  return false;
}

u32 GrowableMemoryByteStream::Read(void* pDestination, u32 ByteCount)
{
  u32 sz = ByteCount;
  if ((m_iPosition + ByteCount) > m_iSize)
    sz = m_iSize - m_iPosition;

  if (sz > 0)
  {
    std::memcpy(pDestination, m_pMemory + m_iPosition, sz);
    m_iPosition += sz;
  }

  return sz;
}

bool GrowableMemoryByteStream::Read2(void* pDestination, u32 ByteCount, u32* pNumberOfBytesRead /* = nullptr */)
{
  u32 r = Read(pDestination, ByteCount);
  if (pNumberOfBytesRead != NULL)
    *pNumberOfBytesRead = r;

  return (r == ByteCount);
}

bool GrowableMemoryByteStream::WriteByte(u8 SourceByte)
{
  if (m_iPosition == m_iMemorySize)
    Grow(1);

  m_pMemory[m_iPosition++] = SourceByte;
  m_iSize = std::max(m_iSize, m_iPosition);
  return true;
}

u32 GrowableMemoryByteStream::Write(const void* pSource, u32 ByteCount)
{
  if ((m_iPosition + ByteCount) > m_iMemorySize)
    Grow(ByteCount);

  std::memcpy(m_pMemory + m_iPosition, pSource, ByteCount);
  m_iPosition += ByteCount;
  m_iSize = std::max(m_iSize, m_iPosition);
  return ByteCount;
}

bool GrowableMemoryByteStream::Write2(const void* pSource, u32 ByteCount, u32* pNumberOfBytesWritten /* = nullptr */)
{
  u32 r = Write(pSource, ByteCount);
  if (pNumberOfBytesWritten != nullptr)
    *pNumberOfBytesWritten = r;

  return (r == ByteCount);
}

bool GrowableMemoryByteStream::SeekAbsolute(u64 Offset)
{
  u32 Offset32 = (u32)Offset;
  if (Offset32 > m_iSize)
    return false;

  m_iPosition = Offset32;
  return true;
}

bool GrowableMemoryByteStream::SeekRelative(s64 Offset)
{
  s32 Offset32 = (s32)Offset;
  if ((Offset32 < 0 && -Offset32 > (s32)m_iPosition) || (u32)((s32)m_iPosition + Offset32) > m_iSize)
    return false;

  m_iPosition += Offset32;
  return true;
}

u64 GrowableMemoryByteStream::GetSize() const
{
  return (u64)m_iSize;
}

u64 GrowableMemoryByteStream::GetPosition() const
{
  return (u64)m_iPosition;
}

bool GrowableMemoryByteStream::Flush()
{
  return true;
}

bool GrowableMemoryByteStream::Commit()
{
  return true;
}

bool GrowableMemoryByteStream::Discard()
{
  return false;
}

void GrowableMemoryByteStream::Grow(u32 MinimumGrowth)
{
  u32 NewSize = std::max(m_iMemorySize + MinimumGrowth, m_iMemorySize * 2);
  ResizeMemory(NewSize);
}

#if defined(_WIN32)

std::unique_ptr<ByteStream> ByteStream_OpenFileStream(const char* fileName, u32 openMode)
{
  if ((openMode & (BYTESTREAM_OPEN_CREATE | BYTESTREAM_OPEN_WRITE)) == BYTESTREAM_OPEN_WRITE)
  {
    // if opening with write but not create, the path must exist.
    if (!path_is_valid(fileName))
      return nullptr;
  }

  char modeString[16];
  u32 modeStringLength = 0;

  if (openMode & BYTESTREAM_OPEN_WRITE)
  {
    // if the file exists, use r+, otherwise w+
    // HACK: if we're not truncating, and the file exists (we want to only update it), we still have to use r+
    if (!path_is_valid(fileName))
    {
      modeString[modeStringLength++] = 'w';
      if (openMode & BYTESTREAM_OPEN_READ)
        modeString[modeStringLength++] = '+';
    }
    else
    {
      modeString[modeStringLength++] = 'r';
      modeString[modeStringLength++] = '+';
    }

    modeString[modeStringLength++] = 'b';
  }
  else if (openMode & BYTESTREAM_OPEN_READ)
  {
    modeString[modeStringLength++] = 'r';
    modeString[modeStringLength++] = 'b';
  }

  // doesn't work with _fdopen
  if (!(openMode & BYTESTREAM_OPEN_ATOMIC_UPDATE))
  {
    if (openMode & BYTESTREAM_OPEN_STREAMED)
      modeString[modeStringLength++] = 'S';
    else if (openMode & BYTESTREAM_OPEN_SEEKABLE)
      modeString[modeStringLength++] = 'R';
  }

  modeString[modeStringLength] = 0;

  if (openMode & BYTESTREAM_OPEN_CREATE_PATH)
  {
    u32 i;
    u32 fileNameLength = static_cast<u32>(std::strlen(fileName));
    char* tempStr = (char*)alloca(fileNameLength + 1);

    // check if it starts with a drive letter. if so, skip ahead
    if (fileNameLength >= 2 && fileName[1] == ':')
    {
      if (fileNameLength <= 3)
      {
        // create a file called driveletter: or driveletter:\ ? you must be crazy
        i = fileNameLength;
      }
      else
      {
        std::memcpy(tempStr, fileName, 3);
        i = 3;
      }
    }
    else
    {
      // start at beginning
      i = 0;
    }

    // step through each path component, create folders as necessary
    for (; i < fileNameLength; i++)
    {
      if (i > 0 && (fileName[i] == '\\' || fileName[i] == '/'))
      {
        // terminate the string
        tempStr[i] = '\0';

        // check if it exists
	if (!path_is_valid(tempStr))
        {
          if (errno == ENOENT)
          {
            // try creating it
	    if (!path_mkdir(tempStr)) // no point trying any further down the chain
              break;
          }
          else // if (errno == ENOTDIR)
          {
            // well.. someone's trying to open a fucking weird path that is comprised of both directories and files...
            // I aint sticking around here to find out what disaster awaits... let fopen deal with it
            break;
          }
        }

// append platform path seperator
#if defined(_WIN32)
        tempStr[i] = '\\';
#else
        tempStr[i] = '/';
#endif
      }
      else
      {
        // append character to temp string
        tempStr[i] = fileName[i];
      }
    }
  }

  if (openMode & BYTESTREAM_OPEN_ATOMIC_UPDATE)
  {
    // generate the temporary file name
    u32 fileNameLength = static_cast<u32>(std::strlen(fileName));
    char* temporaryFileName = (char*)alloca(fileNameLength + 8);
    std::snprintf(temporaryFileName, fileNameLength + 8, "%s.XXXXXX", fileName);

    // fill in random characters
    _mktemp_s(temporaryFileName, fileNameLength + 8);
    wchar_t *wideTemporaryFileName = utf8_to_utf16_string_alloc(temporaryFileName);

    // massive hack here
    DWORD desiredAccess = GENERIC_WRITE;
    if (openMode & BYTESTREAM_OPEN_READ)
      desiredAccess |= GENERIC_READ;

    HANDLE hFile =
      CreateFileW(wideTemporaryFileName, desiredAccess, FILE_SHARE_DELETE, NULL, CREATE_NEW, 0, NULL);

    if (hFile == INVALID_HANDLE_VALUE)
    {
      free(wideTemporaryFileName);
      return nullptr;
    }

    // get fd from this
    int fd = _open_osfhandle(reinterpret_cast<intptr_t>(hFile), 0);
    if (fd < 0)
    {
      CloseHandle(hFile);
      DeleteFileW(wideTemporaryFileName);
      free(wideTemporaryFileName);
      return nullptr;
    }

    // convert to a stream
    FILE* pTemporaryFile = _fdopen(fd, modeString);
    if (!pTemporaryFile)
    {
      _close(fd);
      DeleteFileW(wideTemporaryFileName);
      free(wideTemporaryFileName);
      return nullptr;
    }

    // create the stream pointer
    std::unique_ptr<AtomicUpdatedFileByteStream> pStream =
      std::make_unique<AtomicUpdatedFileByteStream>(pTemporaryFile, fileName, temporaryFileName);

    // do we need to copy the existing file into this one?
    if (!(openMode & BYTESTREAM_OPEN_TRUNCATE))
    {
      RFILE* pOriginalFile = FileSystem::OpenRFile(fileName, "rb");
      if (!pOriginalFile)
      {
        // this will delete the temporary file
        pStream->Discard();
	free(wideTemporaryFileName);
        return nullptr;
      }

      static const size_t BUFFERSIZE = 4096;
      u8 buffer[BUFFERSIZE];
      while (!rfeof(pOriginalFile))
      {
        size_t nBytes = rfread(buffer, BUFFERSIZE, sizeof(u8), pOriginalFile);
        if (nBytes == 0)
          break;

        if (pStream->Write(buffer, (u32)nBytes) != (u32)nBytes)
        {
          pStream->Discard();
          rfclose(pOriginalFile);
	  free(wideTemporaryFileName);
          return nullptr;
        }
      }

      // close original file
      rfclose(pOriginalFile);
    }

    free(wideTemporaryFileName);
    // return pointer
    return pStream;
  }
  else
  {
    // forward through
    FILE* pFile = FileSystem::OpenCFile(fileName, modeString);
    if (!pFile)
      return nullptr;

    return std::make_unique<FileByteStream>(pFile);
  }
}

#else

std::unique_ptr<ByteStream> ByteStream_OpenFileStream(const char* fileName, u32 openMode)
{
  if ((openMode & (BYTESTREAM_OPEN_CREATE | BYTESTREAM_OPEN_WRITE)) == BYTESTREAM_OPEN_WRITE)
  {
    // if opening with write but not create, the path must exist.
    if (!path_is_valid(fileName))
      return nullptr;
  }

  char modeString[16];
  u32 modeStringLength = 0;

  if (openMode & BYTESTREAM_OPEN_WRITE)
  {
    if (openMode & BYTESTREAM_OPEN_TRUNCATE)
      modeString[modeStringLength++] = 'w';
    else
      modeString[modeStringLength++] = 'a';

    modeString[modeStringLength++] = 'b';

    if (openMode & BYTESTREAM_OPEN_READ)
      modeString[modeStringLength++] = '+';
  }
  else if (openMode & BYTESTREAM_OPEN_READ)
  {
    modeString[modeStringLength++] = 'r';
    modeString[modeStringLength++] = 'b';
  }

  modeString[modeStringLength] = 0;

  if (openMode & BYTESTREAM_OPEN_CREATE_PATH)
  {
    u32 i;
    const u32 fileNameLength = static_cast<u32>(std::strlen(fileName));
    char* tempStr = (char*)alloca(fileNameLength + 1);

#if defined(_WIN32)
    // check if it starts with a drive letter. if so, skip ahead
    if (fileNameLength >= 2 && fileName[1] == ':')
    {
      if (fileNameLength <= 3)
      {
        // create a file called driveletter: or driveletter:\ ? you must be crazy
        i = fileNameLength;
      }
      else
      {
        std::memcpy(tempStr, fileName, 3);
        i = 3;
      }
    }
    else
    {
      // start at beginning
      i = 0;
    }
#endif

    // step through each path component, create folders as necessary
    for (i = 0; i < fileNameLength; i++)
    {
      if (i > 0 && (fileName[i] == '\\' || fileName[i] == '/') && fileName[i] != ':')
      {
        // terminate the string
        tempStr[i] = '\0';

        // check if it exists
	if (!path_is_valid(tempStr))
        {
          if (errno == ENOENT)
          {
            // try creating it
	    if (!path_mkdir(tempStr)) // no point trying any further down the chain
              break;
          }
          else // if (errno == ENOTDIR)
          {
            // well.. someone's trying to open a fucking weird path that is comprised of both directories and
            // files... I aint sticking around here to find out what disaster awaits... let fopen deal with it
            break;
          }
        }

// append platform path seperator
#if defined(_WIN32)
        tempStr[i] = '\\';
#else
        tempStr[i] = '/';
#endif
      }
      else
      {
        // append character to temp string
        tempStr[i] = fileName[i];
      }
    }
  }

  if (openMode & BYTESTREAM_OPEN_ATOMIC_UPDATE)
  {
    // generate the temporary file name
    const u32 fileNameLength = static_cast<u32>(std::strlen(fileName));
    char* temporaryFileName = (char*)alloca(fileNameLength + 8);
    std::snprintf(temporaryFileName, fileNameLength + 8, "%s.XXXXXX", fileName);

    std::FILE* pTemporaryFile;
    // fill in random characters
#ifdef HAVE_MKSTEMP
    int fd = mkstemp(temporaryFileName);
    if (fd == -1)
      return nullptr;
    pTemporaryFile = fdopen(fd, modeString);
#else
    if (mktemp(temporaryFileName) == nullptr)
      return nullptr;
    pTemporaryFile = std::fopen(temporaryFileName, modeString);
#endif
    if (pTemporaryFile == nullptr)
      return nullptr;

    // create the stream pointer
    std::unique_ptr<AtomicUpdatedFileByteStream> pStream =
      std::make_unique<AtomicUpdatedFileByteStream>(pTemporaryFile, fileName, temporaryFileName);

    // do we need to copy the existing file into this one?
    if (!(openMode & BYTESTREAM_OPEN_TRUNCATE))
    {
      RFILE* pOriginalFile = rfopen(fileName, "rb");
      if (!pOriginalFile)
      {
        // this will delete the temporary file
        pStream->SetErrorState();
        return nullptr;
      }

      static const size_t BUFFERSIZE = 4096;
      u8 buffer[BUFFERSIZE];
      while (!rfeof(pOriginalFile))
      {
        size_t nBytes = rfread(buffer, BUFFERSIZE, sizeof(u8), pOriginalFile);
        if (nBytes == 0)
          break;

        if (pStream->Write(buffer, (u32)nBytes) != (u32)nBytes)
        {
          pStream->SetErrorState();
          rfclose(pOriginalFile);
          return nullptr;
        }
      }

      // close original file
      rfclose(pOriginalFile);
    }

    // return pointer
    return pStream;
  }
  else
  {
    std::FILE* pFile = std::fopen(fileName, modeString);
    if (!pFile)
      return nullptr;

    return std::make_unique<FileByteStream>(pFile);
  }
}

#endif

std::unique_ptr<MemoryByteStream> ByteStream_CreateMemoryStream(void* pMemory, u32 Size)
{
  return std::make_unique<MemoryByteStream>(pMemory, Size);
}

std::unique_ptr<ReadOnlyMemoryByteStream> ByteStream_CreateReadOnlyMemoryStream(const void* pMemory, u32 Size)
{
  return std::make_unique<ReadOnlyMemoryByteStream>(pMemory, Size);
}

std::unique_ptr<GrowableMemoryByteStream> ByteStream_CreateGrowableMemoryStream(void* pInitialMemory, u32 InitialSize)
{
  return std::make_unique<GrowableMemoryByteStream>(pInitialMemory, InitialSize);
}

std::unique_ptr<GrowableMemoryByteStream> ByteStream_CreateGrowableMemoryStream()
{
  return std::make_unique<GrowableMemoryByteStream>(nullptr, 0);
}
