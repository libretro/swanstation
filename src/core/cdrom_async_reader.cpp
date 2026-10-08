#include "common/lockfree.h"
#include "cdrom_async_reader.h"
#include "common/log.h"
#include <rthreads/retro_eventcount.h>
#include <rthreads/rthreads.h>
Log_SetChannel(CDROMAsyncReader);

enum class CDROMAsyncReader::Op : uint8_t
{
  ReadUncached,
  SwapMedia,
};

/* The worker owns the media while it runs. The emulation thread posts
 * seek requests and ops by sequence number and reads sectors out of the
 * ring between tail (its front sector) and head (published by the
 * worker); no lock is taken on either side. */
struct CDROMAsyncReader::ThreadState
{
  CDROMAsyncReader* owner = nullptr;
  sthread_t* thread = nullptr;
  retro_eventcount_t worker_ec;
  retro_eventcount_t reader_ec;

  retro_atomic_size_t head;
  retro_atomic_size_t tail;
  retro_atomic_int_t request_seq;
  retro_atomic_int_t request_lba;
  retro_atomic_int_t ack_seq;
  retro_atomic_int_t error_seq;
  retro_atomic_int_t op_seq;
  retro_atomic_int_t op_done;
  retro_atomic_int_t quit;

  /* Published by op_seq, answered by op_done. */
  Op op = Op::ReadUncached;
  CDImage::LBA op_lba = 0;
  CDImage::SubChannelQ* op_subq = nullptr;
  SectorBuffer* op_data = nullptr;
  std::unique_ptr<CDImage>* op_media = nullptr;
  bool op_result = false;

  bool events_ok = false;

  ThreadState()
  {
    retro_atomic_size_init(&head, 0);
    retro_atomic_size_init(&tail, 0);
    retro_atomic_int_init(&request_seq, 0);
    retro_atomic_int_init(&request_lba, 0);
    retro_atomic_int_init(&ack_seq, 0);
    retro_atomic_int_init(&error_seq, 0);
    retro_atomic_int_init(&op_seq, 0);
    retro_atomic_int_init(&op_done, 0);
    retro_atomic_int_init(&quit, 0);
    events_ok = retro_eventcount_init(&worker_ec);
    events_ok = retro_eventcount_init(&reader_ec) && events_ok;
  }

  ~ThreadState()
  {
    retro_eventcount_free(&reader_ec);
    retro_eventcount_free(&worker_ec);
  }
};

CDROMAsyncReader::CDROMAsyncReader() = default;

CDROMAsyncReader::~CDROMAsyncReader()
{
  StopThread();
}

void CDROMAsyncReader::StartThread(uint32_t readahead_count)
{
  if (IsUsingThread())
    StopThread();

  m_buffers.clear();
  m_buffers.resize(readahead_count);
  m_buffer_front = 0;

  std::unique_ptr<ThreadState> t = std::make_unique<ThreadState>();
  t->owner = this;
  if (!t->events_ok)
  {
    Log_ErrorPrint("Failed to initialise read thread events, reading synchronously");
    return;
  }

  m_thread = std::move(t);
  m_thread->thread = sthread_create(&CDROMAsyncReader::WorkerThreadEntryPoint, m_thread.get());
  if (!m_thread->thread)
  {
    Log_ErrorPrint("Failed to create read thread, reading synchronously");
    m_thread.reset();
    return;
  }

  Log_InfoPrintf("Read thread started with readahead of %u sectors", readahead_count);
}

void CDROMAsyncReader::StopThread()
{
  if (!IsUsingThread())
    return;

  retro_atomic_store_release_int(&m_thread->quit, 1);
  retro_eventcount_notify(&m_thread->worker_ec);
  sthread_join(m_thread->thread);
  m_thread.reset();

  m_buffers.clear();
  m_buffer_front = 0;
  m_sector_valid = false;
}

void CDROMAsyncReader::SetMedia(std::unique_ptr<CDImage> media)
{
  if (IsUsingThread())
    RunOnWorker(Op::SwapMedia, 0, nullptr, nullptr, &media);
  else
    m_media = std::move(media);
}

std::unique_ptr<CDImage> CDROMAsyncReader::RemoveMedia()
{
  std::unique_ptr<CDImage> media;
  if (IsUsingThread())
    RunOnWorker(Op::SwapMedia, 0, nullptr, nullptr, &media);
  else
    media = std::move(m_media);
  return media;
}

