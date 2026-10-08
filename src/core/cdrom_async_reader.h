#pragma once
#include "common/cd_image.h"
#include "types.h"
#include <array>
#include <memory>
#include <vector>

class CDROMAsyncReader
{
public:
  using SectorBuffer = std::array<uint8_t, CDImage::RAW_SECTOR_SIZE>;

  struct BufferSlot
  {
    CDImage::LBA lba;
    SectorBuffer data;
    CDImage::SubChannelQ subq;
    bool result;
  };

  CDROMAsyncReader();
  ~CDROMAsyncReader();

  CDImage::LBA GetLastReadSector() const { return m_buffers[m_buffer_front].lba; }
  const SectorBuffer& GetSectorBuffer() const { return m_buffers[m_buffer_front].data; }
  const CDImage::SubChannelQ& GetSectorSubQ() const { return m_buffers[m_buffer_front].subq; }
  uint32_t GetReadaheadCount() const { return static_cast<uint32_t>(m_buffers.size()); }

  bool HasMedia() const { return static_cast<bool>(m_media); }
  const CDImage* GetMedia() const { return m_media.get(); }
  const std::string& GetMediaFileName() const { return m_media->GetFileName(); }

  bool IsUsingThread() const { return static_cast<bool>(m_thread); }
  void StartThread(uint32_t readahead_count = 8);
  void StopThread();

  void SetMedia(std::unique_ptr<CDImage> media);
  std::unique_ptr<CDImage> RemoveMedia();

  void QueueReadSector(CDImage::LBA lba);

  bool WaitForReadToComplete();

  /// Bypasses the sector cache and reads directly from the image.
  bool ReadSectorUncached(CDImage::LBA lba, CDImage::SubChannelQ* subq, SectorBuffer* data);

private:
  struct ThreadState;
  enum class Op : uint8_t;

  static void WorkerThreadEntryPoint(void* userdata);
  void WorkerThread(ThreadState* t);
  void RunOnWorker(Op op, CDImage::LBA lba, CDImage::SubChannelQ* subq, SectorBuffer* data,
                   std::unique_ptr<CDImage>* media);
  void ReadSectorNonThreaded(CDImage::LBA lba);
  bool InternalReadSectorUncached(CDImage::LBA lba, CDImage::SubChannelQ* subq, SectorBuffer* data);

  std::unique_ptr<CDImage> m_media;

  std::vector<BufferSlot> m_buffers;
  uint32_t m_buffer_front = 0;

  /* Without the thread: whether m_buffers holds a sector. */
  bool m_sector_valid = false;

  std::unique_ptr<ThreadState> m_thread;
};
