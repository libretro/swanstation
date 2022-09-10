#pragma once
#include "types.h"
#include <memory>

// base byte stream creation functions
enum BYTESTREAM_OPEN_MODE
{
  BYTESTREAM_OPEN_READ = 1,           // open stream for writing
  BYTESTREAM_OPEN_WRITE = 2,          // open stream for writing
  BYTESTREAM_OPEN_APPEND = 4,         // seek to the end
  BYTESTREAM_OPEN_TRUNCATE = 8,       // truncate the file, seek to start
  BYTESTREAM_OPEN_CREATE = 16,        // if the file does not exist, create it
  BYTESTREAM_OPEN_CREATE_PATH = 32,   // if the file parent directories don't exist, create them
  BYTESTREAM_OPEN_ATOMIC_UPDATE = 64, //
  BYTESTREAM_OPEN_SEEKABLE = 128,
  BYTESTREAM_OPEN_STREAMED = 256,
};

// interface class used by readers, writers, etc.
class ByteStream
{
public:
  virtual ~ByteStream() {}

  // reads a single byte from the stream.
  virtual bool ReadByte(u8* pDestByte) = 0;

  // read bytes from this stream. returns the number of bytes read, if this isn't equal to the requested size, an error
  // or EOF occurred.
  virtual u32 Read(void* pDestination, u32 ByteCount) = 0;

  // read bytes from this stream, optionally returning the number of bytes read.
  virtual bool Read2(void* pDestination, u32 ByteCount, u32* pNumberOfBytesRead = nullptr) = 0;

  // writes a single byte to the stream.
  virtual bool WriteByte(u8 SourceByte) = 0;

  // write bytes to this stream, returns the number of bytes written. if this isn't equal to the requested size, a
  // buffer overflow, or write error occurred.
  virtual u32 Write(const void* pSource, u32 ByteCount) = 0;

  // write bytes to this stream, optionally returning the number of bytes written.
  virtual bool Write2(const void* pSource, u32 ByteCount, u32* pNumberOfBytesWritten = nullptr) = 0;

  // seeks to the specified position in the stream
  // if seek failed, returns false.
  virtual bool SeekAbsolute(u64 Offset) = 0;
  virtual bool SeekRelative(s64 Offset) = 0;

  // gets the current offset in the stream
  virtual u64 GetPosition() const = 0;

  // gets the size of the stream
  virtual u64 GetSize() const = 0;

  // flush any changes to the stream to disk
  virtual bool Flush() = 0;

  // if the file was opened in atomic update mode, discards any changes made to the file
  virtual bool Discard() = 0;

  // if the file was opened in atomic update mode, commits the file and replaces the temporary file
  virtual bool Commit() = 0;

  // state accessors
  inline void SetErrorState() { m_errorState = true; }

protected:
  ByteStream() : m_errorState(false) {}

  // state bits
  bool m_errorState;

  // make it noncopyable
  ByteStream(const ByteStream&) = delete;
  ByteStream& operator=(const ByteStream&) = delete;
};

class MemoryByteStream final : public ByteStream
{
public:
  MemoryByteStream(void* pMemory, u32 MemSize);
  ~MemoryByteStream() override;

  bool ReadByte(u8* pDestByte) override;
  u32 Read(void* pDestination, u32 ByteCount) override;
  bool Read2(void* pDestination, u32 ByteCount, u32* pNumberOfBytesRead) override;
  bool WriteByte(u8 SourceByte) override;
  u32 Write(const void* pSource, u32 ByteCount) override;
  bool Write2(const void* pSource, u32 ByteCount, u32* pNumberOfBytesWritten) override;
  bool SeekAbsolute(u64 Offset) override;
  bool SeekRelative(s64 Offset) override;
  u64 GetSize() const override;
  u64 GetPosition() const override;
  bool Flush() override;
  bool Commit() override;
  bool Discard() override;

private:
  u8* m_pMemory;
  u32 m_iPosition;
  u32 m_iSize;
};

class ReadOnlyMemoryByteStream final : public ByteStream
{
public:
  ReadOnlyMemoryByteStream(const void* pMemory, u32 MemSize);
  ~ReadOnlyMemoryByteStream() override;

  bool ReadByte(u8* pDestByte) override;
  u32 Read(void* pDestination, u32 ByteCount) override;
  bool Read2(void* pDestination, u32 ByteCount, u32* pNumberOfBytesRead) override;
  bool WriteByte(u8 SourceByte) override;
  u32 Write(const void* pSource, u32 ByteCount) override;
  bool Write2(const void* pSource, u32 ByteCount, u32* pNumberOfBytesWritten) override;
  bool SeekAbsolute(u64 Offset) override;
  bool SeekRelative(s64 Offset) override;
  u64 GetSize() const override;
  u64 GetPosition() const override;
  bool Flush() override;
  bool Commit() override;
  bool Discard() override;

private:
  const u8* m_pMemory;
  u32 m_iPosition;
  u32 m_iSize;
};

class GrowableMemoryByteStream final : public ByteStream
{
public:
  GrowableMemoryByteStream(void* pInitialMem, u32 InitialMemSize);
  ~GrowableMemoryByteStream() override;

  void Resize(u32 new_size);
  void ResizeMemory(u32 new_size);

  bool ReadByte(u8* pDestByte) override;
  u32 Read(void* pDestination, u32 ByteCount) override;
  bool Read2(void* pDestination, u32 ByteCount, u32* pNumberOfBytesRead) override;
  bool WriteByte(u8 SourceByte) override;
  u32 Write(const void* pSource, u32 ByteCount) override;
  bool Write2(const void* pSource, u32 ByteCount, u32* pNumberOfBytesWritten) override;
  bool SeekAbsolute(u64 Offset) override;
  bool SeekRelative(s64 Offset) override;
  u64 GetSize() const override;
  u64 GetPosition() const override;
  bool Flush() override;
  bool Commit() override;
  bool Discard() override;

private:
  void Grow(u32 MinimumGrowth);

  u8* m_pPrivateMemory;
  u8* m_pMemory;
  u32 m_iPosition;
  u32 m_iSize;
  u32 m_iMemorySize;
};

// base byte stream creation functions
// opens a local file-based stream. fills in error if passed, and returns false if the file cannot be opened.
std::unique_ptr<ByteStream> ByteStream_OpenFileStream(const char* FileName, u32 OpenMode);

// memory byte stream, caller is responsible for management, therefore it can be located on either the stack or on the
// heap.
std::unique_ptr<MemoryByteStream> ByteStream_CreateMemoryStream(void* pMemory, u32 Size);

// a growable memory byte stream will automatically allocate its own memory if the provided memory is overflowed.
// a "pure heap" buffer, i.e. a buffer completely managed by this implementation, can be created by supplying a NULL
// pointer and initialSize of zero.
std::unique_ptr<GrowableMemoryByteStream> ByteStream_CreateGrowableMemoryStream(void* pInitialMemory, u32 InitialSize);
std::unique_ptr<GrowableMemoryByteStream> ByteStream_CreateGrowableMemoryStream();

// readable memory stream
std::unique_ptr<ReadOnlyMemoryByteStream> ByteStream_CreateReadOnlyMemoryStream(const void* pMemory, u32 Size);
