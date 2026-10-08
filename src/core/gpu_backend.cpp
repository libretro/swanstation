#include "common/lockfree.h"
#include "gpu_backend.h"
#include "common/align.h"
#include "common/log.h"
#include "common/state_wrapper.h"
#include "settings.h"
#include <new>
#include <retro_spsc.h>
#include <rthreads/retro_eventcount.h>
#include <rthreads/rthreads.h>
Log_SetChannel(GPUBackend);

struct GPUBackend::ThreadState
{
  ThreadState()
  {
    retro_atomic_int_init(&quit, 0);
    retro_atomic_int_init(&syncs_done, 0);
    if (!retro_spsc_init(&ring, COMMAND_QUEUE_SIZE))
      throw std::bad_alloc();
    events_ok = retro_eventcount_init(&work_ec);
    events_ok = retro_eventcount_init(&space_ec) && events_ok;
    events_ok = retro_eventcount_init(&sync_ec) && events_ok;
  }

  ~ThreadState()
  {
    retro_eventcount_free(&sync_ec);
    retro_eventcount_free(&space_ec);
    retro_eventcount_free(&work_ec);
    retro_spsc_free(&ring);
  }

  /* Commands are built in place; one never straddles the end. */
  retro_spsc_t ring;
  retro_eventcount_t work_ec;
  retro_eventcount_t space_ec;
  retro_eventcount_t sync_ec;
  retro_atomic_int_t quit;
  retro_atomic_int_t syncs_done;
  sthread_t* thread = nullptr;

  /* Producer-only. */
  size_t notified_head = 0;
  int syncs_queued = 0;
  bool events_ok = false;
};

GPUBackend::GPUBackend() : m_thread(std::make_unique<ThreadState>()) {}

GPUBackend::~GPUBackend() = default;

bool GPUBackend::Initialize(bool force_thread)
{
  if (force_thread || g_settings.gpu_use_thread)
    StartGPUThread();

  return true;
}

void GPUBackend::Reset(bool clear_vram)
{
  Sync(true);
  m_drawing_area = {};
}

void GPUBackend::UpdateSettings()
{
  Sync(true);

  if (m_use_gpu_thread != g_settings.gpu_use_thread)
  {
    if (!g_settings.gpu_use_thread)
      StopGPUThread();
    else
      StartGPUThread();
  }
}

void GPUBackend::Shutdown()
{
  StopGPUThread();
}

GPUBackendFillVRAMCommand* GPUBackend::NewFillVRAMCommand()
{
  return static_cast<GPUBackendFillVRAMCommand*>(
    AllocateCommand(GPUBackendCommandType::FillVRAM, sizeof(GPUBackendFillVRAMCommand)));
}

GPUBackendUpdateVRAMCommand* GPUBackend::NewUpdateVRAMCommand(uint32_t num_words)
{
  const uint32_t size = sizeof(GPUBackendUpdateVRAMCommand) + (num_words * sizeof(uint16_t));
  GPUBackendUpdateVRAMCommand* cmd =
    static_cast<GPUBackendUpdateVRAMCommand*>(AllocateCommand(GPUBackendCommandType::UpdateVRAM, size));
  return cmd;
}

GPUBackendCopyVRAMCommand* GPUBackend::NewCopyVRAMCommand()
{
  return static_cast<GPUBackendCopyVRAMCommand*>(
    AllocateCommand(GPUBackendCommandType::CopyVRAM, sizeof(GPUBackendCopyVRAMCommand)));
}

GPUBackendSetDrawingAreaCommand* GPUBackend::NewSetDrawingAreaCommand()
{
  return static_cast<GPUBackendSetDrawingAreaCommand*>(
    AllocateCommand(GPUBackendCommandType::SetDrawingArea, sizeof(GPUBackendSetDrawingAreaCommand)));
}

GPUBackendDrawPolygonCommand* GPUBackend::NewDrawPolygonCommand(uint32_t num_vertices)
{
  const uint32_t size = sizeof(GPUBackendDrawPolygonCommand) + (num_vertices * sizeof(GPUBackendDrawPolygonCommand::Vertex));
  GPUBackendDrawPolygonCommand* cmd =
    static_cast<GPUBackendDrawPolygonCommand*>(AllocateCommand(GPUBackendCommandType::DrawPolygon, size));
  cmd->num_vertices = static_cast<uint16_t>(num_vertices);
  return cmd;
}

GPUBackendDrawRectangleCommand* GPUBackend::NewDrawRectangleCommand()
{
  return static_cast<GPUBackendDrawRectangleCommand*>(
    AllocateCommand(GPUBackendCommandType::DrawRectangle, sizeof(GPUBackendDrawRectangleCommand)));
}

GPUBackendDrawLineCommand* GPUBackend::NewDrawLineCommand(uint32_t num_vertices)
{
  const uint32_t size = sizeof(GPUBackendDrawLineCommand) + (num_vertices * sizeof(GPUBackendDrawLineCommand::Vertex));
  GPUBackendDrawLineCommand* cmd =
    static_cast<GPUBackendDrawLineCommand*>(AllocateCommand(GPUBackendCommandType::DrawLine, size));
  cmd->num_vertices = static_cast<uint16_t>(num_vertices);
  return cmd;
}