void CDROMAsyncReader::QueueReadSector(CDImage::LBA lba)
{
  if (!IsUsingThread())
  {
    ReadSectorNonThreaded(lba);
    return;
  }

  ThreadState* t = m_thread.get();
  const int request = retro_atomic_load_relaxed_int(&t->request_seq);
  if (retro_atomic_load_acquire_int(&t->ack_seq) == request)
  {
    const size_t tail = retro_atomic_load_relaxed_size(&t->tail);
    const size_t count = retro_atomic_load_acquire_size(&t->head) - tail;
    if (count > 0)
    {
      // don't re-read the same sector if it was the last one we read
      // the CDC code does this when seeking->reading
      if (m_buffers[m_buffer_front].lba == lba)
        return;

      // did we readahead to the correct sector?
      const uint32_t next_buffer = static_cast<uint32_t>((tail + 1) % m_buffers.size());
      if (count > 1 && m_buffers[next_buffer].lba == lba)
      {
        // great, don't need a seek, but still kick the thread to start reading ahead again
        m_buffer_front = next_buffer;
        retro_atomic_store_release_size(&t->tail, tail + 1);
        retro_eventcount_notify(&t->worker_ec);
        return;
      }
    }
  }

  // we need to toss away our readahead and start fresh
  retro_atomic_store_relaxed_int(&t->request_lba, static_cast<int>(lba));
  retro_atomic_store_release_int(&t->request_seq, NextSequence(request));
  retro_eventcount_notify(&t->worker_ec);
}

bool CDROMAsyncReader::ReadSectorUncached(CDImage::LBA lba, CDImage::SubChannelQ* subq, SectorBuffer* data)
{
  if (!IsUsingThread())
    return InternalReadSectorUncached(lba, subq, data);

  RunOnWorker(Op::ReadUncached, lba, subq, data, nullptr);
  return m_thread->op_result;
}

bool CDROMAsyncReader::InternalReadSectorUncached(CDImage::LBA lba, CDImage::SubChannelQ* subq, SectorBuffer* data)
{
  if (m_media->GetPositionOnDisc() != lba && !m_media->Seek(lba))
  {
    Log_WarningPrintf("Seek to LBA %u failed", lba);
    return false;
  }

  if (!m_media->ReadRawSector(data, subq))
  {
    Log_WarningPrintf("Read of LBA %u failed", lba);
    return false;
  }

  return true;
}

bool CDROMAsyncReader::WaitForReadToComplete()
{
  if (!IsUsingThread())
    return m_sector_valid && m_buffers[m_buffer_front].result;

  ThreadState* t = m_thread.get();
  const int request = retro_atomic_load_relaxed_int(&t->request_seq);
  const size_t tail = retro_atomic_load_relaxed_size(&t->tail);

  /* 1 sector ready, 0 seek failed, -1 still pending. */
  const auto poll = [t, request, tail]() {
    if (retro_atomic_load_acquire_int(&t->ack_seq) != request)
      return -1;
    if (retro_atomic_load_acquire_int(&t->error_seq) == request)
      return 0;
    return (retro_atomic_load_acquire_size(&t->head) != tail) ? 1 : -1;
  };

  int state;
  while ((state = poll()) < 0)
  {
    const int key = retro_eventcount_prepare_wait(&t->reader_ec);
    if ((state = poll()) >= 0)
    {
      retro_eventcount_cancel_wait(&t->reader_ec);
      break;
    }
    retro_eventcount_commit_wait(&t->reader_ec, key);
  }

  return state && m_buffers[m_buffer_front].result;
}

void CDROMAsyncReader::ReadSectorNonThreaded(CDImage::LBA lba)
{
  m_buffers.resize(1);
  m_buffer_front = 0;
  m_sector_valid = false;

  if (m_media->GetPositionOnDisc() != lba && !m_media->Seek(lba))
  {
    Log_WarningPrintf("Seek to LBA %u failed", lba);
    return;
  }

  BufferSlot& buffer = m_buffers.front();
  buffer.lba = m_media->GetPositionOnDisc();

  buffer.result = m_media->ReadRawSector(buffer.data.data(), &buffer.subq);
  m_sector_valid = true;
}