void* GPUBackend::AllocateCommand(GPUBackendCommandType command, uint32_t size)
{
  // Ensure size is a multiple of 4 so we don't end up with an unaligned command.
  size = Common::AlignUpPow2(size, 4);

  retro_spsc_t* q = &m_thread->ring;
  GPUBackendCommand* cmd;
  if (!m_use_gpu_thread)
  {
    cmd = reinterpret_cast<GPUBackendCommand*>(q->buffer);
  }
  else
  {
    for (;;)
    {
      const size_t head = retro_atomic_load_relaxed_size(&q->head);
      const size_t offset = head & (q->capacity - 1);
      const size_t to_end = q->capacity - offset;

      /* Always leave room for a wraparound marker before the end. */
      const bool wrap = (size + sizeof(GPUBackendCommand)) > to_end;
      const size_t needed = wrap ? to_end : size;
      if (q->capacity - (head - q->cached_tail) < needed)
      {
        q->cached_tail = retro_atomic_load_acquire_size(&q->tail);
        if (q->capacity - (head - q->cached_tail) < needed)
        {
          WaitForSpace(needed);
          continue;
        }
      }

      cmd = reinterpret_cast<GPUBackendCommand*>(q->buffer + offset);
      if (!wrap)
        break;

      cmd->type = GPUBackendCommandType::Wraparound;
      cmd->size = static_cast<uint32_t>(to_end);
      cmd->params.bits = 0;
      retro_spsc_write_end(q, to_end);
    }
  }

  cmd->type = command;
  cmd->size = size;
  return cmd;
}

void GPUBackend::WaitForSpace(size_t bytes)
{
  ThreadState* t = m_thread.get();
  retro_spsc_t* q = &t->ring;
  const size_t head = retro_atomic_load_relaxed_size(&q->head);

  WakeGPUThread();
  for (;;)
  {
    q->cached_tail = retro_atomic_load_acquire_size(&q->tail);
    if (q->capacity - (head - q->cached_tail) >= bytes)
      return;

    const int key = retro_eventcount_prepare_wait(&t->space_ec);
    q->cached_tail = retro_atomic_load_acquire_size(&q->tail);
    if (q->capacity - (head - q->cached_tail) >= bytes)
    {
      retro_eventcount_cancel_wait(&t->space_ec);
      return;
    }
    retro_eventcount_commit_wait(&t->space_ec, key);
  }
}

void GPUBackend::PushCommand(GPUBackendCommand* cmd)
{
  if (!m_use_gpu_thread)
  {
    // single-thread mode
    if (cmd->type != GPUBackendCommandType::Sync)
      HandleCommand(cmd);
  }
  else
  {
    retro_spsc_write_end(&m_thread->ring, cmd->size);
    if ((retro_atomic_load_relaxed_size(&m_thread->ring.head) - m_thread->notified_head) >= THRESHOLD_TO_WAKE_GPU)
      WakeGPUThread();
  }
}

void GPUBackend::WakeGPUThread()
{
  m_thread->notified_head = retro_atomic_load_relaxed_size(&m_thread->ring.head);
  retro_eventcount_notify(&m_thread->work_ec);
}

void GPUBackend::StartGPUThread()
{
  ThreadState* t = m_thread.get();
  if (!t->events_ok)
  {
    Log_ErrorPrint("GPU thread unavailable, rendering on the CPU thread");
    return;
  }

  retro_spsc_clear(&t->ring);
  t->notified_head = 0;
  t->syncs_queued = 0;
  retro_atomic_store_relaxed_int(&t->syncs_done, 0);
  retro_atomic_store_release_int(&t->quit, 0);

  m_use_gpu_thread = true;
  t->thread = sthread_create(&GPUBackend::GPUThreadEntryPoint, this);
  if (!t->thread)
  {
    Log_ErrorPrint("Failed to create GPU thread, rendering on the CPU thread");
    m_use_gpu_thread = false;
  }
}

void GPUBackend::StopGPUThread()
{
  if (!m_use_gpu_thread)
    return;

  retro_atomic_store_release_int(&m_thread->quit, 1);
  WakeGPUThread();
  sthread_join(m_thread->thread);
  m_thread->thread = nullptr;
  m_use_gpu_thread = false;
}

void GPUBackend::Sync(bool allow_sleep)
{
  if (!m_use_gpu_thread)
    return;

  ThreadState* t = m_thread.get();
  GPUBackendSyncCommand* cmd =
    static_cast<GPUBackendSyncCommand*>(AllocateCommand(GPUBackendCommandType::Sync, sizeof(GPUBackendSyncCommand)));
  cmd->allow_sleep = allow_sleep;
  PushCommand(cmd);
  const int ticket = t->syncs_queued = NextSequence(t->syncs_queued);
  WakeGPUThread();

  for (;;)
  {
    if (retro_atomic_load_acquire_int(&t->syncs_done) == ticket)
      return;

    const int key = retro_eventcount_prepare_wait(&t->sync_ec);
    if (retro_atomic_load_acquire_int(&t->syncs_done) == ticket)
    {
      retro_eventcount_cancel_wait(&t->sync_ec);
      return;
    }
    retro_eventcount_commit_wait(&t->sync_ec, key);
  }
}

void GPUBackend::GPUThreadEntryPoint(void* userdata)
{
  static_cast<GPUBackend*>(userdata)->RunGPULoop();
}

void GPUBackend::RunGPULoop()
{
  /* Spin this long for more work after a batch, unless a Sync allowed
   * sleeping, before parking. */
  static constexpr unsigned SPIN_ITERATIONS = 16384;

  ThreadState* t = m_thread.get();
  retro_spsc_t* q = &t->ring;
  bool allow_sleep = true;

  for (;;)
  {
    const void* data;
    const size_t avail = retro_spsc_read_begin(q, &data);
    if (avail == 0)
    {
      if (!allow_sleep)
      {
        for (unsigned i = 0; i < SPIN_ITERATIONS && retro_spsc_read_avail(q) == 0; i++)
          retro_cpu_relax();
        allow_sleep = true;
        continue;
      }

      if (retro_atomic_load_acquire_int(&t->quit))
        break;

      const int key = retro_eventcount_prepare_wait(&t->work_ec);
      if (retro_spsc_read_avail(q) != 0 || retro_atomic_load_acquire_int(&t->quit))
        retro_eventcount_cancel_wait(&t->work_ec);
      else
        retro_eventcount_commit_wait(&t->work_ec, key);
      continue;
    }

    allow_sleep = false;
    const uint8_t* const base = static_cast<const uint8_t*>(data);
    for (size_t offset = 0; offset < avail;)
    {
      const GPUBackendCommand* cmd = reinterpret_cast<const GPUBackendCommand*>(base + offset);
      offset += cmd->size;

      switch (cmd->type)
      {
        case GPUBackendCommandType::Wraparound:
          break;

        case GPUBackendCommandType::Sync:
        {
          allow_sleep = static_cast<const GPUBackendSyncCommand*>(cmd)->allow_sleep;
          retro_atomic_fetch_add_int(&t->syncs_done, 1);
          retro_eventcount_notify(&t->sync_ec);
        }
        break;

        default:
          HandleCommand(cmd);
          break;
      }
    }

    retro_spsc_read_end(q, avail);
    retro_eventcount_notify(&t->space_ec);
  }
}

void GPUBackend::HandleCommand(const GPUBackendCommand* cmd)
{
  switch (cmd->type)
  {
    case GPUBackendCommandType::FillVRAM:
    {
      FlushRender();
      const GPUBackendFillVRAMCommand* ccmd = static_cast<const GPUBackendFillVRAMCommand*>(cmd);
      FillVRAM(static_cast<uint32_t>(ccmd->x), static_cast<uint32_t>(ccmd->y), static_cast<uint32_t>(ccmd->width), static_cast<uint32_t>(ccmd->height),
               ccmd->color, ccmd->params);
    }
    break;

    case GPUBackendCommandType::UpdateVRAM:
    {
      FlushRender();
      const GPUBackendUpdateVRAMCommand* ccmd = static_cast<const GPUBackendUpdateVRAMCommand*>(cmd);
      UpdateVRAM(static_cast<uint32_t>(ccmd->x), static_cast<uint32_t>(ccmd->y), static_cast<uint32_t>(ccmd->width), static_cast<uint32_t>(ccmd->height),
                 ccmd->data, ccmd->params);
    }
    break;

    case GPUBackendCommandType::CopyVRAM:
    {
      FlushRender();
      const GPUBackendCopyVRAMCommand* ccmd = static_cast<const GPUBackendCopyVRAMCommand*>(cmd);
      CopyVRAM(static_cast<uint32_t>(ccmd->src_x), static_cast<uint32_t>(ccmd->src_y), static_cast<uint32_t>(ccmd->dst_x),
               static_cast<uint32_t>(ccmd->dst_y), static_cast<uint32_t>(ccmd->width), static_cast<uint32_t>(ccmd->height), ccmd->params);
    }
    break;

    case GPUBackendCommandType::SetDrawingArea:
    {
      FlushRender();
      m_drawing_area = static_cast<const GPUBackendSetDrawingAreaCommand*>(cmd)->new_area;
    }
    break;

    case GPUBackendCommandType::DrawPolygon:
    {
      DrawPolygon(static_cast<const GPUBackendDrawPolygonCommand*>(cmd));
    }
    break;

    case GPUBackendCommandType::DrawRectangle:
    {
      DrawRectangle(static_cast<const GPUBackendDrawRectangleCommand*>(cmd));
    }
    break;

    case GPUBackendCommandType::DrawLine:
    {
      DrawLine(static_cast<const GPUBackendDrawLineCommand*>(cmd));
    }
    break;

    default:
      break;
  }
}