void CDROMAsyncReader::RunOnWorker(Op op, CDImage::LBA lba, CDImage::SubChannelQ* subq, SectorBuffer* data,
                                   std::unique_ptr<CDImage>* media)
{
  ThreadState* t = m_thread.get();
  t->op = op;
  t->op_lba = lba;
  t->op_subq = subq;
  t->op_data = data;
  t->op_media = media;

  const int seq = NextSequence(retro_atomic_load_relaxed_int(&t->op_seq));
  retro_atomic_store_release_int(&t->op_seq, seq);
  retro_eventcount_notify(&t->worker_ec);

  while (retro_atomic_load_acquire_int(&t->op_done) != seq)
  {
    const int key = retro_eventcount_prepare_wait(&t->reader_ec);
    if (retro_atomic_load_acquire_int(&t->op_done) == seq)
    {
      retro_eventcount_cancel_wait(&t->reader_ec);
      break;
    }
    retro_eventcount_commit_wait(&t->reader_ec, key);
  }
}

void CDROMAsyncReader::WorkerThreadEntryPoint(void* userdata)
{
  ThreadState* t = static_cast<ThreadState*>(userdata);
  t->owner->WorkerThread(t);
}

void CDROMAsyncReader::WorkerThread(ThreadState* t)
{
  const size_t num_buffers = m_buffers.size();
  size_t head = 0;
  int ops_done = 0;
  int requests_done = 0;
  bool can_readahead = false;

  for (;;)
  {
    if (retro_atomic_load_acquire_int(&t->quit))
      break;

    const int op = retro_atomic_load_acquire_int(&t->op_seq);
    if (op != ops_done)
    {
      if (t->op == Op::ReadUncached)
      {
        const CDImage::LBA prev_lba = m_media->GetPositionOnDisc();
        t->op_result = InternalReadSectorUncached(t->op_lba, t->op_subq, t->op_data);
        if (!m_media->Seek(prev_lba))
        {
          Log_ErrorPrintf("Failed to re-seek to cached position %u", prev_lba);
          can_readahead = false;
        }
      }
      else
      {
        head = retro_atomic_load_acquire_size(&t->tail);
        retro_atomic_store_release_size(&t->head, head);
        can_readahead = false;
        m_media.swap(*t->op_media);
      }

      ops_done = op;
      retro_atomic_store_release_int(&t->op_done, op);
      retro_eventcount_notify(&t->reader_ec);
      continue;
    }

    const int request = retro_atomic_load_acquire_int(&t->request_seq);
    if (request != requests_done)
    {
      // discard buffers, we're seeking to a new location
      const CDImage::LBA seek_location = static_cast<CDImage::LBA>(retro_atomic_load_relaxed_int(&t->request_lba));
      requests_done = request;
      head = retro_atomic_load_acquire_size(&t->tail);
      retro_atomic_store_release_size(&t->head, head);

      can_readahead =
        m_media && (m_media->GetPositionOnDisc() == seek_location || m_media->Seek(seek_location));
      if (!can_readahead)
      {
        Log_WarningPrintf("Seek to LBA %u failed", seek_location);
        retro_atomic_store_release_int(&t->error_seq, request);
      }

      retro_atomic_store_release_int(&t->ack_seq, request);
      retro_eventcount_notify(&t->reader_ec);
      continue;
    }

    // readahead time! read as many sectors as we have space for
    if (can_readahead && (head - retro_atomic_load_acquire_size(&t->tail)) < num_buffers)
    {
      BufferSlot& buffer = m_buffers[head % num_buffers];
      buffer.lba = m_media->GetPositionOnDisc();
      buffer.result = m_media->ReadRawSector(buffer.data.data(), &buffer.subq);
      retro_atomic_store_release_size(&t->head, ++head);
      retro_eventcount_notify(&t->reader_ec);
      continue;
    }

    const int key = retro_eventcount_prepare_wait(&t->worker_ec);
    if (retro_atomic_load_acquire_int(&t->quit) || retro_atomic_load_acquire_int(&t->op_seq) != ops_done ||
        retro_atomic_load_acquire_int(&t->request_seq) != requests_done ||
        (can_readahead && (head - retro_atomic_load_acquire_size(&t->tail)) < num_buffers))
    {
      retro_eventcount_cancel_wait(&t->worker_ec);
    }
    else
    {
      retro_eventcount_commit_wait(&t->worker_ec, key);
    }
  }
}
